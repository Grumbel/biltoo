// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_SESSION_HPP
#define BILTOO_TILELOD_TILE_SESSION_HPP

#include "tilelod/draw_plan.hpp"
#include "tilelod/lod_planner.hpp"
#include "tilelod/tile_memory_cache.hpp"
#include "tilelod/tile_source.hpp"
#include "tilelod/tile_types.hpp"

#include <mutex>
#include <optional>
#include <vector>

namespace tilelod {

/**
 * Per-image LOD session: viewport → plan → budgeted requests → draw plan.
 * Qt-free. Host calls pump/issue_requests/draw_plan from the frame path.
 */
class TileSession {
public:
  /**
   * @param source non-owning tile backend
   * @param shared_cache optional shared RAM cache (e.g. per path). When null,
   *        the session owns a private TileMemoryCache.
   */
  explicit TileSession(TileSource* source, TileMemoryCache* shared_cache = nullptr);

  void set_content_size(int width, int height, int min_scale = 0);
  int content_w() const { return m_content_w; }
  int content_h() const { return m_content_h; }
  int min_scale() const { return m_min_scale; }
  int max_scale() const { return m_max_scale; }

  /// Optional LQIP underlay presence (bitmap owned by host; flag only here).
  void set_has_lqip(bool on) { m_has_lqip = on; }
  bool has_lqip() const { return m_has_lqip; }

  void set_viewport(Viewport const& vp, double margin_content = 0.0);

  int target_scale() const { return m_target_scale; }
  std::vector<TileKey> const& visible_keys() const { return m_visible_keys; }
  std::uint64_t generation() const { return m_generation; }

  /**
   * Apply completions queued from worker callbacks.
   * Returns number of entries integrated.
   */
  int pump();

  /**
   * Start up to budget requests for missing exact visible keys.
   * Priority: closer to viewport centre first.
   * Returns number of new requests started.
   */
  int issue_requests(int budget);

  /// Cancel in-flight keys no longer in the visible set.
  void cancel_obsolete();

  DrawPlan draw_plan() const;

  TileMemoryCache const& cache() const { return *m_cache; }
  TileMemoryCache& cache() { return *m_cache; }

  /// True if any Succeeded tile exists in the cache (host may drop LQIP fill).
  bool has_any_succeeded_tile() const;

  /// Test helper: enqueue a completion as if the source called back.
  void inject_completion(TileKey key, std::optional<TileBitmap> bitmap);

private:
  struct PendingCompletion {
    TileKey key;
    std::optional<TileBitmap> bitmap;
    std::uint64_t generation = 0;
  };

  void on_source_completion(TileKey key, std::optional<TileBitmap> bitmap,
                            std::uint64_t gen);

  TileSource* m_source = nullptr;  // non-owning
  int m_content_w = 0;
  int m_content_h = 0;
  int m_min_scale = 0;
  int m_max_scale = 0;
  bool m_has_lqip = false;

  Viewport m_viewport;
  int m_target_scale = 0;
  std::vector<TileKey> m_visible_keys;
  std::uint64_t m_generation = 0;

  TileMemoryCache m_owned_cache;
  TileMemoryCache* m_cache = nullptr;  // → shared or &m_owned_cache

  mutable std::mutex m_pending_mutex;
  std::vector<PendingCompletion> m_pending;
};

}  // namespace tilelod

#endif
