// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_memory_cache.hpp"

#include <algorithm>
#include <set>

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
  e.last_used = generation;
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

std::size_t TileMemoryCache::approx_bytes() const
{
  std::size_t n = 0;
  for (auto const& [k, e] : m_map) {
    (void)k;
    if (e.state == TileState::Succeeded) {
      n += e.bitmap.bytes.size();
    }
  }
  return n;
}

std::size_t TileMemoryCache::succeeded_count() const
{
  std::size_t n = 0;
  for (auto const& [k, e] : m_map) {
    (void)k;
    if (e.state == TileState::Succeeded && e.bitmap.valid()) {
      ++n;
    }
  }
  return n;
}


void TileMemoryCache::touch(TileKey const& key, std::uint64_t now)
{
  auto it = m_map.find(key);
  if (it != m_map.end() && it->second.state == TileState::Succeeded) {
    it->second.last_used = now;
  }
}

std::size_t TileMemoryCache::trim_to_budget(std::size_t max_bytes,
                                           std::vector<TileKey> const& protect)
{
  if (approx_bytes() <= max_bytes) {
    return 0;
  }
  std::set<TileKey> keep(protect.begin(), protect.end());
  // Candidates: Succeeded not protected, sorted by last_used ascending.
  std::vector<std::pair<std::uint64_t, TileKey>> victims;
  for (auto const& [k, e] : m_map) {
    if (e.state != TileState::Succeeded) {
      continue;
    }
    if (keep.find(k) != keep.end()) {
      continue;
    }
    victims.emplace_back(e.last_used, k);
  }
  std::sort(victims.begin(), victims.end(),
            [](auto const& a, auto const& b) { return a.first < b.first; });
  std::size_t freed = 0;
  for (auto const& [lu, key] : victims) {
    (void)lu;
    if (approx_bytes() <= max_bytes) {
      break;
    }
    auto it = m_map.find(key);
    if (it == m_map.end()) {
      continue;
    }
    freed += it->second.bitmap.bytes.size();
    m_map.erase(it);
  }
  return freed;
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
