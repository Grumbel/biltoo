// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * Shared “full native content stretched into dest” tile paint for ImageView,
 * Gallery, and Slideshow. Same transform model as drawImage(dest, fullFrame).
 */

#include "tilelod/tile_lod_controller.hpp"

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QSize>

namespace tilelod {

struct CoverPaintArgs {
  TileLodController *lod = nullptr;
  QSize native;                 ///< content pixel size (scale 0)
  QRectF dest;                  ///< device/view destination rect
  QImage underlay;              ///< LQIP / soft under holes
  int tick_budget = 24;         ///< issue budget per prepare
  int min_scale = 0;            ///< Image/Slideshow: 0 so density can climb
  bool tick = true;             ///< false when host already ticked this frame
};

/**
 * Update viewport for full-content cover into @p args.dest, optionally tick,
 * then paint tiles in dest space.
 * @return true if any tile (or tile underlay plan) was drawn.
 */
[[nodiscard]] bool prepare_and_paint_cover(QPainter *painter, CoverPaintArgs const& args);

/** device pixels per content pixel for uniform cover of native into dest. */
[[nodiscard]] inline double cover_device_per_content(QSizeF const& dest, QSize const& native)
{
  if (native.width() < 1 || native.height() < 1) {
    return 0.0;
  }
  double const sx = dest.width() / static_cast<double>(native.width());
  double const sy = dest.height() / static_cast<double>(native.height());
  return sx > sy ? sx : sy;
}

}  // namespace tilelod
