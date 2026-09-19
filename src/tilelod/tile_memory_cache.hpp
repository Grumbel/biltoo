// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_MEMORY_CACHE_HPP
#define BILTOO_TILELOD_TILE_MEMORY_CACHE_HPP

#include "tilelod/tile_types.hpp"

#include <map>
#include <optional>
#include <vector>

namespace tilelod {

/**
 * Simple RAM map TileKey → entry. No thread safety; host serializes access
 * via TileSession::pump on the frame tick.
 */
class TileMemoryCache {
public:
  CacheEntry const* find(TileKey const& key) const;
  CacheEntry* find_mut(TileKey const& key);

  void set_in_flight(TileKey const& key, std::uint64_t generation);
  void set_succeeded(TileKey const& key, TileBitmap bitmap,
                     std::uint64_t generation);
  void set_failed(TileKey const& key, std::uint64_t generation);

  void erase(TileKey const& key);
  void clear();

  /// Drop Succeeded entries with scale < keep_min_scale (finer than keep).
  void drop_finer_than(int keep_min_scale);

  /// Approximate payload bytes for Succeeded bitmaps.
  std::size_t approx_bytes() const;

  /// Number of Succeeded entries (any scale).
  std::size_t succeeded_count() const;

  bool has_succeeded() const { return succeeded_count() > 0; }

  /**
   * Evict Succeeded tiles not in @p protect until approx_bytes() <= max_bytes.
   * Oldest last_used first. Never drops InFlight/Failed. Returns bytes freed.
   */
  std::size_t trim_to_budget(std::size_t max_bytes,
                             std::vector<TileKey> const& protect);

  void touch(TileKey const& key, std::uint64_t now);

  /// Keys currently InFlight.
  std::vector<TileKey> in_flight_keys() const;

  std::size_t size() const { return m_map.size(); }

  std::map<TileKey, CacheEntry> const& map() const { return m_map; }

  /// Default shared-path budget (~128 MiB of RGBA tiles).
  static constexpr std::size_t kDefaultBudgetBytes = 128ull * 1024ull * 1024ull;

private:
  std::map<TileKey, CacheEntry> m_map;
};

}  // namespace tilelod

#endif
