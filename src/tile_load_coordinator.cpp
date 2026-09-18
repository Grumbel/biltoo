// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tile_load_coordinator.h"

#include "biltoo_thread.h"
#include "imageitem.h"
#include "imageview.h"
#include "pathrasterservice.h"
#include "thumtoocache.h"
#include "tilelod/tile_session.hpp"

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

TileLoadCoordinator::TileLoadCoordinator(ImageView *view)
    : m_view(view)
{
}

QList<TileLoadCoordinator::Cand>
TileLoadCoordinator::collectCandidates(const QRectF &sceneVis) const
{
    QList<Cand> cands;
    if (!m_view || !m_view->scene()) {
        return cands;
    }
    cands.reserve(16);

    qreal viewScale = 1.0;
    qreal dpr = 1.0;
    if (QWidget *vp = m_view->viewport()) {
        dpr = vp->devicePixelRatioF();
    }
    {
        const QTransform vt = m_view->transform();
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
            ? m_view->scene()->items()
            : m_view->scene()->items(sceneVis, Qt::IntersectsItemBoundingRect);

    for (QGraphicsItem *gi : hit) {
        auto *ii = qgraphicsitem_cast<ImageItem *>(gi);
        if (!ii || ii->path().isEmpty()) {
            continue;
        }
        if (m_view->isCropDraftLockedItem(ii) || ii->tileLodSuppressed()) {
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
            Cand c;
            c.item = ii;
            c.inView = true;
            c.hasAnyTile = ii->tileLodActive();
            c.fullyCovered = ii->tileLodViewportCovered();
            c.screenLong = screenLong;
            if (!c.hasAnyTile) {
                c.coveragePriority = 1000;
            } else if (!c.fullyCovered) {
                c.coveragePriority = 500;
            } else {
                c.coveragePriority = 0;
            }
            cands.append(c);
            continue;
        }
        // Image / Workspace: keep existing gate.
        if (!ii->tileLodWanted()) {
            continue;
        }
        Cand c;
        c.item = ii;
        c.inView = true;
        c.hasAnyTile = ii->tileLodActive();
        c.fullyCovered = ii->tileLodViewportCovered();
        const QRectF br = ii->sceneBoundingRect();
        c.screenLong = qMax(br.width(), br.height()) * viewScale * dpr;
        if (!c.hasAnyTile) {
            c.coveragePriority = 1000;
        } else if (!c.fullyCovered) {
            c.coveragePriority = 500;
        } else {
            c.coveragePriority = 0;
        }
        cands.append(c);
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
    GUI_BUDGET_MS("TileLoadCoordinator::tick", 4);
    if (!m_view || globalBudget < 0) {
        return;
    }
    if (m_view->isSlideshowProgressActive()) {
        return;
    }
    // Size probes first: do not compete with EnsureTiles while Gallery is still
    // resolving the session (thumtoo prefers tiles over ProbeSize in the queue).
    if (m_view->gallerySizeResolveActive()) {
        return;
    }

    // Coalesce scroll storms — multiple decode-window refreshes per frame.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastTickMs > 0 && (nowMs - m_lastTickMs) < 16) {
        return;
    }
    m_lastTickMs = nowMs;

    QElapsedTimer wall;
    wall.start();
    // Gallery overview: many small cells need coarse tiles quickly. Image-mode
    // deep zoom keeps a tight wall so pan stays responsive.
    const bool gallery = m_view->isGalleryMode();
    // Image deep zoom needs enough wall to fill outer keys, not only center.
    const qint64 kWallMs = gallery ? 12 : 8;

    QRectF sceneVis;
    if (m_view->scene()) {
        if (QWidget *vp = m_view->viewport()) {
            sceneVis = m_view->mapToScene(vp->rect()).boundingRect();
        }
    }

    QList<Cand> cands = collectCandidates(sceneVis);
    if (cands.isEmpty() || wall.elapsed() >= kWallMs) {
        return;
    }
    sortByPolicy(cands);

    // Prefer draining cells with zero tiles first (stuck LQIP / blank).
    // Gallery: issue many overview cells per tick; Image: keep tight.
    const int kMaxTargets = gallery ? 16 : 4;
    if (cands.size() > kMaxTargets) {
        cands.resize(kMaxTargets);
    }

    bool anyInViewNeedsCoverage = false;
    for (const Cand &c : cands) {
        if (c.inView && !c.fullyCovered) {
            anyInViewNeedsCoverage = true;
            break;
        }
    }

    QList<ImageItem *> issueTargets;
    issueTargets.reserve(cands.size());
    PathRasterService *pathRaster = m_view->pathRasterForCoordinator();
    QSet<QString> *preferCancelled = m_view->tileLodPreferCancelledForCoordinator();

    for (const Cand &c : cands) {
        if (wall.elapsed() >= kWallMs) {
            break;
        }
        if (anyInViewNeedsCoverage && c.fullyCovered) {
            continue;
        }
        ImageItem *item = c.item;
        if (!item) {
            continue;
        }
        // Soft cancel is deferred to a single pass after issue targets are
        // chosen — cancel inside the hot loop blocked on PathRaster state.
        issueTargets.append(item);
    }

    if (pathRaster && preferCancelled) {
        for (ImageItem *item : issueTargets) {
            if (!item) {
                continue;
            }
            const QString path = item->path();
            if (path.isEmpty() || preferCancelled->contains(path)) {
                continue;
            }
            pathRaster->cancel(path);
            preferCancelled->insert(path);
            if (wall.elapsed() >= kWallMs) {
                break;
            }
        }
    }

    if (issueTargets.isEmpty()) {
        for (const Cand &c : cands) {
            if (wall.elapsed() >= kWallMs) {
                break;
            }
            if (c.item) {
                c.item->tickTileLod(0);
            }
        }
        return;
    }

    if (const char *td = std::getenv("BILTOO_TILE_DEBUG");
        td && td[0] && td[0] != '0') {
        static qint64 s_last = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - s_last >= 500) {
            s_last = now;
            int zero = 0;
            for (const Cand &c : cands) {
                if (!c.hasAnyTile) {
                    ++zero;
                }
            }
            std::fprintf(stderr,
                         "biltoo/tile-coord: cands=%d zeroTile=%d issue=%d "
                         "budget=%d wall=%lldms\n",
                         static_cast<int>(cands.size()), zero,
                         static_cast<int>(issueTargets.size()), globalBudget,
                         static_cast<long long>(wall.elapsed()));
            int samples = 0;
            for (ImageItem *item : issueTargets) {
                if (!item || samples >= 4) {
                    break;
                }
                std::fprintf(stderr, "  %s\n",
                             qPrintable(item->tileLodDebugLine()));
                ++samples;
            }
            std::fflush(stderr);
        }
    }

    const int n = issueTargets.size();
    int remaining = qMax(0, gallery ? qMax(globalBudget, 48) : globalBudget);
    for (int i = 0; i < n; ++i) {
        if (wall.elapsed() >= kWallMs) {
            break;
        }
        ImageItem *item = issueTargets.at(i);
        if (!item) {
            continue;
        }
        // Gallery overview cells often need only 1–4 coarse keys; allow more
        // keys when budget remains so one tick covers a full cell.
        const int left = n - i;
        const int perCellCap = gallery ? 8 : 4;
        const int share = remaining > 0
            ? qMin(perCellCap, qMax(1, remaining / left))
            : 0;
        item->tickTileLod(share);
        remaining -= share;
        if (wall.elapsed() >= kWallMs) {
            break;
        }
    }
}
