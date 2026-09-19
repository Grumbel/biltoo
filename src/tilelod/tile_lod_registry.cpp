// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_lod_registry.hpp"

#include "thumtoocache.h"

#include <QString>
#include <QByteArray>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
  auto idle_count = [this]() {
    std::size_t n = 0;
    for (auto const& [key, shared] : m_by_path) {
      (void)key;
      if (shared && shared->refcount == 0) {
        ++n;
      }
    }
    return n;
  };
  const bool over_bytes = total_approx_bytes_locked() > m_global_budget;
  const bool over_count = idle_count() > m_max_idle_paths;
  if (!over_bytes && !over_count) {
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
    const bool still_over_bytes =
        total_approx_bytes_locked() > m_global_budget;
    const bool still_over_count = idle_count() > m_max_idle_paths;
    if (!still_over_bytes && !still_over_count) {
      break;
    }
    // Dropping the entry destroys ThumtooTileSource (epoch bump) and the
    // TileMemoryCache; in-flight completions become no-ops via session inbox.
    if (const char* td = std::getenv("BILTOO_TILE_DEBUG");
        td && td[0] && td[0] != '0') {
      std::fprintf(stderr, "biltoo/tile-reg: trim idle path=%s\n", key.c_str());
      std::fflush(stderr);
    }
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
  auto it = m_by_path.find(key);
  if (it == m_by_path.end()) {
    return;
  }
  // Cancel in-flight fetches and clear Succeeded tiles in place so every
  // live controller sharing this SharedPathTiles stops painting stale cells.
  // Erase only when idle; active holders keep the empty shell until release.
  if (it->second) {
    if (it->second->source) {
      it->second->source->cancel_all();
    }
    if (it->second->cache) {
      it->second->cache->clear();
    }
    if (it->second->refcount == 0) {
      m_by_path.erase(it);
    }
  } else {
    m_by_path.erase(it);
  }
}

void TileLodRegistry::invalidateAll()
{
  std::lock_guard<std::mutex> lock(m_mu);
  // Cancel + clear every entry first so any remaining holders (stashed items)
  // cannot paint Succeeded tiles from the previous session after the map drop.
  for (auto& [key, shared] : m_by_path) {
    (void)key;
    if (!shared) {
      continue;
    }
    if (shared->source) {
      shared->source->cancel_all();
    }
    if (shared->cache) {
      shared->cache->clear();
    }
  }
  m_by_path.clear();
}

bool TileLodRegistry::has_succeeded_tiles(QString const& path) const
{
  return path_succeeded_count(path) > 0;
}

std::size_t TileLodRegistry::path_succeeded_count(QString const& path) const
{
  if (path.isEmpty()) {
    return 0;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end() || !it->second || !it->second->cache) {
    return 0;
  }
  return it->second->cache->succeeded_count();
}

std::size_t TileLodRegistry::idle_path_count() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  std::size_t n = 0;
  for (auto const& [k, shared] : m_by_path) {
    (void)k;
    if (shared && shared->refcount == 0) {
      ++n;
    }
  }
  return n;
}

std::size_t TileLodRegistry::path_approx_bytes(QString const& path) const
{
  if (path.isEmpty()) {
    return 0;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end() || !it->second || !it->second->cache) {
    return 0;
  }
  return it->second->cache->approx_bytes();
}

void TileLodRegistry::touch(QString const& path)
{
  if (path.isEmpty()) {
    return;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end() || !it->second) {
    return;
  }
  touch_locked(*it->second);
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
  m_global_budget = bytes > 0 ? bytes : kDefaultGlobalBudgetBytes;
  trim_idle_locked();
}

std::size_t TileLodRegistry::global_budget_bytes() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return m_global_budget;
}

void TileLodRegistry::set_max_idle_paths(std::size_t n)
{
  std::lock_guard<std::mutex> lock(m_mu);
  m_max_idle_paths = n > 0 ? n : kDefaultMaxIdlePaths;
  trim_idle_locked();
}

std::size_t TileLodRegistry::max_idle_paths() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return m_max_idle_paths;
}

QString TileLodRegistry::debug_summary() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  std::size_t idle = 0;
  for (auto const& [k, shared] : m_by_path) {
    (void)k;
    if (shared && shared->refcount == 0) {
      ++idle;
    }
  }
  const double mib =
      static_cast<double>(total_approx_bytes_locked()) / (1024.0 * 1024.0);
  return QStringLiteral("regPaths=%1 idle=%2 ramMiB=%3 maxIdle=%4")
      .arg(m_by_path.size())
      .arg(idle)
      .arg(mib, 0, 'f', 1)
      .arg(m_max_idle_paths);
}

void TileLodRegistry::apply_environment_overrides()
{
  bool changed = false;
  if (const char* e = std::getenv("BILTOO_TILE_RAM_MIB");
      e && e[0]) {
    char* end = nullptr;
    const long mib = std::strtol(e, &end, 10);
    if (end != e && mib > 0 && mib < 1024 * 1024) {
      set_global_budget_bytes(static_cast<std::size_t>(mib) * 1024ull * 1024ull);
      changed = true;
    }
  }
  if (const char* e = std::getenv("BILTOO_TILE_MAX_IDLE");
      e && e[0]) {
    char* end = nullptr;
    const long n = std::strtol(e, &end, 10);
    if (end != e && n > 0 && n < 100000) {
      set_max_idle_paths(static_cast<std::size_t>(n));
      changed = true;
    }
  }
  if (changed) {
    if (const char* td = std::getenv("BILTOO_TILE_DEBUG");
        td && td[0] && td[0] != '0') {
      const QByteArray line = debug_summary().toUtf8();
      std::fprintf(stderr, "biltoo/tile-reg: env %s\n", line.constData());
      std::fflush(stderr);
    }
  }
}

}  // namespace tilelod
