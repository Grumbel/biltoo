// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_SESSION_HPP
#define BILTOO_TILELOD_TILE_SESSION_HPP

#include "tilelod/draw_plan.hpp"
#include "tilelod/lod_planner.hpp"
#include "tilelod/tile_memory_cache.hpp"
#include "tilelod/tile_source.hpp"
#include "tilelod/tile_types.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <functional>
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
  ~TileSession();

  TileSession(TileSession const&) = delete;
  TileSession& operator=(TileSession const&) = delete;

  void set_content_size(int width, int height, int min_scale = 0);
  int content_w() const { return m_content_w; }
  int content_h() const { return m_content_h; }
  int min_scale() const { return m_min_scale; }
  int max_scale() const { return m_max_scale; }

  /// Optional LQIP underlay presence (bitmap owned by host; flag only here).
  void set_has_lqip(bool on)
  {
    if (m_has_lqip == on) {
      return;
    }
    m_has_lqip = on;
    m_draw_plan_dirty = true;
  }
  bool has_lqip() const { return m_has_lqip; }

  void set_viewport(Viewport const& vp, double margin_content = 0.0);

  int target_scale() const { return m_target_scale; }
  /** Ideal scale from density before hold damping. */
  int desired_scale() const { return m_desired_scale; }
  bool request_scale_holding() const;
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
  /** True if any Succeeded tile has scale >= @p min_scale (e.g. 1 = non–full-res). */
  bool has_succeeded_scale_ge(int min_scale) const;
  /** True if any Succeeded cell exists at exact pyramid scale. */
  bool has_succeeded_at_scale(int scale) const;

  /** Exact-tile coverage of the current visible key set. */
  struct Coverage {
    int visible = 0;
    int exact_succeeded = 0;
    int in_flight = 0;
    int failed = 0;
    int missing = 0;  ///< no cache entry yet
    bool fully_covered() const
    {
      return visible > 0 && exact_succeeded >= visible && in_flight == 0
             && exact_succeeded > 0;
    }
    /** Every visible key is Succeeded or Failed — no work left this generation. */
    bool settled() const
    {
      return visible > 0 && in_flight == 0 && missing == 0
             && (exact_succeeded + failed) >= visible;
    }
  };
  Coverage coverage() const;

  /** Counts from the last issue_requests() call (BILTOO_TILE_DEBUG). */
  struct IssueDiag {
    int early = 0;       // 0=ran, 1=no source, 2=budget, 3=no content
    int vis = 0;
    int skip_ok = 0;     // already Succeeded/InFlight
    int skip_fail = 0;   // Failed this generation
    int skip_s0gate = 0; // scale0 needs coarser
    int skip_denser = 0; // denser needs exact s=0
    int cand = 0;        // scored for issue
    int batched = 0;     // set_in_flight + handed to source
    int content_w = 0;
    int content_h = 0;
    int target = 0;
    int budget = 0;
    std::uint64_t gen = 0;
    int s0 = 0;
  };

  /** Lightweight snapshot for BILTOO_TILE_DEBUG host logs. */
  struct DebugSnapshot {
    int target_scale = 0;
    int desired_scale = 0;
    int stable_scale = 0;
    int min_scale = 0;
    int max_scale = 0;
    int visible = 0;
    int exact_succeeded = 0;
    int in_flight = 0;
    int failed = 0;
    int missing = 0;
    int cache_succeeded = 0;
    int scale0_ok = 0;  // 1 if any Succeeded cell at exact scale 0
    /// Draw-plan command histogram (visible keys only).
    int plan_exact = 0;
    int plan_parent = 0;
    int plan_underlay = 0;
    /// Visible keys with no Exact/Coarser/Underlay command (hole).
    int plan_empty = 0;
    bool has_lqip = false;
    bool holding = false;
    bool reached_desired = false;
    std::uint64_t generation = 0;
    IssueDiag issue;
  };
  DebugSnapshot debug_snapshot() const;
  IssueDiag const& last_issue_diag() const { return m_last_issue; }


  void set_byte_budget(std::size_t bytes) { m_byte_budget = bytes; }

  /**
   * Called from any thread when a tile completion is queued in the inbox.
   * Host should queue a GUI-thread pump+repaint (tiles never appear without it).
   */
  void set_wake(std::function<void()> wake);
  std::size_t byte_budget() const { return m_byte_budget; }

  /// Test helper: enqueue a completion as if the source called back.
  void inject_completion(TileKey key, std::optional<TileBitmap> bitmap);

private:
  struct PendingCompletion {
    TileKey key;
    std::optional<TileBitmap> bitmap;
    std::uint64_t generation = 0;
  };

  /**
   * Shared with source callbacks so completions remain safe after the
   * TileSession object is destroyed (cancel + drop).
   */
  struct CompletionInbox {
    std::mutex mu;
    std::vector<PendingCompletion> pending;
    std::atomic<bool> alive{true};
    std::function<void()> wake;
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
  int m_desired_scale = 0;
  std::vector<TileKey> m_visible_keys;
  std::uint64_t m_generation = 0;
  IssueDiag m_last_issue{};

  // Debounce adjacent scale steps during continuous zoom (Galapix lesson).
  bool m_have_stable_scale = false;
  bool m_reached_desired = false;  // false until cold climb hits desired
  int m_stable_scale = 0;
  int m_pending_scale = 0;
  std::chrono::steady_clock::time_point m_pending_since{};
  static constexpr std::chrono::milliseconds kScaleHold{150};

  int stable_request_scale(int desired_scale);
  /** True when every visible key is Succeeded or Failed (not InFlight/missing). */
  bool visible_keys_settled() const;
  /** Step held scale toward desired once visible keys at held scale settled. */
  bool advance_progressive_scale();
  /** When every denser (scale<0) visible cell Failed, raise min_scale. */
  bool backoff_failed_denser();

  TileMemoryCache m_owned_cache;
  TileMemoryCache* m_cache = nullptr;  // → shared or &m_owned_cache
  std::size_t m_byte_budget = TileMemoryCache::kDefaultBudgetBytes;

  std::shared_ptr<CompletionInbox> m_inbox;

  // Paint calls draw_plan every frame; rebuild only when viewport gen or
  // Succeeded set changes (pump / set_viewport).
  mutable bool m_draw_plan_dirty = true;
  mutable DrawPlan m_draw_plan_cache;
};

}  // namespace tilelod

#endif
