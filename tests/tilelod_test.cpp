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
  CHECK_EQ(n, 2);  // remaining two
  CHECK_EQ(static_cast<int>(src.requested.size()), 4);

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
  test_fallback_parent_uv();
  test_lqip_until_tile();
  test_budget_and_session();
  test_cancel_on_viewport_change();
  test_edge_tile_content_rect();
  test_parent_key();

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "tilelod_test: all passed\n";
  return 0;
}
