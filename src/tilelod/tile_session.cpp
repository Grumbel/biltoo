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
    }
  }
  if (m_inbox) {
    std::lock_guard<std::mutex> lock(m_inbox->mu);
    m_inbox->pending.clear();
  }
}

void TileSession::set_content_size(int width, int height, int min_scale)
{
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
}


int TileSession::stable_request_scale(int desired_scale)
{
  using clock = std::chrono::steady_clock;
  if (!m_have_stable_scale) {
    m_have_stable_scale = true;
    m_stable_scale = desired_scale;
    m_pending_scale = desired_scale;
    m_pending_since = clock::now();
    return m_stable_scale;
  }
  if (desired_scale == m_stable_scale) {
    m_pending_scale = desired_scale;
    return m_stable_scale;
  }
  int const delta = desired_scale > m_stable_scale
                        ? desired_scale - m_stable_scale
                        : m_stable_scale - desired_scale;
  // Large jumps (fit, multi-notch) commit immediately.
  if (delta > 1) {
    m_stable_scale = desired_scale;
    m_pending_scale = desired_scale;
    m_pending_since = clock::now();
    return m_stable_scale;
  }
  // Adjacent step: hold previous scale briefly so wheel zoom does not
  // enqueue a full intermediate grid every frame.
  if (desired_scale != m_pending_scale) {
    m_pending_scale = desired_scale;
    m_pending_since = clock::now();
    return m_stable_scale;
  }
  if (clock::now() - m_pending_since >= kScaleHold) {
    m_stable_scale = desired_scale;
    return m_stable_scale;
  }
  return m_stable_scale;
}

bool TileSession::request_scale_holding() const
{
  return m_have_stable_scale && m_desired_scale != m_stable_scale;
}

void TileSession::set_viewport(Viewport const& vp, double margin_content)
{
  m_viewport = vp;
  ++m_generation;

  PlannerInput in;
  in.content_w = m_content_w;
  in.content_h = m_content_h;
  in.min_scale = m_min_scale;
  in.max_scale = m_max_scale;
  in.viewport = vp;
  in.margin_content = margin_content;

  // Desired scale from density, then hold adjacent steps during continuous zoom.
  PlannerOutput const ideal = plan_visible_tiles(in);
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
  m_target_scale = out.target_scale;
  m_visible_keys = out.visible_keys;

  // Zoomed out: drop finer Succeeded tiles only on a *private* cache.
  // Shared path caches must not drop scale-0 cells another ImageItem of the
  // same path still needs (Workspace duplicates / multi-view).
  if (m_target_scale > prev_target && m_cache == &m_owned_cache) {
    m_cache->drop_finer_than(m_target_scale);
  }

  cancel_obsolete();
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
    batch.swap(m_inbox->pending);
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
  // Protect current visible keys and their coarser parents (draw-plan
  // stand-ins). Evict other Succeeded tiles under budget.
  std::vector<TileKey> protect;
  protect.reserve(m_visible_keys.size() * 4);
  for (TileKey const& k : m_visible_keys) {
    m_cache->touch(k, m_generation);
    protect.push_back(k);
    for (int d = 1; k.scale + d <= m_max_scale; ++d) {
      TileKey const pk = parent_key(k, d);
      m_cache->touch(pk, m_generation);
      protect.push_back(pk);
    }
  }
  m_cache->trim_to_budget(m_byte_budget, protect);
  return applied;
}

int TileSession::issue_requests(int budget)
{
  if (!m_source || budget <= 0 || m_content_w <= 0) {
    return 0;
  }

  // Visible keys missing or failed (retry failed lightly by re-request).
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
    // Same viewport generation already failed — do not re-hammer the source
    // every 33ms. A later set_viewport bumps generation and allows retry.
    if (e && e->state == TileState::Failed && e->generation == m_generation) {
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

  int const n = std::min(budget, static_cast<int>(missing.size()));
  if (n <= 0) {
    return 0;
  }

  std::vector<TileKey> batch;
  batch.reserve(static_cast<size_t>(n));
  std::uint64_t const gen = m_generation;
  for (int i = 0; i < n; ++i) {
    TileKey const& key = missing[static_cast<size_t>(i)].key;
    m_cache->set_in_flight(key, gen);
    batch.push_back(key);
  }

  // Capture generation + inbox (not `this`) so late worker callbacks are safe
  // after TileSession destruction.
  std::shared_ptr<CompletionInbox> inbox = m_inbox;
  m_source->request(batch, [inbox, gen](TileKey key,
                                        std::optional<TileBitmap> bitmap) {
    if (!inbox || !inbox->alive.load()) {
      return;
    }
    std::lock_guard<std::mutex> lock(inbox->mu);
    if (!inbox->alive.load()) {
      return;
    }
    inbox->pending.push_back(
        PendingCompletion{std::move(key), std::move(bitmap), gen});
  });

  return n;
}

void TileSession::cancel_obsolete()
{
  if (!m_source) {
    return;
  }
  std::set<TileKey> visible(m_visible_keys.begin(), m_visible_keys.end());
  std::vector<TileKey> drop;
  for (TileKey const& key : m_cache->in_flight_keys()) {
    if (visible.find(key) == visible.end()) {
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
  return build_draw_plan(in);
}

bool TileSession::has_any_succeeded_tile() const
{
  for (auto const& [k, e] : m_cache->map()) {
    (void)k;
    if (e.state == TileState::Succeeded && e.bitmap.valid()) {
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
      continue;
    }
    if (e->state == TileState::Succeeded && e->bitmap.valid()) {
      ++c.exact_succeeded;
    } else if (e->state == TileState::InFlight) {
      ++c.in_flight;
    }
  }
  return c;
}

}  // namespace tilelod
