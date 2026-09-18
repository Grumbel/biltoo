// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TILE_LOAD_COORDINATOR_H
#define TILE_LOAD_COORDINATOR_H

#include <QList>
#include <QRectF>
#include <QtGlobal>

class ImageItem;
class ImageView;

/**
 * Sole owner of Gallery/Image tile *issue* policy for an ImageView.
 *
 * ImageItems hold TileLodController for viewport plan + paint only. They must
 * not decide global priority. This coordinator:
 *   1. Collects candidates (tileLodWanted, in view, not suppressed).
 *   2. Prioritizes finishing coarse / incomplete coverage before upres.
 *   3. Splits a global issue budget and calls ImageItem::tickTileLod(share).
 *
 * Call only from the GUI thread (ImageView::tickPrimaryTileLod).
 */
class TileLoadCoordinator
{
public:
    explicit TileLoadCoordinator(ImageView *view);

    /**
     * Issue up to @p globalBudget tile requests across visible need.
     * Completions are still pumped per-item (share may be 0).
     */
    void tick(int globalBudget);

private:
    struct Cand {
        ImageItem *item = nullptr;
        bool inView = false;
        bool hasAnyTile = false;
        bool fullyCovered = false;
        /** Higher = coarser target still incomplete (prefer first). */
        int coveragePriority = 0;
        qreal screenLong = 0;
    };

    QList<Cand> collectCandidates(const QRectF &sceneVis) const;
    static void sortByPolicy(QList<Cand> &cands);

    ImageView *m_view = nullptr;
};

#endif
