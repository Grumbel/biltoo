// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TILE_LOAD_COORDINATOR_H
#define TILE_LOAD_COORDINATOR_H

#include <QList>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QtGlobal>

class ImageItem;
class ImageView;

/**
 * Sole owner of Gallery/Image tile *load* policy for an ImageView.
 *
 * All tile issue / progressive climb / per-item tick routing goes through here.
 * ImageItem::tickTileLod is the only per-item entry (prepare + pump + issue +
 * repaint). Paint only draws the current DrawPlan — no scheduling.
 *
 *   1. Collect viewport hits (gallery screen-edge or tileLodWanted).
 *   2. Prioritize cells with zero tiles (LQIP/blank) before upres.
 *   3. Split budget; call ImageItem::tickTileLod(share).
 *
 * GUI thread only (ImageView::tickPrimaryTileLod).
 */
class TileLoadCoordinator
{
public:
    /** Default tile requests per GUI tick (slideshow warm / primary climb). */
    static constexpr int kDefaultTickBudget = 16;
    /** Collect sort keys: higher coveragePriority is issued first. */
    static constexpr int kPriorityZeroTile = 1000;
    static constexpr int kPriorityIncomplete = 500;
    static constexpr int kPriorityCovered = 0;

    explicit TileLoadCoordinator(ImageView *view);

    /**
     * Issue up to @p globalBudget tile requests across visible need.
     * Completions are still pumped per-item (share may be 0).
     */
    void tick(int globalBudget);

    /** Drop PreferCache cancel marks (session Open / wipe). */
    void clearPreferCancelled() { m_preferCancelled.clear(); }

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
    static Cand makeCand(ImageItem *ii, bool inView, qreal screenLong);
    static void sortByPolicy(QList<Cand> &cands);

    ImageView *m_view = nullptr;
    qint64 m_lastTickMs = 0;
    /** Paths whose PreferCache climb was cancelled for tile issue this session. */
    QSet<QString> m_preferCancelled;
};

#endif
