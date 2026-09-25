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
    cmd.dst_content = {static_cast<double>(cr.x), static_cast<double>(cr.y),
                       static_cast<double>(cr.w), static_cast<double>(cr.h)};

    // 1. Exact
    //   level rect L = exclusive cell on dim_at_tile_scale(content, s)
    //   dest = L * 2^s   (integer, from tile_content_rect)
    //   src  = [0,0]×L   (cap width/height to bitmap; legacy 257 → first 256)
    if (CacheEntry const* exact = in.lookup(key);
        exact && exact->state == TileState::Succeeded && exact->bitmap.valid()) {
      cmd.src_key = key;
      RectI const lr = tile_level_rect(in.content_w, in.content_h, key);
      int ew = lr.w;
      int eh = lr.h;
      if (ew > exact->bitmap.width) {
        ew = exact->bitmap.width;
      }
      if (eh > exact->bitmap.height) {
        eh = exact->bitmap.height;
      }
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
      // Dest stays the exact content mapping of the level grid cell.
      cmd.dst_content = {static_cast<double>(cr.x), static_cast<double>(cr.y),
                         static_cast<double>(cr.w), static_cast<double>(cr.h)};
      cmd.kind = DrawKind::ExactTile;
      plan.any_tile = true;
      plan.commands.push_back(cmd);
      continue;
    }

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

    // 3. Hole: soft underlay shows through unless LQIP is available.
    if (in.has_lqip) {
      cmd.kind = DrawKind::Underlay;
      cmd.use_lqip = true;
      plan.commands.push_back(cmd);
    }
  }

  return plan;
}

}  // namespace tilelod
