// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tile_load_coordinator.h"

#include "biltoo_thread.h"
#include "imageitem.h"
#include "imageview.h"
#include "pathrasterservice.h"
#include "tilelod/tile_session.hpp"

#include <QSet>
#include <QTransform>
#include <QWidget>
#include <algorithm>

TileLoadCoordinator::TileLoadCoordinator(ImageView *view)
    : m_view(view)
{
}

QList<TileLoadCoordinator::Cand>
TileLoadCoordinator::collectCandidates(const QRectF &sceneVis) const
{
    QList<Cand> cands;
    if (!m_view) {
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

    const QList<ImageItem *> &items = m_view->liveItems();
    for (ImageItem *ii : items) {
        if (!ii || ii->path().isEmpty() || !ii->tileLodWanted()) {
            continue;
        }
        if (m_view->isCropDraftLockedItem(ii) || ii->tileLodSuppressed()) {
            continue;
        }
        Cand c;
        c.item = ii;
        c.inView = sceneVis.isNull()
            || ii->sceneBoundingRect().intersects(sceneVis);
        c.hasAnyTile = ii->tileLodActive();
        c.fullyCovered = ii->tileLodViewportCovered();

        QSizeF cell = ii->galleryCellSize();
        if (cell.isEmpty()) {
            const QRectF br = ii->sceneBoundingRect();
            cell = QSizeF(br.width(), br.height());
        }
        c.screenLong = qMax(cell.width(), cell.height()) * viewScale * dpr;

        // Prefer paths with no tiles yet, then incomplete exact coverage.
        // coveragePriority: higher runs first among incomplete.
        if (!c.hasAnyTile) {
            c.coveragePriority = 1000;
        } else if (!c.fullyCovered) {
            c.coveragePriority = 500;
        } else {
            c.coveragePriority = 0; // upres / maintain only after others covered
        }
        cands.append(c);
    }
    return cands;
}

void TileLoadCoordinator::sortByPolicy(QList<Cand> &cands)
{
    std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
        // 1. On-screen first
        if (a.inView != b.inView) {
            return a.inView;
        }
        // 2. Need coarse/any coverage before upres of already-covered cells
        if (a.coveragePriority != b.coveragePriority) {
            return a.coveragePriority > b.coveragePriority;
        }
        // 3. Larger on-screen footprint
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

    QRectF sceneVis;
    if (m_view->scene()) {
        if (QWidget *vp = m_view->viewport()) {
            sceneVis = m_view->mapToScene(vp->rect()).boundingRect();
        }
    }

    QList<Cand> cands = collectCandidates(sceneVis);
    if (cands.isEmpty()) {
        return;
    }
    sortByPolicy(cands);

    // Cap concurrent targets — coordinator owns the queue, not each item.
    constexpr int kMaxTargets = 8;
    if (cands.size() > kMaxTargets) {
        cands.resize(kMaxTargets);
    }

    // While any in-view cell still lacks *any* tile, do not spend budget on
    // fully-covered cells (upres). Drain coarse coverage first.
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
        if (anyInViewNeedsCoverage && c.fullyCovered) {
            continue; // upres deferred
        }
        ImageItem *item = c.item;
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (pathRaster && preferCancelled && !path.isEmpty()
            && !preferCancelled->contains(path)) {
            pathRaster->cancel(path);
            preferCancelled->insert(path);
        }
        issueTargets.append(item);
    }

    if (issueTargets.isEmpty()) {
        // Still pump completions on candidates with budget 0.
        for (const Cand &c : cands) {
            if (c.item) {
                c.item->tickTileLod(0);
            }
        }
        return;
    }

    const int n = issueTargets.size();
    int remaining = qMax(0, globalBudget);
    for (int i = 0; i < n; ++i) {
        ImageItem *item = issueTargets.at(i);
        if (!item) {
            continue;
        }
        const int left = n - i;
        const int share = remaining > 0 ? qMax(1, remaining / left) : 0;
        item->tickTileLod(share);
        remaining -= share;
    }
}
