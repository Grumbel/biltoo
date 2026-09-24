// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_cover_paint.hpp"

#include <algorithm>

namespace tilelod {

bool prepare_and_paint_cover(QPainter *painter, CoverPaintArgs const& args)
{
  if (!painter || !args.lod || args.dest.isEmpty()) {
    return false;
  }
  if (args.native.width() < 1 || args.native.height() < 1) {
    return false;
  }
  const int long_edge =
      args.native.width() > args.native.height() ? args.native.width()
                                                 : args.native.height();
  const double dpc = cover_device_per_content(args.dest.size(), args.native);
  if (!TileLodController::shouldUseTiles(dpc, long_edge)) {
    return false;
  }

  args.lod->setContentSize(args.native.width(), args.native.height(), args.min_scale);
  args.lod->updateViewport(
      QRectF(0, 0, args.native.width(), args.native.height()), dpc, 0.0);
  if (args.tick) {
    (void)args.lod->tick(args.tick_budget);
  }

  // Warm shared path cache counts even before this session's first success.
  if (!args.lod->hasAnyTile() && !args.lod->hasRetainedTiles()) {
    return false;
  }

  painter->save();
  painter->translate(args.dest.topLeft());
  const double sx =
      args.dest.width() / static_cast<double>(std::max(1, args.native.width()));
  const double sy =
      args.dest.height() / static_cast<double>(std::max(1, args.native.height()));
  painter->scale(sx, sy);
  const bool drew = args.lod->paint(painter, args.underlay);
  painter->restore();
  return drew;
}

}  // namespace tilelod
