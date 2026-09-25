// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_lod_controller.hpp"

#include <QPainter>

namespace tilelod {

TileLodController::TileLodController() = default;

TileLodController::~TileLodController() { unbind(); }

bool TileLodController::shouldUseTiles(double devicePerContent, int contentLongEdge)
{
  if (!(devicePerContent > 0.0) || contentLongEdge <= 0) {
    return false;
  }
  // contentLongEdge is *layout* long edge (may be a crop box < 256). Do not
  // treat that as "file smaller than one tile" — the pyramid is still file-native
  // and 256² cells remain useful. LQIP is only for negligible on-screen size.
  double const screenLong = devicePerContent * static_cast<double>(contentLongEdge);
  return screenLong > 32.0;
}

void TileLodController::unbind()
{
  // Destroy the per-item session first (viewport / generation / inbox).
  // release() only drops registry interest; Succeeded tiles stay in the
  // path entry until global LRU eviction.
  m_session.reset();
  if (m_shared) {
    QString const p = m_shared->path;
    m_shared.reset();
    TileLodRegistry::instance().release(p);
  }
}

void TileLodController::bindSession()
{
  if (!m_shared || !m_shared->source || !m_shared->cache) {
    m_session.reset();
    return;
  }
  m_session = std::make_unique<TileSession>(m_shared->source.get(),
                                            m_shared->cache.get());
}

void TileLodController::setPath(QString path)
{
  if (m_path == path && m_shared) {
    return;
  }
  unbind();
  m_path = std::move(path);
  if (m_path.isEmpty()) {
    return;
  }
  // Re-acquire may return a zero-ref retained entry with tiles still warm.
  m_shared = TileLodRegistry::instance().acquire(m_path);
  bindSession();
}

void TileLodController::setContentSize(int w, int h, int minScale)
{
  if (m_session) {
    m_session->set_content_size(w, h, minScale);
  }
}

void TileLodController::setHasLqip(bool on)
{
  if (m_session) {
    m_session->set_has_lqip(on);
  }
}

void TileLodController::updateViewport(QRectF const& contentVisible,
                                       double devicePerContent,
                                       double marginContent)
{
  if (!m_session) {
    return;
  }
  Viewport vp;
  vp.content_rect = {contentVisible.x(), contentVisible.y(), contentVisible.width(),
                     contentVisible.height()};
  vp.device_per_content = devicePerContent;
  m_session->set_viewport(vp, marginContent);
}

int TileLodController::tick(int requestBudget)
{
  if (!m_session || !m_enabled) {
    return 0;
  }
  int const applied = m_session->pump();
  // Completions keep the path preferred under global idle/byte LRU.
  if (applied > 0 && !m_path.isEmpty()) {
    TileLodRegistry::instance().touch(m_path);
  }
  m_session->issue_requests(requestBudget);
  return applied;
}

bool TileLodController::paint(QPainter* painter, QImage const& lqipUnderlay) const
{
  if (!painter || !m_session || !m_enabled) {
    return false;
  }
  DrawPlan plan = m_session->draw_plan();
  if (plan.commands.empty()) {
    return false;
  }

  PaintDrawPlanArgs args;
  args.plan = &plan;
  args.lqip = lqipUnderlay;
  args.smooth = true;
  args.resolve = [this](TileKey const& key, TileBitmap const&) -> QImage {
    CacheEntry const* e = m_session->cache().find(key);
    if (!e || e->state != TileState::Succeeded || !e->bitmap.valid()) {
      return {};
    }
    return tile_bitmap_to_qimage(e->bitmap);
  };
  paint_draw_plan(painter, args);
  return plan.any_tile;
}

bool TileLodController::hasAnyTile() const
{
  return m_session && m_session->has_any_succeeded_tile();
}

bool TileLodController::hasRetainedTiles() const
{
  if (m_shared && m_shared->cache) {
    return m_shared->cache->has_succeeded();
  }
  return hasAnyTile();
}

bool TileLodController::viewportFullyCovered() const
{
  return m_session && m_session->coverage().fully_covered()
         && !m_session->request_scale_holding();
}

int TileLodController::targetScale() const
{
  return m_session ? m_session->target_scale() : 0;
}

}  // namespace tilelod
