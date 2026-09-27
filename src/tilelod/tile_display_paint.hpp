// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * Single host path: DrawPlan + shared TileMemoryCache → screen pixels.
 *
 * - Identity cover (Slideshow / filmstrip rasterize): prepare_and_paint_cover
 *   → TileLodController::paint → paint_draw_plan.
 * - Oriented content (ImageItem / Gallery / Workspace): paint_tiles_display
 *   (same plan + resolve, with ContentXform mapping).
 *
 * Tile RAM is always path-keyed in TileLodRegistry (process-wide). Controllers
 * are per surface; Succeeded tiles are shared across modes and widgets.
 */

#include "tilelod/draw_plan.hpp"
#include "tilelod/tile_session.hpp"
#include "content/contentxform.h"

#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <functional>

namespace tilelod {

struct PaintTilesDisplayArgs {
  QPainter *painter = nullptr;
  TileSession *session = nullptr;
  DrawPlan const *plan = nullptr;
  QSize native;
  ContentXform::Value xform;
  QPointF contentOffset;   ///< ImageItem::offset()
  QRectF contentBounds;    ///< item contentRect (clip + overlay)
  bool freeRotPainter = false;
  bool smooth = true;
  QString path;            ///< for debug overlay underlay tag
  bool debugOverlay = false;
  /** Resolve Succeeded tile key → QImage (may apply colour grade). */
  std::function<QImage(TileKey const &)> resolve;
};

/**
 * Paint Exact/Coarser plan cells into display space (orient/flip/crop).
 * Optional plan debug overlay uses the same mapping.
 * @return true if any tile patch was drawn.
 */
[[nodiscard]] bool paint_tiles_display(PaintTilesDisplayArgs const &args);

}  // namespace tilelod
