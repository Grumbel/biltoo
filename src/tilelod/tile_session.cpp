// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_session.hpp"

#include "tilelod/lod_math.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace tilelod {

TileSession::TileSession(TileSource* source, TileMemoryCache* shared_cache)
    : m_source(source)
    , m_cache(shared_cache ? shared_cache : &m_owned_cache)
    , m_inbox(std::make_shared<CompletionInbox>())
{
}

TileSession::~TileSession()
{
  if (m_inbox) {
    m_inbox->alive.store(false);
  }
  if (m_source && m_cache) {
    std::vector<TileKey> inflight = m_cache->in_flight_keys();
    if (!inflight.empty()) {
      m_source->cancel(inflight);
      // Shared caches: drop InFlight so peer sessions of the same path can
      // re-request. Completions for this session are already dropped (inbox).
      // Succeeded tiles are left intact.
      for (TileKey const& key : inflight) {
        CacheEntry const* e = m_cache->find(key);
        if (e && e->state == TileState::InFlight) {
          m_cache->erase(key);
        }
      }
    }
  }
  if (m_inbox) {
    std::lock_guard<std::mutex> lock(m_inbox->mu);
    m_inbox->pending.clear();
  }
}

void TileSession::set_content_size(int width, int height, int min_scale)
{
  // prepareTileLod calls this every paint/tick with the same native size.
  // Always resetting cleared visible_keys and bumped generation, so
  // set_viewport always saw a plan change and failed no-spam never held.
  if (m_content_w == width && m_content_h == height && m_min_scale == min_scale) {
    return;
  }

  // Durable memo often arrives *after* open (min_scale 0 → N). Raising the
  // floor must not wipe progressive climb / Succeeded cache state.
  // Lowering the floor (Image mode min_scale 0 after a Gallery durable floor)
  // must re-open progressive climb so density can target finer scales.
  if (m_content_w == width && m_content_h == height && min_scale != m_min_scale) {
    int const old_min = m_min_scale;
    m_min_scale = min_scale;
    m_max_scale = max_scale_for_size(width, height);
    if (m_max_scale < m_min_scale) {
      m_max_scale = m_min_scale;
    }
    if (min_scale < old_min) {
      // Floor lowered: allow climb below previous durable floor.
      m_reached_desired = false;
      m_have_stable_scale = false;
      m_visible_keys.clear();
      ++m_generation;
      m_draw_plan_dirty = true;
      return;
    }
    if (m_stable_scale < m_min_scale) {
      m_stable_scale = m_min_scale;
    }
    if (m_desired_scale < m_min_scale) {
      m_desired_scale = m_min_scale;
    }
    if (m_target_scale < m_min_scale) {
      m_target_scale = m_min_scale;
      m_visible_keys.clear();
      ++m_generation;
      m_draw_plan_dirty = true;
    }
    return;
  }

  m_content_w = width;
  m_content_h = height;
  m_min_scale = min_scale;
  m_max_scale = max_scale_for_size(width, height);
  if (m_max_scale < m_min_scale) {
    m_max_scale = m_min_scale;
  }
  ++m_generation;
  m_visible_keys.clear();
  m_target_scale = m_max_scale;
  m_desired_scale = m_max_scale;
  m_have_stable_scale = false;
  m_reached_desired = false;
  m_draw_plan_dirty = true;
}


int TileSession::stable_request_scale(int desired_scale)
{
  using clock = std::chrono::steady_clock;
  if (desired_scale < m_min_scale) {
    desired_scale = m_min_scale;
  }
  if (desired_scale > m_max_scale) {
    desired_scale = m_max_scale;
  }
  m_desired_scale = desired_scale;

  if (!m_have_stable_scale) {
    m_have_stable_scale = true;
    // Warm shared cache: density target immediately.
    // Cold: at most *one* coarser step than desired — full max_scale walks
    // left Gallery stuck on LQIP for many coordinator ticks (2 cells/tick).
    if (has_any_succeeded_tile()) {
      m_stable_scale = desired_scale;
      m_reached_desired = true;
    } else if (m_max_scale > desired_scale) {
      m_stable_scale = desired_scale + 1;
      m_reached_desired = false;
    } else {
      m_stable_scale = desired_scale;
      m_reached_desired = true;
    }
    m_pending_scale = desired_scale;
    m_pending_since = clock::now();
    return m_stable_scale;
  }

  // Zoom out (coarser): commit immediately. Leaves progressive mode.
  if (desired_scale > m_stable_scale) {
    m_stable_scale = desired_scale;
    m_reached_desired = true;
    m_pending_scale = desired_scale;
    m_pending_since = clock::now();
    return m_stable_scale;
  }

  // Cold progressive climb toward desired — only until first arrival at desired.
  // Must not run after zoom-out/in or adjacent hold is destroyed.
  if (!m_reached_desired && m_stable_scale > desired_scale) {
    m_pending_scale = desired_scale;
    if (visible_keys_settled()) {
      m_stable_scale = m_stable_scale - 1;
      m_pending_since = clock::now();
    }
    if (m_stable_scale <= desired_scale) {
      m_stable_scale = desired_scale;
      m_reached_desired = true;
    }
    return m_stable_scale;
  }

  if (desired_scale == m_stable_scale) {
    m_reached_desired = true;
    m_pending_scale = desired_scale;
    return m_stable_scale;
  }

  // Steady state: adjacent zoom-in debounce (Galapix lesson).
  int const delta = m_stable_scale - desired_scale;
  if (delta > 1) {
    m_stable_scale = desired_scale;
    m_reached_desired = true;
    m_pending_scale = desired_scale;
    m_pending_since = clock::now();
    return m_stable_scale;
  }
  if (desired_scale != m_pending_scale) {
    m_pending_scale = desired_scale;
    m_pending_since = clock::now();
    return m_stable_scale;
  }
  if (clock::now() - m_pending_since >= kScaleHold) {
    m_stable_scale = desired_scale;
    m_reached_desired = true;
    return m_stable_scale;
  }
  return m_stable_scale;
}

bool TileSession::request_scale_holding() const
{
  return m_have_stable_scale && m_desired_scale != m_stable_scale;
}

bool TileSession::visible_keys_settled() const
{
  // No plan yet: stay on current scale until set_viewport builds keys.
  if (m_visible_keys.empty()) {
    return false;
  }
  bool any_success = false;
  for (TileKey const& key : m_visible_keys) {
    CacheEntry const* e = m_cache->find(key);
    if (!e) {
      return false;  // not requested yet
    }
    if (e->state == TileState::InFlight) {
      return false;
    }
    if (e->state == TileState::Succeeded && e->bitmap.valid()) {
      any_success = true;
    }
  }
  // Require at least one success — all-Failed must not climb (spam finer scales).
  return any_success;
}

bool TileSession::advance_progressive_scale()
{
  if (!m_have_stable_scale || m_content_w <= 0) {
    return false;
  }
  // Only during cold progressive climb — not after zoom settle.
  if (m_reached_desired || m_stable_scale <= m_desired_scale) {
    return false;
  }
  if (!visible_keys_settled()) {
    return false;
  }
  int const prev = m_stable_scale;
  m_stable_scale = m_stable_scale - 1;
  if (m_stable_scale <= m_desired_scale) {
    m_stable_scale = m_desired_scale;
    m_reached_desired = true;
  }

  // Re-plan at the new held scale with the current viewport.
  PlannerInput in;
  in.content_w = m_content_w;
  in.content_h = m_content_h;
  in.min_scale = m_stable_scale;
  in.max_scale = m_stable_scale;
  in.viewport = m_viewport;
  in.margin_content = 0;
  PlannerOutput const out = plan_visible_tiles(in);
  bool const plan_changed =
      out.target_scale != m_target_scale || out.visible_keys != m_visible_keys;
  if (plan_changed) {
    ++m_generation;
  }
  m_target_scale = out.target_scale;
  m_visible_keys = out.visible_keys;
  if (plan_changed) {
    cancel_obsolete();
    m_draw_plan_dirty = true;
  }
  return m_stable_scale != prev;
}

void TileSession::set_viewport(Viewport const& vp, double margin_content)
{
  m_viewport = vp;

  PlannerInput in;
  in.content_w = m_content_w;
  in.content_h = m_content_h;
  in.min_scale = m_min_scale;
  in.max_scale = m_max_scale;
  in.viewport = vp;
  in.margin_content = margin_content;

  // Desired scale from density, then hold adjacent steps during continuous zoom.
  PlannerOutput const ideal = plan_visible_tiles(in);
  int const prev_desired = m_desired_scale;
  m_desired_scale = ideal.target_scale;
  int const held = stable_request_scale(m_desired_scale);
  in.viewport = vp;
  // Re-plan visible keys at the held request scale (not every intermediate).
  if (held != ideal.target_scale) {
    // Force planner to use held scale by adjusting density so target matches held.
    // denser → lower scale. We instead set max=min=held via temporary clamp.
    in.min_scale = held;
    in.max_scale = held;
  }
  PlannerOutput const out = plan_visible_tiles(in);
  int const prev_target = m_target_scale;

  // Bump generation when the plan changes, or when density intent changes so
  // Failed keys can be retried after a zoom (no-spam still holds for stable plan).
  bool const plan_changed =
      out.target_scale != m_target_scale || out.visible_keys != m_visible_keys;
  // Bump on plan change. Also when denser (lower desired) so Failed keys can
  // retry after zoom-in — not on every equal desired re-set (CPU spin).
  if (plan_changed || (m_desired_scale < prev_desired)) {
    ++m_generation;
  }

  m_target_scale = out.target_scale;
  m_visible_keys = out.visible_keys;

  // Zoomed out: drop finer Succeeded tiles only on a *private* cache.
  // Shared path caches must not drop scale-0 cells another ImageItem of the
  // same path still needs (Workspace duplicates / multi-view).
  if (m_target_scale > prev_target && m_cache == &m_owned_cache) {
    m_cache->drop_finer_than(m_target_scale);
  }

  // Host calls set_viewport every paint/tick; cancel work is only useful when
  // the visible set or scale changed.
  if (plan_changed) {
    cancel_obsolete();
    m_draw_plan_dirty = true;
  }
}

void TileSession::set_wake(std::function<void()> wake)
{
  if (!m_inbox) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_inbox->mu);
  m_inbox->wake = std::move(wake);
}

void TileSession::on_source_completion(TileKey key,
                                       std::optional<TileBitmap> bitmap,
                                       std::uint64_t gen)
{
  if (!m_inbox || !m_inbox->alive.load()) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_inbox->mu);
  if (!m_inbox->alive.load()) {
    return;
  }
  m_inbox->pending.push_back(PendingCompletion{key, std::move(bitmap), gen});
}

void TileSession::inject_completion(TileKey key,
                                    std::optional<TileBitmap> bitmap)
{
  on_source_completion(std::move(key), std::move(bitmap), m_generation);
}

int TileSession::pump()
{
  std::vector<PendingCompletion> batch;
  if (m_inbox) {
    std::lock_guard<std::mutex> lock(m_inbox->mu);
    // Cap per tick so a completion storm (warm durable pyramid) cannot hold
    // the GUI for seconds inside TileLoadCoordinator::tick.
    constexpr std::size_t kMaxPumpPerTick = 16;
    if (m_inbox->pending.size() <= kMaxPumpPerTick) {
      batch.swap(m_inbox->pending);
    } else {
      batch.reserve(kMaxPumpPerTick);
      for (std::size_t i = 0; i < kMaxPumpPerTick; ++i) {
        batch.push_back(std::move(m_inbox->pending[i]));
      }
      m_inbox->pending.erase(m_inbox->pending.begin(),
                             m_inbox->pending.begin() + static_cast<std::ptrdiff_t>(kMaxPumpPerTick));
    }
  }

  int applied = 0;
  for (auto& pc : batch) {
    // Ignore completions from older viewport generations when key is InFlight
    // for a newer gen — still accept Succeeded data for fallback utility.
    CacheEntry const* existing = m_cache->find(pc.key);
    if (existing && existing->state == TileState::InFlight &&
        existing->generation > pc.generation) {
      continue;
    }
    if (pc.bitmap && pc.bitmap->valid()) {
      m_cache->set_succeeded(pc.key, std::move(*pc.bitmap), pc.generation);
    } else {
      m_cache->set_failed(pc.key, pc.generation);
    }
    ++applied;
  }
  // Protect visible keys, a 1-cell ring (scroll reuse), and coarser parents.
  // Without the ring, a small pan drops edge cells and forces full re-fetch.
  std::vector<TileKey> protect;
  protect.reserve(m_visible_keys.size() * 12);
  for (TileKey const& k : m_visible_keys) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (k.x + dx < 0 || k.y + dy < 0) {
          continue;
        }
        TileKey const nk{k.scale, k.x + dx, k.y + dy};
        m_cache->touch(nk, m_generation);
        protect.push_back(nk);
      }
    }
    for (int d = 1; k.scale + d <= m_max_scale; ++d) {
      TileKey const pk = parent_key(k, d);
      m_cache->touch(pk, m_generation);
      protect.push_back(pk);
    }
  }
  m_cache->trim_to_budget(m_byte_budget, protect);
  if (applied > 0) {
    m_draw_plan_dirty = true;
  }
  // Coarse tiles landed while viewport is static: host prepareTileLod skips
  // set_viewport, so stable_request_scale never ran again. Advance held scale
  // here so the next issue_requests targets one level finer.
  if (applied > 0) {
    (void)advance_progressive_scale();
  }
  return applied;
}

int TileSession::issue_requests(int budget)
{
  if (!m_source || budget <= 0 || m_content_w <= 0) {
    return 0;
  }

  // Climb one level if the current plan is fully settled.
  (void)advance_progressive_scale();

  // Issue only keys in the current plan (m_visible_keys at m_target_scale).
  // Coarser parents are drawn as stand-ins via draw_plan; requesting every
  // parent chain in the same batch mixed overview with target and felt like
  // "proper res first". Progressive scale already walks max → desired.
  //
  // Failed is terminal for this generation (no retry spam). Generation bumps
  // on viewport/plan change reopen Failed. Incomplete Store cells must be
  // fixed by encode-on-request in thumtoo, not host polling.
  struct Scored {
    TileKey key;
    double dist2 = 0;
  };
  std::vector<Scored> missing;
  missing.reserve(m_visible_keys.size());

  double const cx = m_viewport.content_rect.x + m_viewport.content_rect.w * 0.5;
  double const cy = m_viewport.content_rect.y + m_viewport.content_rect.h * 0.5;

  for (TileKey const& key : m_visible_keys) {
    CacheEntry const* e = m_cache->find(key);
    if (e && (e->state == TileState::Succeeded ||
              e->state == TileState::InFlight)) {
      continue;
    }
    if (e && e->state == TileState::Failed && e->generation == m_generation) {
      continue;
    }
    // scale 0 = full-res: require at least one coarser success first
    // (unless the pyramid is single-level).
    if (key.scale == 0 && m_max_scale > 0 && !has_succeeded_scale_ge(1)) {
      continue;
    }
    RectI const cr = tile_content_rect(m_content_w, m_content_h, key);
    double const tx = cr.x + cr.w * 0.5;
    double const ty = cr.y + cr.h * 0.5;
    double const dx = tx - cx;
    double const dy = ty - cy;
    missing.push_back({key, dx * dx + dy * dy});
  }

  std::sort(missing.begin(), missing.end(),
            [](Scored const& a, Scored const& b) { return a.dist2 < b.dist2; });

  std::vector<TileKey> batch;
  batch.reserve(static_cast<size_t>(budget));
  std::uint64_t const gen = m_generation;

  for (Scored const& s : missing) {
    if (static_cast<int>(batch.size()) >= budget) {
      break;
    }
    CacheEntry const* e = m_cache->find(s.key);
    if (e && (e->state == TileState::Succeeded ||
              e->state == TileState::InFlight)) {
      continue;
    }
    m_cache->set_in_flight(s.key, gen);
    batch.push_back(s.key);
  }

  if (batch.empty()) {
    return 0;
  }

  std::shared_ptr<CompletionInbox> inbox = m_inbox;
  m_source->request(batch, [inbox, gen](TileKey key,
                                        std::optional<TileBitmap> bitmap) {
    if (!inbox || !inbox->alive.load()) {
      return;
    }
    std::function<void()> wake;
    {
      std::lock_guard<std::mutex> lock(inbox->mu);
      if (!inbox->alive.load()) {
        return;
      }
      inbox->pending.push_back(
          PendingCompletion{std::move(key), std::move(bitmap), gen});
      wake = inbox->wake;
    }
    if (wake) {
      wake();
    }
  });

  return static_cast<int>(batch.size());
}

void TileSession::cancel_obsolete()
{
  if (!m_source) {
    return;
  }
  // Keep exact visible keys and their coarser parents (prefetch / stand-ins).
  std::set<TileKey> keep(m_visible_keys.begin(), m_visible_keys.end());
  for (TileKey const& k : m_visible_keys) {
    for (int d = 1; k.scale + d <= m_max_scale; ++d) {
      keep.insert(parent_key(k, d));
    }
  }
  std::vector<TileKey> drop;
  for (TileKey const& key : m_cache->in_flight_keys()) {
    if (keep.find(key) == keep.end()) {
      drop.push_back(key);
    }
  }
  if (drop.empty()) {
    return;
  }
  m_source->cancel(drop);
  for (TileKey const& key : drop) {
    // Drop InFlight so the key can be re-requested if it returns to view.
    // Never erase Succeeded tiles — shared caches keep stand-ins for other
    // viewports of the same path.
    CacheEntry const* e = m_cache->find(key);
    if (e && e->state == TileState::Succeeded) {
      continue;
    }
    m_cache->erase(key);
  }
}

DrawPlan TileSession::draw_plan() const
{
  if (!m_draw_plan_dirty) {
    return m_draw_plan_cache;
  }
  BuildDrawPlanInput in;
  in.content_w = m_content_w;
  in.content_h = m_content_h;
  in.target_scale = m_target_scale;
  in.max_scale = m_max_scale;
  in.visible_keys = m_visible_keys;
  in.has_lqip = m_has_lqip;
  in.lookup = [this](TileKey const& k) -> CacheEntry const* {
    return m_cache->find(k);
  };
  m_draw_plan_cache = build_draw_plan(in);
  m_draw_plan_dirty = false;
  return m_draw_plan_cache;
}

bool TileSession::has_any_succeeded_tile() const
{
  return m_cache && m_cache->has_succeeded();
}

bool TileSession::has_succeeded_scale_ge(int min_scale) const
{
  for (auto const& [k, e] : m_cache->map()) {
    if (k.scale >= min_scale && e.state == TileState::Succeeded
        && e.bitmap.valid()) {
      return true;
    }
  }
  return false;
}

TileSession::Coverage TileSession::coverage() const
{
  Coverage c;
  c.visible = static_cast<int>(m_visible_keys.size());
  for (TileKey const& key : m_visible_keys) {
    CacheEntry const* e = m_cache->find(key);
    if (!e) {
      ++c.missing;
      continue;
    }
    if (e->state == TileState::Succeeded && e->bitmap.valid()) {
      ++c.exact_succeeded;
    } else if (e->state == TileState::InFlight) {
      ++c.in_flight;
    } else if (e->state == TileState::Failed) {
      ++c.failed;
    } else {
      ++c.missing;
    }
  }
  return c;
}

TileSession::DebugSnapshot TileSession::debug_snapshot() const
{
  DebugSnapshot s;
  s.target_scale = m_target_scale;
  s.desired_scale = m_desired_scale;
  s.min_scale = m_min_scale;
  s.max_scale = m_max_scale;
  s.holding = request_scale_holding();
  s.reached_desired = m_reached_desired;
  s.generation = m_generation;
  s.has_lqip = m_has_lqip;
  Coverage const c = coverage();
  s.visible = c.visible;
  s.exact_succeeded = c.exact_succeeded;
  s.in_flight = c.in_flight;
  for (auto const& [k, e] : m_cache->map()) {
    (void)k;
    if (e.state == TileState::Succeeded && e.bitmap.valid()) {
      ++s.cache_succeeded;
    }
  }
  // Plan histogram: rebuild is cheap (RAM lookup only); counts paint kinds.
  DrawPlan const plan = draw_plan();
  for (DrawCommand const& cmd : plan.commands) {
    switch (cmd.kind) {
    case DrawKind::ExactTile:
      ++s.plan_exact;
      break;
    case DrawKind::CoarserTile:
      ++s.plan_parent;
      break;
    case DrawKind::Underlay:
      ++s.plan_underlay;
      break;
    case DrawKind::Empty:
      ++s.plan_empty;
      break;
    }
  }
  // Keys with no command are holes (no exact/parent/underlay).
  int const commanded = s.plan_exact + s.plan_parent + s.plan_underlay
                        + s.plan_empty;
  if (s.visible > commanded) {
    s.plan_empty += s.visible - commanded;
  }
  return s;
}


}  // namespace tilelod
