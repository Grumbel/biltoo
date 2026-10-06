// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/tile_load_coordinator.h"
#include "display/displaypipelinecontroller.h"
#include "display/displaypipelinehost.h"
#include "view/viewtransform.h"

#include "util/biltoo_thread.h"
#include "imageitem.h"
#include "crop/cropcontroller.h"
#include "slideshow/slideshowcontroller.h"
#include "gallery/gallerysizeresolve.h"
#include "display/pathrasterservice.h"
#include "host/thumtoocache.h"
#include "tilelod/tile_session.hpp"
#include "tilelod/tile_lod_registry.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QSet>
#include <QTransform>
#include <QWidget>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

TileLoadCoordinator::TileLoadCoordinator(DisplayPipelineController *pipeline)
    : m_pipeline(pipeline)
{
}

TileLoadCoordinator::Cand
TileLoadCoordinator::makeCand(ImageItem *ii, bool inView, qreal screenLong)
{
    Cand c;
    c.item = ii;
    c.inView = inView;
    c.hasAnyTile = ii && (ii->tileLodActive() || ii->tileLodHasPathRam());
    c.fullyCovered = ii && ii->tileLodViewportCovered();
    c.settled = ii && ii->tileLodSettled();
    c.screenLong = screenLong;
    // Settled (all Succeeded or Failed) is not zero-tile work — re-issuing
    // Failed keys is terminal for the generation and only spins the GUI.
    if (c.settled || c.fullyCovered) {
        c.coveragePriority = kPriorityCovered;
    } else if (!c.hasAnyTile) {
        c.coveragePriority = kPriorityZeroTile;
    } else {
        c.coveragePriority = kPriorityIncomplete;
    }
    return c;
}

QList<TileLoadCoordinator::Cand>
TileLoadCoordinator::collectCandidates(const QRectF &sceneVis) const
{
    QList<Cand> cands;
    if (!m_pipeline || !m_pipeline->host() || !m_pipeline->host()->canvasScene()) {
        return cands;
    }
    cands.reserve(16);

    qreal viewScale = 1.0;
    qreal dpr = 1.0;
    if (QWidget *vp = m_pipeline->host()->viewportWidget()) {
        dpr = vp->devicePixelRatioF();
    }
    {
        const QTransform vt = m_pipeline->host()->viewTransform();
        viewScale = qMax(0.01, qMax(qAbs(vt.m11()), qAbs(vt.m22())));
    }
    if (!(dpr > 0.0)) {
        dpr = 1.0;
    }
    constexpr qreal kGalleryTileScreenMin = 32.0;

    // Only items intersecting the viewport — full m_items scan was O(n) with
    // tileLodWanted() (views() + transform + cachedSize) per cell and blew the
    // GUI budget on large galleries (hundreds of ms).
    const QList<QGraphicsItem *> hit =
        sceneVis.isNull()
            ? m_pipeline->host()->canvasScene()->items()
            : m_pipeline->host()->canvasScene()->items(sceneVis, Qt::IntersectsItemBoundingRect);

    for (QGraphicsItem *gi : hit) {
        auto *ii = qgraphicsitem_cast<ImageItem *>(gi);
        if (!ii || ii->path().isEmpty()) {
            continue;
        }
        // Crop draft: sample freeze blocks soft install, not tiles. Same paint
        // path as Image/Workspace. Only explicit suppress skips the grid.
        if (ii->tileLodSuppressed()) {
            continue;
        }
        // Gallery packed cell: screen long edge without re-entering tileLodWanted.
        QSizeF cell = ii->galleryCellSize();
        if (!cell.isEmpty()) {
            const qreal screenLong =
                qMax(cell.width(), cell.height()) * viewScale * dpr;
            if (screenLong <= kGalleryTileScreenMin) {
                continue;
            }
            // Must be able to open a session — otherwise we tick forever with
            // no progress (LQIP stick + CPU).
            if (!ii->tileLodWanted()) {
                continue;
            }
            cands.append(makeCand(ii, true, screenLong));
            continue;
        }
        // Image / Workspace: keep existing gate.
        if (!ii->tileLodWanted()) {
            continue;
        }
        const QRectF br = ii->sceneBoundingRect();
        const qreal screenLong =
            qMax(br.width(), br.height()) * viewScale * dpr;
        cands.append(makeCand(ii, true, screenLong));
    }
    return cands;
}

void TileLoadCoordinator::sortByPolicy(QList<Cand> &cands)
{
    std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
        if (a.inView != b.inView) {
            return a.inView;
        }
        if (a.coveragePriority != b.coveragePriority) {
            return a.coveragePriority > b.coveragePriority;
        }
        return a.screenLong > b.screenLong;
    });
}

void TileLoadCoordinator::tick(int globalBudget)
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("TileLoadCoordinator::tick");
    // Issue budget and order belong to tilelod::TileScheduler (global cap,
    // priority across paths). This pass only keeps each visible item's plan
    // and demand lease current.
    Q_UNUSED(globalBudget);
    if (!m_pipeline || !m_pipeline->host()) {
        return;
    }
    // Slideshow pure-phase drives its own sessions (tickSlideshowTileLod).
    if (m_pipeline->host()->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    // Image ←/→ key-repeat: soft swap only; tiles resume on settle.
    if (m_pipeline->host()->hostSlideshow().hud().isNavHot()) {
        return;
    }
    // Size probes first: do not compete with tiles while Gallery is still
    // resolving the session.
    if (m_pipeline->host()->hostGallerySizeResolve().active()) {
        return;
    }

    const bool gallery = m_pipeline->host()->isGalleryMode();
    QElapsedTimer wall;
    wall.start();
    const qint64 kWallMs = gallery ? 100 : 16;

    QRectF sceneVis;
    if (m_pipeline->host()->canvasScene() && m_pipeline->host()->viewportWidget()) {
        sceneVis = m_pipeline->host()->mapViewportToScene();
    }

    QList<Cand> cands = collectCandidates(sceneVis);
    if (cands.isEmpty()) {
        return;
    }
    sortByPolicy(cands);
    constexpr int kMaxTargets = 256;
    if (cands.size() > kMaxTargets) {
        cands.resize(kMaxTargets);
    }

    // Entering the tile band: cancel the whole-frame PreferCache climb once.
    if (PathRasterService *pathRaster = m_pipeline->host()->hostPathRaster()) {
        for (const Cand &c : cands) {
            const QString path = c.item ? c.item->path() : QString();
            if (path.isEmpty() || m_preferCancelled.contains(path)) {
                continue;
            }
            pathRaster->cancel(path);
            m_preferCancelled.insert(path);
        }
    }

    // Every visible item renews its demand (Workspace has several; a lapsed
    // lease would cancel its loading). Most-needy first in case the wall
    // budget runs out on huge galleries.
    for (const Cand &c : cands) {
        if (wall.elapsed() >= kWallMs) {
            break;
        }
        if (c.item) {
            m_pipeline->tickItemTileLod(c.item, 0);
        }
    }

    if (const char *td = std::getenv("BILTOO_TILE_DEBUG");
        td && td[0] && td[0] != '0') {
        static qint64 s_last = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - s_last >= 500) {
            s_last = now;
            const QString regLine =
                tilelod::TileLodRegistry::instance().debug_summary();
            std::fprintf(stderr, "biltoo/tile-coord: cands=%d wall=%lldms %s\n",
                         static_cast<int>(cands.size()),
                         static_cast<long long>(wall.elapsed()),
                         qPrintable(regLine));
            int samples = 0;
            for (const Cand &c : cands) {
                if (!c.item || samples >= 4) {
                    break;
                }
                std::fprintf(stderr, "  %s\n", qPrintable(c.item->tileLodDebugLine()));
                ++samples;
            }
            std::fflush(stderr);
        }
    }
}
