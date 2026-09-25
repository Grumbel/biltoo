// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/draw_plan.hpp"

#include "tilelod/lod_math.hpp"

namespace tilelod {

DrawPlan build_draw_plan(BuildDrawPlanInput const& in)
{
  DrawPlan plan;
  plan.target_scale = in.target_scale;
  if (!in.lookup || in.content_w <= 0 || in.content_h <= 0) {
    return plan;
  }

  plan.commands.reserve(in.visible_keys.size());

  for (TileKey const& key : in.visible_keys) {
    DrawCommand cmd;
    RectI const cr = tile_content_rect(in.content_w, in.content_h, key);
    if (cr.empty()) {
      continue;
    }
    // 1. Exact — dest size follows exclusive source × 2^scale (no stretch to
    // fill a content remainder; that made the right/bottom column look wrong).
    if (CacheEntry const* exact = in.lookup(key);
        exact && exact->state == TileState::Succeeded && exact->bitmap.valid()) {
      cmd.src_key = key;
      int ew = exact->bitmap.width;
      int eh = exact->bitmap.height;
      if (ew > kTileSize) {
        ew = kTileSize;
      }
      if (eh > kTileSize) {
        eh = kTileSize;
      }
      if (ew < 1) {
        ew = 1;
      }
      if (eh < 1) {
        eh = 1;
      }
      cmd.src_uv = {0, 0, static_cast<double>(ew), static_cast<double>(eh)};
      int const factor = (key.scale > 0) ? (1 << key.scale) : 1;
      int dw = ew * factor;
      int dh = eh * factor;
      if (cr.x + dw > in.content_w) {
        dw = in.content_w - cr.x;
      }
      if (cr.y + dh > in.content_h) {
        dh = in.content_h - cr.y;
      }
      if (dw < 1) {
        dw = 1;
      }
      if (dh < 1) {
        dh = 1;
      }
      cmd.dst_content = {static_cast<double>(cr.x), static_cast<double>(cr.y),
                         static_cast<double>(dw), static_cast<double>(dh)};
      cmd.kind = DrawKind::ExactTile;
      plan.any_tile = true;
      plan.commands.push_back(cmd);
      continue;
    }

    cmd.dst_content = {static_cast<double>(cr.x), static_cast<double>(cr.y),
                       static_cast<double>(cr.w), static_cast<double>(cr.h)};

    // 2. Finest available coarser parent
    bool found_parent = false;
    int const max_delta = std::max(0, in.max_scale - key.scale);
    for (int delta = 1; delta <= max_delta; ++delta) {
      TileKey const pk = parent_key(key, delta);
      CacheEntry const* parent = in.lookup(pk);
      if (!parent || parent->state != TileState::Succeeded ||
          !parent->bitmap.valid()) {
        continue;
      }
      cmd.src_key = pk;
      cmd.src_uv = parent_uv_for_child(key, pk, parent->bitmap.width,
                                       parent->bitmap.height, in.content_w,
                                       in.content_h);
      cmd.kind = DrawKind::CoarserTile;
      plan.any_tile = true;
      plan.commands.push_back(cmd);
      found_parent = true;
      break;
    }
    if (found_parent) {
      continue;
    }

    // 3. Hole: host already paints a continuous soft/PreferCache base under the
    // tile grid. Emitting Underlay/Empty here forced paint_draw_plan (and debug
    // washes) to walk every missing cell for no visual gain when lqip is null.
    if (in.has_lqip) {
      cmd.kind = DrawKind::Underlay;
      cmd.use_lqip = true;
      plan.commands.push_back(cmd);
    }
    // else: omit — soft shows through; parent/exact commands still listed above.
  }

  return plan;
}

}  // namespace tilelod
