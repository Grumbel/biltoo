// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_LOD_REGISTRY_HPP
#define BILTOO_TILELOD_TILE_LOD_REGISTRY_HPP

#include "tilelod/tile_memory_cache.hpp"
#include "tilelod/thumtoo_tile_source.hpp"

#include <QString>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace tilelod {

/**
 * Shared tile backend + RAM cache for one session path.
 * Multiple ImageItems (Workspace duplicates) share Succeeded tiles.
 * Each item keeps its own TileSession (viewport / request budget).
 */
struct SharedPathTiles {
  std::shared_ptr<ThumtooTileSource> source;
  std::shared_ptr<TileMemoryCache> cache;
  QString path;
  int refcount = 0;
};

/**
 * Process-wide registry keyed by path string.
 * GUI-thread use only (ImageItem / TileLodController).
 */
class TileLodRegistry {
public:
  static TileLodRegistry& instance();

  /** Acquire or create shared state for path. Caller must release(). */
  std::shared_ptr<SharedPathTiles> acquire(QString const& path);

  /** Drop one reference; destroys entry when count hits zero. */
  void release(QString const& path);

  int path_refcount(QString const& path) const;

private:
  TileLodRegistry() = default;

  mutable std::mutex m_mu;
  std::unordered_map<std::string, std::shared_ptr<SharedPathTiles>> m_by_path;
};

}  // namespace tilelod

#endif
