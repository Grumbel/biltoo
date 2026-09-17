// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_PAINTER_HPP
#define BILTOO_TILELOD_TILE_PAINTER_HPP

#include "tilelod/tile_types.hpp"

#include <QImage>
#include <QPainter>
#include <functional>

namespace tilelod {

/**
 * Resolve a Succeeded cache entry to a QImage for painting.
 * For rgba8 bitmaps builds a shallow-ish QImage from bytes; for jpeg/etc.
 * the host may pre-decode into rgba8 when inserting into the cache.
 */
using TileImageResolver =
    std::function<QImage(TileKey const& key, TileBitmap const& bitmap)>;

struct PaintDrawPlanArgs {
  DrawPlan const* plan = nullptr;
  TileImageResolver resolve;  ///< Required for ExactTile / CoarserTile
  QImage lqip;                ///< Optional underlay when use_lqip
  bool smooth = true;
};

/**
 * Paint a DrawPlan in **content coordinates** (same space as cmd.dst_content).
 * Caller sets the painter transform so content maps to the item/view.
 *
 * Order: for each command, draw the chosen tile sub-rect (or LQIP / skip).
 * Does not draw Empty placeholders (host may already have drawn underlay).
 */
void paint_draw_plan(QPainter* painter, PaintDrawPlanArgs const& args);

/** Decode TileBitmap to QImage (rgba8 copy or QImage::fromData for jpeg). */
QImage tile_bitmap_to_qimage(TileBitmap const& bitmap);

}  // namespace tilelod

#endif
