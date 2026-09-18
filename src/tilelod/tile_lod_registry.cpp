// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_lod_registry.hpp"

#include "thumtoocache.h"

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
                std::vector<ThumtooTileSource::TileCoord> const& /*coords*/) {
    // thumtoo cancel_uri drops all queued EnsureTiles for the path (not
    // per-coord). Good enough for pan/zoom obsolete work.
    (void)ThumtooCache::cancelTilesForPath(path);
  };
}

}  // namespace

TileLodRegistry& TileLodRegistry::instance()
{
  static TileLodRegistry reg;
  return reg;
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
    return it->second;
  }
  auto shared = std::make_shared<SharedPathTiles>();
  shared->path = path;
  shared->cache = std::make_shared<TileMemoryCache>();
  shared->source = std::make_shared<ThumtooTileSource>(
      std::string{"path"}, makeFetch(path), makeCancel(path));
  shared->refcount = 1;
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
  if (it->second->refcount <= 0) {
    m_by_path.erase(it);
  }
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

}  // namespace tilelod
