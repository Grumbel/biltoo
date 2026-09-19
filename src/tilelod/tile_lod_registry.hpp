// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_LOD_REGISTRY_HPP
#define BILTOO_TILELOD_TILE_LOD_REGISTRY_HPP

#include "tilelod/tile_memory_cache.hpp"
#include "tilelod/thumtoo_tile_source.hpp"

#include <QString>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace tilelod {

/**
 * Shared tile backend + RAM cache for one session path.
 * Multiple ImageItems (Workspace duplicates) share Succeeded tiles.
 * Each item keeps its own TileSession (viewport / request budget).
 *
 * Lifetime is **not** tied to active controllers: when the last
 * TileLodController releases a path, the entry stays in the process-wide
 * registry until global LRU eviction (or explicit invalidate). Image ←/→
 * and mode switches therefore keep Succeeded tiles for paths still inside
 * the budget.
 */
struct SharedPathTiles {
  std::shared_ptr<ThumtooTileSource> source;
  std::shared_ptr<TileMemoryCache> cache;
  QString path;
  /** Controllers currently bound to this path (acquire/release). */
  int refcount = 0;
  /** Monotonic clock for zero-ref LRU eviction across paths. */
  std::uint64_t last_used = 0;
};

/**
 * Process-wide registry keyed by path string.
 * GUI-thread use only (ImageItem / TileLodController).
 *
 * Ownership model:
 * - Pixels live here (path-keyed), not on ImageItem.
 * - TileSession stays per view surface (cheap create/destroy on ←/→).
 * - release() drops interest only; cache survives until trim or invalidate.
 */
class TileLodRegistry {
public:
  static TileLodRegistry& instance();

  /**
   * Acquire or create shared state for path. Caller must release() once.
   * Re-acquiring a zero-ref retained path returns the same cache (tiles intact).
   */
  std::shared_ptr<SharedPathTiles> acquire(QString const& path);

  /**
   * Drop one reference. Does **not** destroy the path entry when count hits
   * zero; idle paths are retained for LRU reuse until global budget trim.
   * Empty idle entries (no Succeeded tiles) are dropped immediately. Residual
   * InFlight keys are cleared on the last release.
   */
  void release(QString const& path);

  /**
   * Force-remove a path entry regardless of refcount (e.g. file replaced).
   * Cancels outstanding tile work via source destruction.
   */
  void invalidate(QString const& path);

  /**
   * Drop every path entry (session replace / new archive). Live controllers
   * must re-acquire; retained idle tiles from the previous session must not
   * paint under a new path list.
   */
  void invalidateAll();

  int path_refcount(QString const& path) const;

  /**
   * True if the path entry exists and holds at least one Succeeded tile.
   * Does not acquire (refcount unchanged). Used to skip redundant prefetch
   * when A→B→A retained tiles are already warm.
   */
  bool has_succeeded_tiles(QString const& path) const;

  /** Succeeded tile count for one path (0 if absent). O(1) per path cache. */
  std::size_t path_succeeded_count(QString const& path) const;

  /** Succeeded payload bytes for one path (0 if absent). */
  std::size_t path_approx_bytes(QString const& path) const;

  /** Paths with refcount == 0 still retained for LRU. */
  std::size_t idle_path_count() const;

  /**
   * Bump LRU clock for an existing path without changing refcount.
   * No-op if the path is not in the registry.
   */
  void touch(QString const& path);

  /** Number of path entries currently in the registry (active + idle retained). */
  std::size_t path_count() const;

  /** Sum of Succeeded tile payload bytes across all path caches. */
  std::size_t total_approx_bytes() const;

  /**
   * Cap on total Succeeded tile RAM retained process-wide (active + idle).
   * Default ~384 MiB. When over budget, oldest zero-ref path entries are
   * dropped whole; active paths rely on per-session trim_to_budget.
   */
  void set_global_budget_bytes(std::size_t bytes);
  std::size_t global_budget_bytes() const;

  /**
   * Cap on zero-ref path entries retained for LRU (in addition to the byte
   * budget). Long Image ←/→ sessions otherwise accumulate many small overview
   * caches under the byte cap. Default 64.
   */
  void set_max_idle_paths(std::size_t n);
  std::size_t max_idle_paths() const;

  static constexpr std::size_t kDefaultGlobalBudgetBytes =
      384ull * 1024ull * 1024ull;
  static constexpr std::size_t kDefaultMaxIdlePaths = 64;
  /** Overview-warm threshold shared with neighbor prefetch skip. */
  static constexpr std::size_t kWarmSucceededMin = 4;

  /** One-line summary for BILTOO_TILE_DEBUG (paths/idle/MiB/caps). */
  QString debug_summary() const;

  /**
   * Apply BILTOO_TILE_RAM_MIB and BILTOO_TILE_MAX_IDLE from the environment
   * (no-op when unset). Safe to call once at process start.
   */
  void apply_environment_overrides();

private:
  TileLodRegistry() = default;

  void touch_locked(SharedPathTiles& entry);
  std::size_t total_approx_bytes_locked() const;
  /** Drop zero-ref paths (oldest last_used first) until under byte + count caps. */
  void trim_idle_locked();

  mutable std::mutex m_mu;
  std::unordered_map<std::string, std::shared_ptr<SharedPathTiles>> m_by_path;
  std::uint64_t m_clock = 0;
  std::size_t m_global_budget = kDefaultGlobalBudgetBytes;
  std::size_t m_max_idle_paths = kDefaultMaxIdlePaths;
};

}  // namespace tilelod

#endif
