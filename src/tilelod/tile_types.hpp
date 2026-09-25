// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_TYPES_HPP
#define BILTOO_TILELOD_TILE_TYPES_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tilelod {

/// Matches thumtoo::kTileSize / Galapix tile grid.
inline constexpr int kTileSize = 256;
/// Matches thumtoo::kTileOverlap — right/bottom shared strip in the bitmap; host paints exclusive src→dest.
inline constexpr int kTileOverlap = 1;

/// Grid cell identity at a pyramid scale.
struct TileKey {
  int scale = 0;  ///< 0 = full resolution; +1 halves linear size
  int x = 0;
  int y = 0;

  bool operator==(TileKey const& o) const noexcept
  {
    return scale == o.scale && x == o.x && y == o.y;
  }
  bool operator!=(TileKey const& o) const noexcept { return !(*this == o); }
  bool operator<(TileKey const& o) const noexcept
  {
    if (scale != o.scale) {
      return scale < o.scale;
    }
    if (x != o.x) {
      return x < o.x;
    }
    return y < o.y;
  }
};

/// Axis-aligned rectangle in continuous space (content or UV).
struct RectF {
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;

  double right() const noexcept { return x + w; }
  double bottom() const noexcept { return y + h; }

  bool empty() const noexcept { return w <= 0 || h <= 0; }

  bool intersects(RectF const& o) const noexcept
  {
    return x < o.right() && right() > o.x && y < o.bottom() && bottom() > o.y;
  }

  RectF intersection(RectF const& o) const noexcept
  {
    double const nx = x > o.x ? x : o.x;
    double const ny = y > o.y ? y : o.y;
    double const nr = right() < o.right() ? right() : o.right();
    double const nb = bottom() < o.bottom() ? bottom() : o.bottom();
    if (nr <= nx || nb <= ny) {
      return {};
    }
    return {nx, ny, nr - nx, nb - ny};
  }
};

/// Integer content-space rect (pixel-aligned content coordinates).
struct RectI {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  int right() const noexcept { return x + w; }
  int bottom() const noexcept { return y + h; }
  bool empty() const noexcept { return w <= 0 || h <= 0; }
};

/// Viewport in content space plus screen density.
struct Viewport {
  RectF content_rect;  ///< Visible region in scale-0 content pixels
  /// Device (screen) pixels per content pixel along the longer mapped axis.
  /// 1.0 = 1:1; 2.0 = zoomed in 2×; 0.5 = zoomed out.
  double device_per_content = 1.0;
};

/// Owning pixel buffer for one tile (decoded RGBA8 or encoded payload).
struct TileBitmap {
  int width = 0;
  int height = 0;
  /// RGBA8 row-major when codec empty or "rgba8"; otherwise opaque bytes.
  std::string codec;  ///< e.g. "" / "rgba8" / "jpeg"
  std::vector<std::uint8_t> bytes;

  bool valid() const noexcept
  {
    return width > 0 && height > 0 && !bytes.empty();
  }
};

enum class TileState {
  Missing,
  InFlight,
  Succeeded,
  Failed
};

struct CacheEntry {
  TileState state = TileState::Missing;
  TileBitmap bitmap;
  std::uint64_t generation = 0;  ///< Viewport generation when requested
  std::uint64_t last_used = 0;    ///< For budget eviction (monotonic touch)
};

enum class DrawKind {
  ExactTile,     ///< Cache hit at target scale
  CoarserTile,   ///< Parent stand-in
  Underlay,      ///< LQIP / overview (host paints separately if needed)
  Empty          ///< Nothing available
};

/// One paint operation in content space.
struct DrawCommand {
  RectF dst_content;   ///< Where to place in content coordinates
  TileKey src_key;     ///< Tile that supplies pixels (if any)
  RectF src_uv;        ///< Sub-rect in tile pixel space [0,width]×[0,height]
  DrawKind kind = DrawKind::Empty;
  /// True when kind is Underlay and host should use session LQIP for this rect.
  bool use_lqip = false;
};

struct DrawPlan {
  std::vector<DrawCommand> commands;
  int target_scale = 0;
  bool any_tile = false;  ///< At least one ExactTile or CoarserTile
};

}  // namespace tilelod

#endif
