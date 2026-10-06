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
class DisplayPipelineController;

/**
 * Keeps visible items' tile plans and demand leases current for a display
 * pipeline host. It does **not** issue requests: tilelod::TileScheduler owns
 * issue order and the global in-flight cap (docs/TILE_STATE_MACHINE.md).
 *
 *   1. Collect viewport hits (gallery screen-edge or tileLodWanted).
 *   2. Cancel PreferCache once per path entering the tile band.
 *   3. tickItemTileLod(item) → ImageItem::tickTileLod: plan + demand renew.
 *
 * GUI thread only (DisplayPipelineController::tickPrimaryTileLod).
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

    explicit TileLoadCoordinator(DisplayPipelineController *pipeline);

    /** Refresh plan + demand for visible items (budget ignored; kept for callers). */
    void tick(int globalBudget);

    /** Drop PreferCache cancel marks (session Open / wipe). */
    void clearPreferCancelled() { m_preferCancelled.clear(); }

private:
    struct Cand {
        ImageItem *item = nullptr;
        bool inView = false;
        bool hasAnyTile = false;
        bool fullyCovered = false;
        /** Visible keys Succeeded or Failed — no further issue this generation. */
        bool settled = false;
        /** Higher = coarser target still incomplete (prefer first). */
        int coveragePriority = 0;
        qreal screenLong = 0;
    };

    QList<Cand> collectCandidates(const QRectF &sceneVis) const;
    static Cand makeCand(ImageItem *ii, bool inView, qreal screenLong);
    static void sortByPolicy(QList<Cand> &cands);

    DisplayPipelineController *m_pipeline = nullptr; // not owned
    /** Paths whose PreferCache climb was cancelled for tile issue this session. */
    QSet<QString> m_preferCancelled;
};

#endif
