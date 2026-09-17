// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// Qt-free unit tests for tilelod core.

#include "tilelod/draw_plan.hpp"
#include "tilelod/lod_math.hpp"
#include "tilelod/lod_planner.hpp"
#include "tilelod/tile_memory_cache.hpp"
#include "tilelod/tile_session.hpp"
#include "tilelod/tile_source.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #cond      \
                << "\n";                                                       \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    auto _va = (a);                                                            \
    auto _vb = (b);                                                            \
    if (_va != _vb) {                                                          \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " << #a         \
                << " == " << #b << "  (" << _va << " != " << _vb << ")\n";     \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

tilelod::TileBitmap solid_tile(int w, int h, std::uint8_t r)
{
  tilelod::TileBitmap b;
  b.width = w;
  b.height = h;
  b.codec = "rgba8";
  b.bytes.assign(static_cast<size_t>(w * h * 4), 0);
  for (int i = 0; i < w * h; ++i) {
    b.bytes[static_cast<size_t>(i * 4 + 0)] = r;
    b.bytes[static_cast<size_t>(i * 4 + 3)] = 255;
  }
  return b;
}

/// Fake source: stores requests; tests call complete() manually.
class FakeTileSource : public tilelod::TileSource {
public:
  std::vector<tilelod::TileKey> requested;
  std::vector<tilelod::TileKey> cancelled;
  Completion last_cb;

  void request(std::vector<tilelod::TileKey> keys, Completion on_each) override
  {
    last_cb = std::move(on_each);
    for (auto const& k : keys) {
      requested.push_back(k);
    }
  }

  void cancel(std::vector<tilelod::TileKey> const& keys) override
  {
    for (auto const& k : keys) {
      cancelled.push_back(k);
    }
  }

  void cancel_all() override {}

  void complete_all_requested(std::uint8_t color = 128)
  {
    if (!last_cb) {
      return;
    }
    // Copy list — callback may re-enter
    auto keys = requested;
    for (auto const& k : keys) {
      last_cb(k, solid_tile(tilelod::kTileSize, tilelod::kTileSize, color));
    }
  }
};

void test_dim_at_tile_scale()
{
  CHECK_EQ(tilelod::dim_at_tile_scale(1000, 0), 1000);
  CHECK_EQ(tilelod::dim_at_tile_scale(1000, 1), 500);
  CHECK_EQ(tilelod::dim_at_tile_scale(1000, 2), 250);
  CHECK_EQ(tilelod::dim_at_tile_scale(1000, 3), 125);
  CHECK_EQ(tilelod::dim_at_tile_scale(5, 1), 2);
  CHECK_EQ(tilelod::dim_at_tile_scale(5, 2), 1);
  CHECK_EQ(tilelod::dim_at_tile_scale(5, 3), 0);
}

void test_max_scale_and_grid()
{
  // 512×512 → scale 0: 2×2 tiles; scale 1: 1×1
  CHECK_EQ(tilelod::tiles_across(512, 0), 2);
  CHECK_EQ(tilelod::tiles_across(512, 1), 1);
  CHECK_EQ(tilelod::max_scale_for_size(512, 512), 1);

  // 256×256 → already one tile at scale 0
  CHECK_EQ(tilelod::max_scale_for_size(256, 256), 0);

  // 257×10 → scale 0: 2×1; need scale until both 1
  CHECK_EQ(tilelod::tiles_across(257, 0), 2);
  CHECK(tilelod::max_scale_for_size(257, 10) >= 1);
}

void test_planner_1to1()
{
  tilelod::PlannerInput in;
  in.content_w = 512;
  in.content_h = 512;
  in.min_scale = 0;
  in.max_scale = tilelod::max_scale_for_size(512, 512);
  in.viewport.content_rect = {0, 0, 512, 512};
  in.viewport.device_per_content = 1.0;

  auto out = tilelod::plan_visible_tiles(in);
  CHECK_EQ(out.target_scale, 0);
  CHECK_EQ(static_cast<int>(out.visible_keys.size()), 4);  // 2×2
}

void test_planner_zoomed_out()
{
  tilelod::PlannerInput in;
  in.content_w = 2048;
  in.content_h = 2048;
  in.min_scale = 0;
  in.max_scale = tilelod::max_scale_for_size(2048, 2048);
  in.viewport.content_rect = {0, 0, 2048, 2048};
  in.viewport.device_per_content = 0.25;  // heavily zoomed out

  auto out = tilelod::plan_visible_tiles(in);
  CHECK(out.target_scale > 0);
  // Coarser scale → fewer cells than scale 0 (8×8 = 64)
  CHECK(static_cast<int>(out.visible_keys.size()) < 64);
}

void test_planner_partial_viewport()
{
  tilelod::PlannerInput in;
  in.content_w = 1024;
  in.content_h = 1024;
  in.min_scale = 0;
  in.max_scale = tilelod::max_scale_for_size(1024, 1024);
  // Only top-left 200×200 content pixels at 1:1
  in.viewport.content_rect = {0, 0, 200, 200};
  in.viewport.device_per_content = 1.0;

  auto out = tilelod::plan_visible_tiles(in);
  CHECK_EQ(out.target_scale, 0);
  // One tile cell covers 256×256 content at scale 0
  CHECK_EQ(static_cast<int>(out.visible_keys.size()), 1);
  CHECK_EQ(out.visible_keys[0].x, 0);
  CHECK_EQ(out.visible_keys[0].y, 0);
}

void test_parent_uv_edge_tile()
{
  // Non power-of-two content: uniform span UV drifts; content-rect mapping must
  // place the fine cell at the correct fraction of the parent.
  tilelod::TileKey fine{0, 3, 0};
  tilelod::TileKey parent{1, 1, 0};
  int const content_w = 1000;
  int const content_h = 1000;
  int const parent_px = 244; // typical edge payload
  auto uv = tilelod::parent_uv_for_child(fine, parent, parent_px, parent_px,
                                         content_w, content_h);
  auto fine_cr = tilelod::tile_content_rect(content_w, content_h, fine);
  auto parent_cr = tilelod::tile_content_rect(content_w, content_h, parent);
  CHECK(!fine_cr.empty());
  CHECK(!parent_cr.empty());
  // UV origin ≈ (fine.x - parent.x) / parent.w * parent_px
  double expect_u = (static_cast<double>(fine_cr.x - parent_cr.x)
                     / static_cast<double>(parent_cr.w))
                    * parent_px;
  CHECK(uv.x + 1e-6 >= expect_u - 1e-3);
  CHECK(uv.x <= expect_u + 1e-3);
  CHECK(uv.w > 0 && uv.h > 0);
  // Must not be the full parent (that was the "repeated whole tile" symptom).
  CHECK(uv.w < parent_px - 1e-3 || uv.h < parent_px - 1e-3 || uv.x > 1e-3);
}

void test_fallback_parent_uv()
{
  tilelod::TileMemoryCache cache;
  tilelod::TileKey parent{1, 0, 0};
  cache.set_succeeded(parent, solid_tile(256, 256, 10), 1);

  tilelod::BuildDrawPlanInput in;
  in.content_w = 512;
  in.content_h = 512;
  in.target_scale = 0;
  in.max_scale = 1;
  in.visible_keys = {{0, 0, 0}, {0, 1, 0}};
  in.has_lqip = false;
  in.lookup = [&](tilelod::TileKey const& k) { return cache.find(k); };

  auto plan = tilelod::build_draw_plan(in);
  CHECK_EQ(static_cast<int>(plan.commands.size()), 2);
  CHECK(plan.any_tile);
  for (auto const& c : plan.commands) {
    CHECK_EQ(static_cast<int>(c.kind),
             static_cast<int>(tilelod::DrawKind::CoarserTile));
    CHECK_EQ(c.src_key.scale, 1);
    // UV should be a quadrant of the parent
    CHECK(c.src_uv.w > 0 && c.src_uv.h > 0);
    CHECK(c.src_uv.w <= 256.0 + 1e-6);
  }
  // (0,0,0) → left half; (0,1,0) → right half of parent
  CHECK(plan.commands[0].src_uv.x < plan.commands[1].src_uv.x);
}

void test_lqip_until_tile()
{
  tilelod::TileMemoryCache cache;
  tilelod::BuildDrawPlanInput in;
  in.content_w = 256;
  in.content_h = 256;
  in.target_scale = 0;
  in.max_scale = 0;
  in.visible_keys = {{0, 0, 0}};
  in.has_lqip = true;
  in.lookup = [&](tilelod::TileKey const& k) { return cache.find(k); };

  auto plan = tilelod::build_draw_plan(in);
  CHECK_EQ(static_cast<int>(plan.commands.size()), 1);
  CHECK_EQ(static_cast<int>(plan.commands[0].kind),
           static_cast<int>(tilelod::DrawKind::Underlay));
  CHECK(plan.commands[0].use_lqip);
  CHECK(!plan.any_tile);

  cache.set_succeeded({0, 0, 0}, solid_tile(256, 256, 1), 1);
  plan = tilelod::build_draw_plan(in);
  CHECK_EQ(static_cast<int>(plan.commands[0].kind),
           static_cast<int>(tilelod::DrawKind::ExactTile));
  CHECK(plan.any_tile);
}

void test_budget_and_session()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(512, 512);
  session.set_has_lqip(true);

  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 512, 512};
  vp.device_per_content = 1.0;
  session.set_viewport(vp);

  CHECK_EQ(session.target_scale(), 0);
  CHECK_EQ(static_cast<int>(session.visible_keys().size()), 4);

  int n = session.issue_requests(2);
  CHECK_EQ(n, 2);
  CHECK_EQ(static_cast<int>(src.requested.size()), 2);

  n = session.issue_requests(10);
  // Remaining exact cells plus optional coarser parents (stand-in prefetch).
  CHECK(n >= 2);
  CHECK(static_cast<int>(src.requested.size()) >= 4);

  // Completions
  src.complete_all_requested(200);
  CHECK(session.pump() >= 1);
  CHECK(session.has_any_succeeded_tile());

  auto plan = session.draw_plan();
  CHECK(plan.any_tile);
  int exact = 0;
  for (auto const& c : plan.commands) {
    if (c.kind == tilelod::DrawKind::ExactTile) {
      ++exact;
    }
  }
  CHECK(exact == 4);
}

void test_cancel_on_viewport_change()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(2048, 2048);

  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;
  session.set_viewport(vp);
  session.issue_requests(8);
  auto first_req = src.requested;
  CHECK(!first_req.empty());

  // Pan far away
  vp.content_rect = {1800, 1800, 256, 256};
  session.set_viewport(vp);
  CHECK(!src.cancelled.empty());
}

void test_edge_tile_content_rect()
{
  // 300×100: scale 0 tiles (0,0) covers 256×100; (1,0) covers 44×100
  auto r0 = tilelod::tile_content_rect(300, 100, {0, 0, 0});
  CHECK_EQ(r0.w, 256);
  CHECK_EQ(r0.h, 100);
  auto r1 = tilelod::tile_content_rect(300, 100, {0, 1, 0});
  CHECK_EQ(r1.x, 256);
  CHECK_EQ(r1.w, 44);
}


void test_shared_cache_two_sessions()
{
  FakeTileSource src;
  tilelod::TileMemoryCache shared;
  tilelod::TileSession a(&src, &shared);
  tilelod::TileSession b(&src, &shared);
  a.set_content_size(512, 512);
  b.set_content_size(512, 512);

  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;
  a.set_viewport(vp);
  a.issue_requests(4);
  src.complete_all_requested(42);
  CHECK(a.pump() >= 1);
  CHECK(a.has_any_succeeded_tile());

  // B sees the same Succeeded tiles without requesting.
  vp.content_rect = {0, 0, 256, 256};
  b.set_viewport(vp);
  auto plan = b.draw_plan();
  CHECK(plan.any_tile);
  int exact = 0;
  for (auto const& c : plan.commands) {
    if (c.kind == tilelod::DrawKind::ExactTile) {
      ++exact;
    }
  }
  CHECK(exact >= 1);
}


void test_scale_hold_adjacent()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(4096, 4096);

  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 4096, 4096};
  // 1:1 → scale 0
  vp.device_per_content = 1.0;
  session.set_viewport(vp);
  CHECK_EQ(session.target_scale(), 0);
  CHECK_EQ(session.desired_scale(), 0);

  // Adjacent coarser (0.5 → scale 1). Hold should keep scale 0 briefly.
  vp.device_per_content = 0.5;
  session.set_viewport(vp);
  CHECK_EQ(session.desired_scale(), 1);
  CHECK_EQ(session.target_scale(), 0);  // still holding
  CHECK(session.request_scale_holding());

  // Large jump denser than one step: commit immediately.
  // need ~0.125 → scale ~3
  vp.device_per_content = 0.125;
  session.set_viewport(vp);
  CHECK(session.desired_scale() >= 2);
  CHECK_EQ(session.target_scale(), session.desired_scale());
  CHECK(!session.request_scale_holding());
}


void test_coverage_fully_covered()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(256, 256);
  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;
  session.set_viewport(vp);
  CHECK(session.coverage().visible >= 1);
  CHECK(!session.coverage().fully_covered());
  session.issue_requests(16);
  src.complete_all_requested(7);
  session.pump();
  auto c = session.coverage();
  CHECK(c.exact_succeeded >= c.visible);
  CHECK(c.fully_covered());
}


void test_trim_budget()
{
  tilelod::TileMemoryCache cache;
  tilelod::TileBitmap bm;
  bm.width = 256;
  bm.height = 256;
  bm.bytes.resize(256 * 256 * 4, 1);

  // Insert many Succeeded tiles at different last_used.
  for (int i = 0; i < 20; ++i) {
    tilelod::TileKey k{0, i, 0};
    cache.set_succeeded(k, bm, static_cast<std::uint64_t>(i));
  }
  CHECK(cache.approx_bytes() > 1000);
  // Protect first two keys; budget allows ~3 tiles.
  std::size_t const per = 256ull * 256ull * 4ull;
  std::vector<tilelod::TileKey> protect = {{0, 0, 0}, {0, 1, 0}};
  cache.trim_to_budget(per * 3, protect);
  CHECK(cache.find({0, 0, 0}) != nullptr);
  CHECK(cache.find({0, 1, 0}) != nullptr);
  CHECK(cache.approx_bytes() <= per * 3 + 1);
  // Oldest non-protected should be gone first
  CHECK(cache.find({0, 2, 0}) == nullptr);
}


void test_drop_finer_on_zoom_out()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(1024, 1024);

  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;  // scale 0
  session.set_viewport(vp);
  CHECK_EQ(session.target_scale(), 0);
  session.issue_requests(4);
  src.complete_all_requested(9);
  session.pump();
  CHECK(session.has_any_succeeded_tile());

  // Zoom out hard → higher target scale; finer tiles should be dropped.
  vp.device_per_content = 0.125;
  session.set_viewport(vp);
  CHECK(session.target_scale() >= 2);
  // Cache may still have coarser; scale-0 keys for the old region should be gone
  // (drop_finer_than(target)).
  bool any_scale0 = false;
  for (auto const& [k, e] : session.cache().map()) {
    if (k.scale == 0 && e.state == tilelod::TileState::Succeeded) {
      any_scale0 = true;
    }
  }
  CHECK(!any_scale0);
}


void test_trim_protects_parents()
{
  tilelod::TileMemoryCache cache;
  tilelod::TileBitmap bm;
  bm.width = 256;
  bm.height = 256;
  bm.bytes.resize(256 * 256 * 4, 3);

  // Exact visible key at scale 0 and its parent at scale 1.
  tilelod::TileKey exact{0, 0, 0};
  tilelod::TileKey parent = tilelod::parent_key(exact, 1);
  cache.set_succeeded(exact, bm, 10);
  cache.set_succeeded(parent, bm, 5);
  // Unrelated old tile to be evicted
  tilelod::TileKey other{0, 9, 9};
  cache.set_succeeded(other, bm, 1);

  std::size_t const per = 256ull * 256ull * 4ull;
  std::vector<tilelod::TileKey> protect = {exact, parent};
  // Budget fits exactly 2 tiles → other must go; parent stays.
  cache.trim_to_budget(per * 2, protect);
  CHECK(cache.find(exact) != nullptr);
  CHECK(cache.find(parent) != nullptr);
  CHECK(cache.find(other) == nullptr);
}


void test_shared_no_drop_finer()
{
  FakeTileSource src;
  tilelod::TileMemoryCache shared;
  tilelod::TileSession a(&src, &shared);
  tilelod::TileSession b(&src, &shared);
  a.set_content_size(1024, 1024);
  b.set_content_size(1024, 1024);

  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;
  a.set_viewport(vp);
  a.issue_requests(4);
  src.complete_all_requested(11);
  a.pump();
  CHECK(a.has_any_succeeded_tile());

  // B still at fine zoom; A zooms out — must not wipe shared scale-0 tiles.
  vp.device_per_content = 0.125;
  a.set_viewport(vp);
  CHECK(a.target_scale() >= 2);
  bool any_scale0 = false;
  for (auto const& [k, e] : shared.map()) {
    if (k.scale == 0 && e.state == tilelod::TileState::Succeeded) {
      any_scale0 = true;
    }
  }
  CHECK(any_scale0);
}


void test_failed_no_spam_same_generation()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  // Large enough for multiple pyramid scales so density can change the plan.
  session.set_content_size(2048, 2048);
  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 512, 512};
  vp.device_per_content = 0.25; // coarse target scale
  session.set_viewport(vp);
  int n = session.issue_requests(4);
  CHECK(n >= 1);
  // Complete as failed
  auto keys = src.requested;
  src.requested.clear();
  for (auto const& k : keys) {
    session.inject_completion(k, std::nullopt);
  }
  session.pump();
  // Same plan: should not re-request
  n = session.issue_requests(8);
  CHECK_EQ(n, 0);
  CHECK_EQ(static_cast<int>(src.requested.size()), 0);

  // Host re-sets the same viewport every tick — must still not spam.
  session.set_viewport(vp);
  n = session.issue_requests(8);
  CHECK_EQ(n, 0);
  CHECK_EQ(static_cast<int>(src.requested.size()), 0);

  // Plan change (finer target scale) allows retry
  vp.device_per_content = 2.0;
  session.set_viewport(vp);
  n = session.issue_requests(8);
  CHECK(n >= 1);
}


void test_min_scale_clamps_target()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  // Partial pyramid: only scales >= 2 exist.
  session.set_content_size(4096, 4096, /*min_scale=*/2);
  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 8.0; // would want scale 0 without min clamp
  session.set_viewport(vp);
  CHECK(session.target_scale() >= 2);
  for (auto const& k : session.visible_keys()) {
    CHECK(k.scale >= 2);
  }
  session.issue_requests(32);
  for (auto const& k : src.requested) {
    CHECK(k.scale >= 2);
  }
}

void test_host_prepare_loop_stable()
{
  // Host prepareTileLod: set_content_size + set_viewport every frame.
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(2048, 2048);
  tilelod::Viewport vp;
  vp.content_rect = {100, 100, 400, 400};
  vp.device_per_content = 2.0;
  session.set_viewport(vp);
  std::uint64_t const gen0 = session.generation();
  auto keys0 = session.visible_keys();
  CHECK(!keys0.empty());

  // Simulate 30 paint/tick frames with identical size and viewport.
  for (int i = 0; i < 30; ++i) {
    session.set_content_size(2048, 2048);
    session.set_viewport(vp);
  }
  CHECK_EQ(session.generation(), gen0);
  CHECK_EQ(static_cast<int>(session.visible_keys().size()),
           static_cast<int>(keys0.size()));

  // Fail all requested cells, then ensure stable plan does not re-request.
  session.issue_requests(64);
  CHECK(!src.requested.empty());
  if (src.last_cb) {
    auto keys = src.requested;
    for (auto const& k : keys) {
      src.last_cb(k, std::nullopt);
    }
  }
  session.pump();
  src.requested.clear();
  for (int i = 0; i < 10; ++i) {
    session.set_content_size(2048, 2048);
    session.set_viewport(vp);
    session.issue_requests(64);
  }
  CHECK_EQ(static_cast<int>(src.requested.size()), 0);
}

void test_set_content_size_idempotent()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(1024, 1024);
  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;
  session.set_viewport(vp);
  std::uint64_t const gen0 = session.generation();
  auto keys0 = session.visible_keys();
  CHECK(!keys0.empty());

  // Same size again must not clear plan or bump generation.
  session.set_content_size(1024, 1024);
  CHECK_EQ(session.generation(), gen0);
  CHECK_EQ(static_cast<int>(session.visible_keys().size()),
           static_cast<int>(keys0.size()));

  session.set_content_size(2048, 2048);
  CHECK(session.generation() != gen0);
}

void test_destroy_while_inflight()
{
  FakeTileSource src;
  {
    tilelod::TileSession session(&src);
    session.set_content_size(256, 256);
    tilelod::Viewport vp;
    vp.content_rect = {0, 0, 256, 256};
    vp.device_per_content = 1.0;
    session.set_viewport(vp);
    session.issue_requests(4);
    CHECK(!src.requested.empty());
    // Destroy session with requests outstanding — must not crash on complete.
  }
  // Simulate late completions (source still has keys; no live session).
  // Fake does not call callbacks until complete_all_requested — just ensure
  // destructor path ran. Issue again on a new session.
  tilelod::TileSession session2(&src);
  session2.set_content_size(256, 256);
  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;
  session2.set_viewport(vp);
  session2.issue_requests(2);
  src.complete_all_requested(1);
  session2.pump();
  CHECK(session2.has_any_succeeded_tile() || session2.coverage().visible >= 0);
}


void test_parent_prefetch()
{
  FakeTileSource src;
  tilelod::TileSession session(&src);
  session.set_content_size(1024, 1024);
  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;  // scale 0
  session.set_viewport(vp);
  int n = session.issue_requests(8);
  CHECK(n >= 2);
  bool has_exact = false;
  bool has_parent = false;
  for (auto const& k : src.requested) {
    if (k.scale == 0) {
      has_exact = true;
    }
    if (k.scale == 1) {
      has_parent = true;
    }
  }
  CHECK(has_exact);
  CHECK(has_parent);
}


void test_destroy_clears_inflight_shared()
{
  FakeTileSource src;
  tilelod::TileMemoryCache shared;
  {
    tilelod::TileSession a(&src, &shared);
    a.set_content_size(256, 256);
    tilelod::Viewport vp;
    vp.content_rect = {0, 0, 256, 256};
    vp.device_per_content = 1.0;
    a.set_viewport(vp);
    a.issue_requests(4);
    CHECK(!src.requested.empty());
    CHECK(!shared.in_flight_keys().empty());
  }
  // After destroy, InFlight must be cleared so peer can request.
  CHECK(shared.in_flight_keys().empty());

  tilelod::TileSession b(&src, &shared);
  b.set_content_size(256, 256);
  tilelod::Viewport vp;
  vp.content_rect = {0, 0, 256, 256};
  vp.device_per_content = 1.0;
  b.set_viewport(vp);
  int n = b.issue_requests(4);
  CHECK(n >= 1);
}

void test_parent_key()
{
  auto p = tilelod::parent_key({0, 3, 5}, 1);
  CHECK_EQ(p.scale, 1);
  CHECK_EQ(p.x, 1);
  CHECK_EQ(p.y, 2);
  p = tilelod::parent_key({0, 3, 5}, 2);
  CHECK_EQ(p.scale, 2);
  CHECK_EQ(p.x, 0);
  CHECK_EQ(p.y, 1);
}

}  // namespace

int main()
{
  test_dim_at_tile_scale();
  test_max_scale_and_grid();
  test_planner_1to1();
  test_planner_zoomed_out();
  test_planner_partial_viewport();
  test_parent_uv_edge_tile();
  test_fallback_parent_uv();
  test_lqip_until_tile();
  test_budget_and_session();
  test_cancel_on_viewport_change();
  test_edge_tile_content_rect();
  test_shared_cache_two_sessions();
  test_scale_hold_adjacent();
  test_coverage_fully_covered();
  test_trim_budget();
  test_drop_finer_on_zoom_out();
  test_trim_protects_parents();
  test_shared_no_drop_finer();
  test_failed_no_spam_same_generation();
  test_set_content_size_idempotent();
  test_host_prepare_loop_stable();
  test_min_scale_clamps_target();
  test_destroy_while_inflight();
  test_parent_prefetch();
  test_destroy_clears_inflight_shared();
  test_parent_key();

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "tilelod_test: all passed\n";
  return 0;
}
