// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_memory_cache.hpp"

namespace tilelod {

CacheEntry const* TileMemoryCache::find(TileKey const& key) const
{
  auto it = m_map.find(key);
  if (it == m_map.end()) {
    return nullptr;
  }
  return &it->second;
}

CacheEntry* TileMemoryCache::find_mut(TileKey const& key)
{
  auto it = m_map.find(key);
  if (it == m_map.end()) {
    return nullptr;
  }
  return &it->second;
}

void TileMemoryCache::set_in_flight(TileKey const& key,
                                    std::uint64_t generation)
{
  CacheEntry& e = m_map[key];
  e.state = TileState::InFlight;
  e.bitmap = {};
  e.generation = generation;
}

void TileMemoryCache::set_succeeded(TileKey const& key, TileBitmap bitmap,
                                    std::uint64_t generation)
{
  CacheEntry& e = m_map[key];
  e.state = TileState::Succeeded;
  e.bitmap = std::move(bitmap);
  e.generation = generation;
}

void TileMemoryCache::set_failed(TileKey const& key, std::uint64_t generation)
{
  CacheEntry& e = m_map[key];
  e.state = TileState::Failed;
  e.bitmap = {};
  e.generation = generation;
}

void TileMemoryCache::erase(TileKey const& key) { m_map.erase(key); }

void TileMemoryCache::clear() { m_map.clear(); }

void TileMemoryCache::drop_finer_than(int keep_min_scale)
{
  for (auto it = m_map.begin(); it != m_map.end();) {
    if (it->second.state == TileState::Succeeded &&
        it->first.scale < keep_min_scale) {
      it = m_map.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<TileKey> TileMemoryCache::in_flight_keys() const
{
  std::vector<TileKey> out;
  for (auto const& [k, e] : m_map) {
    if (e.state == TileState::InFlight) {
      out.push_back(k);
    }
  }
  return out;
}

}  // namespace tilelod
