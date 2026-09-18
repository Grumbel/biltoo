// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_lod_registry.hpp"

#include "thumtoocache.h"

#include <algorithm>
#include <vector>

namespace tilelod {
namespace {

ThumtooTileSource::FetchFn makeFetch(QString path)
{
  return [path](std::string const& /*uri*/,
                std::vector<ThumtooTileSource::TileCoord> const& coords,
                std::function<void(std::size_t, std::optional<TileBitmap>)> on_cell) {
    QVector<ThumtooCache::TileCoord> qcoords;
    qcoords.reserve(static_cast<int>(coords.size()));
    for (auto const& c : coords) {
      qcoords.push_back({c.scale, c.x, c.y});
    }
    // TileBitmap delivered directly (decode once in ThumtooCache::requestTiles).
    ThumtooCache::requestTiles(path, qcoords, on_cell);
  };
}

ThumtooTileSource::CancelFn makeCancel(QString path)
{
  return [path](std::string const& /*uri*/,
                std::vector<ThumtooTileSource::TileCoord> const& coords) {
    // Per-key cancel_obsolete must NOT cancel_uri — that drops *all* queued
    // EnsureTiles for the path and was invoked on every progressive scale step
    // (hundreds of ms–seconds on a busy thumtoo queue). Generation filtering
    // drops late completions; RAM InFlight is erased in TileSession.
    if (!coords.empty()) {
      return;
    }
    (void)ThumtooCache::cancelTilesForPath(path);
  };
}

}  // namespace

TileLodRegistry& TileLodRegistry::instance()
{
  static TileLodRegistry reg;
  return reg;
}

void TileLodRegistry::touch_locked(SharedPathTiles& entry)
{
  entry.last_used = ++m_clock;
}

std::size_t TileLodRegistry::total_approx_bytes_locked() const
{
  std::size_t n = 0;
  for (auto const& [k, shared] : m_by_path) {
    (void)k;
    if (shared && shared->cache) {
      n += shared->cache->approx_bytes();
    }
  }
  return n;
}

void TileLodRegistry::trim_idle_locked()
{
  if (total_approx_bytes_locked() <= m_global_budget) {
    return;
  }
  // Candidates: zero-ref paths, oldest last_used first.
  std::vector<std::pair<std::uint64_t, std::string>> idle;
  idle.reserve(m_by_path.size());
  for (auto const& [key, shared] : m_by_path) {
    if (!shared || shared->refcount > 0) {
      continue;
    }
    idle.emplace_back(shared->last_used, key);
  }
  std::sort(idle.begin(), idle.end(),
            [](auto const& a, auto const& b) { return a.first < b.first; });
  for (auto const& [lu, key] : idle) {
    (void)lu;
    if (total_approx_bytes_locked() <= m_global_budget) {
      break;
    }
    // Dropping the entry destroys ThumtooTileSource (epoch bump) and the
    // TileMemoryCache; in-flight completions become no-ops via session inbox.
    m_by_path.erase(key);
  }
}

std::shared_ptr<SharedPathTiles> TileLodRegistry::acquire(QString const& path)
{
  if (path.isEmpty()) {
    return {};
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it != m_by_path.end()) {
    ++it->second->refcount;
    touch_locked(*it->second);
    return it->second;
  }
  // Make room for a new path before inserting (prefer dropping idle first).
  trim_idle_locked();
  auto shared = std::make_shared<SharedPathTiles>();
  shared->path = path;
  shared->cache = std::make_shared<TileMemoryCache>();
  shared->source = std::make_shared<ThumtooTileSource>(
      std::string{"path"}, makeFetch(path), makeCancel(path));
  shared->refcount = 1;
  touch_locked(*shared);
  m_by_path.emplace(key, shared);
  return shared;
}

void TileLodRegistry::release(QString const& path)
{
  if (path.isEmpty()) {
    return;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end()) {
    return;
  }
  --it->second->refcount;
  if (it->second->refcount < 0) {
    it->second->refcount = 0;
  }
  touch_locked(*it->second);
  if (it->second->refcount > 0) {
    return;
  }
  // Idle: keep Succeeded tiles for A→B→A; drop empty shells immediately.
  bool const has_tiles =
      it->second->cache && it->second->cache->approx_bytes() > 0;
  bool const has_inflight =
      it->second->cache && !it->second->cache->in_flight_keys().empty();
  if (!has_tiles && !has_inflight) {
    m_by_path.erase(it);
    return;
  }
  trim_idle_locked();
}

void TileLodRegistry::invalidate(QString const& path)
{
  if (path.isEmpty()) {
    return;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  m_by_path.erase(key);
}

int TileLodRegistry::path_refcount(QString const& path) const
{
  if (path.isEmpty()) {
    return 0;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end()) {
    return 0;
  }
  return it->second->refcount;
}

std::size_t TileLodRegistry::path_count() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return m_by_path.size();
}

std::size_t TileLodRegistry::total_approx_bytes() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return total_approx_bytes_locked();
}

void TileLodRegistry::set_global_budget_bytes(std::size_t bytes)
{
  std::lock_guard<std::mutex> lock(m_mu);
  m_global_budget = bytes;
  trim_idle_locked();
}

std::size_t TileLodRegistry::global_budget_bytes() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return m_global_budget;
}

}  // namespace tilelod
