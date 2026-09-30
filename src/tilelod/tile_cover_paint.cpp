// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_cover_paint.hpp"
#include "tilelod/tile_display_paint.hpp"
#include "tilelod/tile_painter.hpp"
#include "tilelod/tile_session.hpp"
#include "tilelod/tile_plan_debug_overlay.hpp"

#include <algorithm>

namespace tilelod {
namespace {

bool xformNeedsOrientPaint(ContentXform::Value const &x)
{
  return x.hFlip || x.vFlip || x.hasCrop
      || ContentXform::normalizeQuarterTurns(x.quarterTurns) != 0
      || qAbs(x.cropRotation) > 1e-3;
}

}  // namespace

bool prepare_and_paint_cover(QPainter *painter, CoverPaintArgs const& args)
{
  if (!painter || !args.lod || args.dest.isEmpty()) {
    return false;
  }
  if (args.native.width() < 1 || args.native.height() < 1) {
    return false;
  }

  const bool orientPaint = xformNeedsOrientPaint(args.xform);
  const QSize layout = orientPaint
      ? ContentXform::layoutSize(args.native, args.xform)
      : args.native;
  if (layout.width() < 1 || layout.height() < 1) {
    return false;
  }

  const int long_edge =
      args.native.width() > args.native.height() ? args.native.width()
                                                 : args.native.height();
  // Density from how dest samples the layout footprint (oriented when present).
  const double dpc = cover_device_per_content(args.dest.size(), layout);
  // Retained path tiles: always paint (min-scale overview). shouldUseTiles is
  // for *issuing* new work, not for refusing warm RAM already in the registry.
  const bool retained = args.lod->hasRetainedTiles() || args.lod->hasAnyTile();
  if (!retained && !TileLodController::shouldUseTiles(dpc, long_edge)) {
    return false;
  }

  // Viewport stays source/native: tile keys and pyramid are path-keyed.
  args.lod->setContentSize(args.native.width(), args.native.height(), args.min_scale);
  args.lod->updateViewport(
      QRectF(0, 0, args.native.width(), args.native.height()), dpc, 0.0);
  if (args.tick) {
    (void)args.lod->tick(args.tick_budget);
  }

  if (!args.lod->hasAnyTile() && !args.lod->hasRetainedTiles()) {
    return false;
  }

  if (!orientPaint) {
    painter->save();
    painter->translate(args.dest.topLeft());
    const double sx =
        args.dest.width() / static_cast<double>(std::max(1, args.native.width()));
    const double sy =
        args.dest.height() / static_cast<double>(std::max(1, args.native.height()));
    painter->scale(sx, sy);
    const bool drew = args.lod->paint(painter, args.underlay);
    if (tilePlanDebugOverlayEnabled() && args.lod->session()) {
      const DrawPlan plan = args.lod->session()->draw_plan();
      paintTilePlanDebugOverlay(
          painter, args.lod->session(), plan,
          QRectF(0, 0, args.native.width(), args.native.height()),
          QPointF(), args.native, ContentXform::Value{},
          false, args.lod->path());
    }
    painter->restore();
    return drew;
  }

  // Oriented: scale layout → dest, then same paint_tiles_display as ImageItem.
  if (!args.lod->session()) {
    return false;
  }
  DrawPlan plan = args.lod->session()->draw_plan();
  if (plan.commands.empty()) {
    return false;
  }

  painter->save();
  painter->translate(args.dest.topLeft());
  const double sx =
      args.dest.width() / static_cast<double>(std::max(1, layout.width()));
  const double sy =
      args.dest.height() / static_cast<double>(std::max(1, layout.height()));
  painter->scale(sx, sy);

  const bool freeRot = args.xform.hasCrop && !args.xform.cropRect.isEmpty()
      && qAbs(args.xform.cropRotation) > 1e-3;
  PaintTilesDisplayArgs targs;
  targs.painter = painter;
  targs.session = args.lod->session();
  targs.plan = &plan;
  targs.native = args.native;
  targs.xform = args.xform;
  targs.contentOffset = QPointF();
  targs.contentBounds = QRectF(0, 0, layout.width(), layout.height());
  targs.freeRotPainter = freeRot;
  targs.smooth = true;
  targs.path = args.lod->path();
  targs.debugOverlay = tilePlanDebugOverlayEnabled();
  targs.resolve = [session = args.lod->session()](TileKey const &key) -> QImage {
    if (!session) {
      return {};
    }
    CacheEntry const *e = session->cache().find(key);
    if (!e || e->state != TileState::Succeeded || !e->bitmap.valid()) {
      return {};
    }
    return tile_bitmap_to_qimage(e->bitmap);
  };
  const bool drew = paint_tiles_display(targs);
  painter->restore();
  return drew;
}

}  // namespace tilelod
