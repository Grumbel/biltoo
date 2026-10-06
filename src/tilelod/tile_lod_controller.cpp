// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_lod_controller.hpp"

#include "display/displayquality.h"
#include "host/thumtoocache.h"

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
  // Destroy the per-view session first (withdraws its demand). release()
  // only drops registry interest; Ready tiles stay in the path loader until
  // global LRU eviction.
  m_session.reset();
  if (m_shared) {
    QString const p = m_shared->path;
    m_shared.reset();
    TileLodRegistry::instance().release(p);
  }
}

void TileLodController::bindSession()
{
  if (!m_shared || !m_shared->loader) {
    m_session.reset();
    return;
  }
  m_session = std::make_unique<TileSession>(m_shared->loader);
  m_session->set_priority_class(m_priority);
  m_session->set_passive(m_passive);
  m_session->set_on_change(m_on_change);
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

void TileLodController::setOnChange(std::function<void()> cb)
{
  m_on_change = std::move(cb);
  if (m_session) {
    m_session->set_on_change(m_on_change);
  }
}

void TileLodController::setPriority(int cls)
{
  m_priority = cls;
  if (m_session) {
    m_session->set_priority_class(cls);
  }
}

void TileLodController::setPassive(bool on)
{
  m_passive = on;
  if (m_session) {
    m_session->set_passive(on);
  }
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
  m_device_per_content = devicePerContent;
  m_session->set_viewport(vp, marginContent);
}

bool TileLodController::refresh()
{
  if (!m_session) {
    return false;
  }
  m_session->renew();
  if (!m_path.isEmpty()) {
    // Visible paths stay preferred under global idle/byte LRU.
    TileLodRegistry::instance().touch(m_path);
  }
  // thumtoo answers Unavailable for denser scales it refuses (image-heavy
  // pages whose full-page raster exceeds its budget). When every visible
  // cell at the target is Unavailable, raise the page's denser floor one
  // step; the next prepare plans at the finest scale that renders instead
  // of showing parents with an error forever.
  if (m_session->target_scale() < 0) {
    auto const cov = m_session->coverage();
    if (cov.visible > 0 && cov.unavailable >= cov.visible
        && ThumtooCache::noteDenserScaleUnavailable(m_path,
                                                    m_session->target_scale())
        && m_on_change) {
      // The view is settled (parents cover), so nothing else would re-plan:
      // ask the host to repaint → prepare picks up the raised floor now.
      m_on_change();
      return true;
    }
  }
  return m_session->phase() == TileSession::Phase::Loading;
}

bool TileLodController::paint(QPainter* painter, QImage const& lqipUnderlay) const
{
  if (!painter || !m_session) {
    return false;
  }
  DrawPlan plan = m_session->draw_plan();
  if (plan.commands.empty()) {
    return false;
  }

  PaintDrawPlanArgs args;
  args.plan = &plan;
  args.lqip = lqipUnderlay;
  args.smooth = DisplayQuality::smoothScaling();
  args.device_per_content = m_device_per_content;
  TileSession const* session = m_session.get();
  args.resolve = [session](TileKey const& key, TileBitmap const&) -> QImage {
    TileCell const* e = session->find(key);
    if (!e || !e->ready()) {
      return {};
    }
    return tile_bitmap_to_qimage(e->bitmap);
  };
  // Return actual tile pixels drawn — not plan.any_tile (commands may exist
  // while every resolve fails; callers must not skip EMB on a false success).
  return paint_draw_plan(painter, args);
}

bool TileLodController::hasAnyTile() const
{
  return m_session && m_session->has_any_ready();
}

bool TileLodController::hasRetainedTiles() const
{
  if (m_shared && m_shared->loader) {
    return m_shared->loader->has_ready();
  }
  return hasAnyTile();
}

bool TileLodController::viewportFullyCovered() const
{
  return m_session && m_session->coverage().fully_covered();
}

bool TileLodController::viewportSettled() const
{
  return m_session && m_session->phase() != TileSession::Phase::Loading;
}

bool TileLodController::isLoading() const
{
  return m_session && m_session->phase() == TileSession::Phase::Loading;
}

TileSession::Phase TileLodController::phase() const
{
  return m_session ? m_session->phase() : TileSession::Phase::Idle;
}

std::string TileLodController::statusLine() const
{
  return m_session ? m_session->status_line() : std::string{};
}

int TileLodController::targetScale() const
{
  return m_session ? m_session->target_scale() : 0;
}

}  // namespace tilelod
