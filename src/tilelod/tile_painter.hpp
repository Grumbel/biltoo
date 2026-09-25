// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_PAINTER_HPP
#define BILTOO_TILELOD_TILE_PAINTER_HPP

#include "tilelod/tile_types.hpp"

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <functional>
#include <vector>

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
  /// Content units per device pixel inverse (screen/content).
  double device_per_content = 0.0;
  QImage lqip;                ///< Optional underlay when use_lqip
  bool smooth = true;
};

/**
 * Paint a DrawPlan in **content coordinates** (same space as cmd.dst_content).
 * When @c smooth, tiles are composited 1:1 into one buffer then scaled once
 * (avoids per-cell bilinear clamp seams).
 */
void paint_draw_plan(QPainter* painter, PaintDrawPlanArgs const& args);

/** Decode TileBitmap to QImage (rgba8 copy or QImage::fromData for jpeg). */
QImage tile_bitmap_to_qimage(TileBitmap const& bitmap);

/** One already-decoded patch and its exclusive dest in painter space. */
struct TilePatchBlit {
  QRectF dst;
  QImage patch;
};

/**
 * Paint patches. If @p smooth and assembly succeeds, composite at 1:1 then
 * one SmoothPixmapTransform; else draw each patch with @p smooth hint.
 */
void paint_tile_patches(QPainter* painter, std::vector<TilePatchBlit> const& patches,
                        bool smooth);

}  // namespace tilelod

#endif
