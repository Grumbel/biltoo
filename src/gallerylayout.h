// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYLAYOUT_H
#define GALLERYLAYOUT_H

#include <QList>
#include <functional>

class ImageItem;

/**
 * Pure packing for Gallery packaged layouts (no QGraphicsView dependency).
 * Mutates item scale, position, and optional crop cell size.
 */
namespace GalleryLayout {

enum class Mode {
    SideBySide,
    Vertical,
    Grid,
    GridCrop, // disabled in UI; kept for load compatibility
    Masonry,
    MasonryRows,
    /** Column masonry then per-column scale so all columns share one bottom edge. */
    MasonryFill,
    /** Row masonry then per-row scale so all rows share one right edge. */
    MasonryRowsFill,
    /** Session order L→R, T→B; wrap at layout width (columns ≈ pages across). */
    Flow,
    /** Flow, then scale each row to exactly fill layout width. */
    FlowFill,
    /** Two-up spreads; page 1 alone as cover, then pairs (2–3), (4–5), … */
    Facing
};

struct Params {
    Mode mode = Mode::Masonry;
    qreal margin = 16.0;
    qreal gap = 12.0;
    qreal availW = 800.0;
    qreal availH = 600.0;
    int masonryColumns = 3;
    int masonryRows = 3;
    /** Grid / GridCrop / Flow column count; 0 = automatic (ceil sqrt n for Grid, 3 for Flow). */
    int gridColumns = 0;
};

/**
 * Arrange @p items in scene coordinates. Clears gallery crop except for GridCrop.
 * @p afterEach is invoked after each item is placed (e.g. to snapshot state).
 */
void pack(const QList<ImageItem *> &items, const Params &params,
          const std::function<void(ImageItem *)> &afterEach = {});

} // namespace GalleryLayout

#endif // GALLERYLAYOUT_H
