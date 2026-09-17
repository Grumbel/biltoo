// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_LOD_MATH_HPP
#define BILTOO_TILELOD_LOD_MATH_HPP

#include "tilelod/tile_types.hpp"

#include <cmath>
#include <utility>

namespace tilelod {

/// Dimension at pyramid scale: successive integer floor-half (thumtoo/Galapix).
[[nodiscard]] inline int dim_at_tile_scale(int n, int scale) noexcept
{
  if (n <= 0) {
    return 0;
  }
  if (scale <= 0) {
    return n;
  }
  for (int i = 0; i < scale; ++i) {
    n /= 2;
    if (n <= 0) {
      return 0;
    }
  }
  return n;
}

/// Number of tile columns/rows covering the image at scale s.
[[nodiscard]] inline int tiles_across(int content_dim, int scale) noexcept
{
  int const d = dim_at_tile_scale(content_dim, scale);
  if (d <= 0) {
    return 0;
  }
  return (d + kTileSize - 1) / kTileSize;
}

/// Smallest scale where the image fits in a single tile (or 0 if already).
[[nodiscard]] inline int max_scale_for_size(int width, int height,
                                            int scale_cap = 32) noexcept
{
  if (width <= 0 || height <= 0) {
    return 0;
  }
  int s = 0;
  while (s < scale_cap) {
    if (tiles_across(width, s) <= 1 && tiles_across(height, s) <= 1) {
      return s;
    }
    ++s;
  }
  return scale_cap;
}

/// Content-space rectangle covered by tile (s, x, y).
[[nodiscard]] inline RectI tile_content_rect(int content_w, int content_h,
                                             TileKey const& key) noexcept
{
  int const factor = 1 << (key.scale > 0 ? key.scale : 0);
  // For negative scales (future PDF), treat as finer than 0 — not used yet.
  int const step = (key.scale >= 0) ? (kTileSize * factor) : kTileSize;
  int const left = key.x * step;
  int const top = key.y * step;
  int const right = left + step;
  int const bottom = top + step;
  int const x0 = left < 0 ? 0 : left;
  int const y0 = top < 0 ? 0 : top;
  int const x1 = right > content_w ? content_w : right;
  int const y1 = bottom > content_h ? content_h : bottom;
  if (x1 <= x0 || y1 <= y0) {
    return {};
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

/// Parent cell at coarser scale (key.scale + delta), delta >= 1.
[[nodiscard]] inline TileKey parent_key(TileKey const& key, int delta) noexcept
{
  if (delta <= 0) {
    return key;
  }
  int const div = 1 << delta;
  return {key.scale + delta, key.x / div, key.y / div};
}

/// Integer power of two: 2^n for n in [0, 30].
[[nodiscard]] inline int pow2i(int n) noexcept
{
  if (n <= 0) {
    return 1;
  }
  if (n >= 30) {
    return 1 << 30;
  }
  return 1 << n;
}

/**
 * Target pyramid scale for a given device density.
 *
 * device_per_content: screen pixels per content pixel (1 = 1:1).
 * We want tile-space pixels per content pixel ≈ 2^{-scale} to be at least
 * device_per_content, so scale ≲ -log2(device_per_content).
 * Finest integer scale meeting density, then clamped.
 */
[[nodiscard]] inline int target_scale_for_density(double device_per_content,
                                                   int min_scale,
                                                   int max_scale) noexcept
{
  if (max_scale < min_scale) {
    return min_scale;
  }
  if (!(device_per_content > 0.0) || !std::isfinite(device_per_content)) {
    // Unknown / invalid → coarsest safe overview
    return max_scale;
  }
  // scale = floor(-log2(need)); need=1 → 0; need=2 → -1 (finer, clamp to min);
  // need=0.5 → 1 (coarser).
  double const ideal = -std::log2(device_per_content);
  int s = static_cast<int>(std::floor(ideal + 1e-9));
  if (s < min_scale) {
    s = min_scale;
  }
  if (s > max_scale) {
    s = max_scale;
  }
  return s;
}

/**
 * UV sub-rect inside a coarser parent tile that covers the fine key's region.
 *
 * Parent tile pixel space is [0, parent_w] × [0, parent_h] (actual payload size,
 * often 256×256 except edges). Fine key is at scale parent.scale - delta.
 */
[[nodiscard]] inline RectF parent_uv_for_child(TileKey const& fine,
                                               TileKey const& parent,
                                               int parent_pixel_w,
                                               int parent_pixel_h) noexcept
{
  int const delta = parent.scale - fine.scale;
  if (delta <= 0 || parent_pixel_w <= 0 || parent_pixel_h <= 0) {
    return {0, 0, static_cast<double>(parent_pixel_w),
            static_cast<double>(parent_pixel_h)};
  }
  int const span = pow2i(delta);  // fine cells per parent cell axis
  int const local_x = fine.x - parent.x * span;
  int const local_y = fine.y - parent.y * span;
  // Map local fine cell into parent pixel space (parent covers span×span fine cells).
  double const cell_w = static_cast<double>(parent_pixel_w) / static_cast<double>(span);
  double const cell_h = static_cast<double>(parent_pixel_h) / static_cast<double>(span);
  return {local_x * cell_w, local_y * cell_h, cell_w, cell_h};
}

}  // namespace tilelod

#endif
