// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_DRAW_PLAN_HPP
#define BILTOO_TILELOD_DRAW_PLAN_HPP

#include "tilelod/tile_types.hpp"

#include <functional>
#include <vector>

namespace tilelod {

/**
 * Lookup function for the RAM cache: return Succeeded entry or nullopt.
 * Pure relative to the provided lookup — no side effects required.
 */
using TileLookup = std::function<CacheEntry const*(TileKey const&)>;

struct BuildDrawPlanInput {
  int content_w = 0;
  int content_h = 0;
  int target_scale = 0;
  int max_scale = 0;
  std::vector<TileKey> visible_keys;
  TileLookup lookup;  ///< Required; returns nullptr if missing/not succeeded
  bool has_lqip = false;
};

/**
 * For each visible key: exact tile → coarser parent → LQIP flag → empty.
 * Does not enqueue requests. UV is in tile pixel space of the chosen source.
 */
[[nodiscard]] DrawPlan build_draw_plan(BuildDrawPlanInput const& in);

}  // namespace tilelod

#endif
