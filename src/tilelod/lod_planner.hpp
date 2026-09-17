// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_LOD_PLANNER_HPP
#define BILTOO_TILELOD_LOD_PLANNER_HPP

#include "tilelod/lod_math.hpp"
#include "tilelod/tile_types.hpp"

#include <vector>

namespace tilelod {

struct PlannerInput {
  int content_w = 0;
  int content_h = 0;
  int min_scale = 0;
  int max_scale = 0;  ///< Inclusive; typically max_scale_for_size(...)
  Viewport viewport;
  /// Extra content-space margin expanded around the viewport before cell test.
  double margin_content = 0.0;
};

struct PlannerOutput {
  int target_scale = 0;
  std::vector<TileKey> visible_keys;
};

/**
 * Pure: viewport + content size → target scale and intersecting tile keys.
 * No I/O, no cache.
 */
[[nodiscard]] PlannerOutput plan_visible_tiles(PlannerInput const& in);

}  // namespace tilelod

#endif
