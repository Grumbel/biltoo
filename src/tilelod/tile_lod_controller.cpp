// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_lod_controller.hpp"

#include "thumtoocache.h"

#include <QPainter>
#include <cstring>

namespace tilelod {

TileLodController::TileLodController() = default;

bool TileLodController::shouldUseTiles(double devicePerContent, int contentLongEdge)
{
  if (!(devicePerContent > 0.0) || contentLongEdge <= 0) {
    return false;
  }
  // Soft ladder max long edge is 512. When the on-screen long edge exceeds
  // that, grid tiles are the correct path for sharp display.
  double const screenLong = devicePerContent * static_cast<double>(contentLongEdge);
  return screenLong > 512.0 * 1.05;
}

void TileLodController::ensureSource()
{
  if (m_source) {
    return;
  }
  auto fetch = [](std::string const& /*uri*/,
                  std::vector<ThumtooTileSource::TileCoord> const& coords,
                  std::function<void(std::size_t, std::optional<TileBitmap>)> on_cell) {
    // URI is bound via path on the controller; ThumtooCache maps path → uri.
    // We pass path through a side channel: requestTiles uses m_path from outer.
    // This lambda is replaced in setPath with a path-capturing one.
    Q_UNUSED(coords);
    Q_UNUSED(on_cell);
  };
  m_source = std::make_unique<ThumtooTileSource>(std::string{}, std::move(fetch));
  m_session = std::make_unique<TileSession>(m_source.get());
}

void TileLodController::setPath(QString path)
{
  m_path = std::move(path);
  ensureSource();

  QString pathCopy = m_path;
  auto fetch = [pathCopy](std::string const& /*uri*/,
                          std::vector<ThumtooTileSource::TileCoord> const& coords,
                          std::function<void(std::size_t, std::optional<TileBitmap>)> on_cell) {
    QVector<ThumtooCache::TileCoord> qcoords;
    qcoords.reserve(static_cast<int>(coords.size()));
    for (auto const& c : coords) {
      qcoords.push_back({c.scale, c.x, c.y});
    }
    ThumtooCache::requestTiles(
        pathCopy, qcoords,
        [on_cell](std::size_t index, QImage image) {
          if (image.isNull()) {
            on_cell(index, std::nullopt);
            return;
          }
          QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
          TileBitmap bm;
          bm.width = rgba.width();
          bm.height = rgba.height();
          bm.codec = "rgba8";
          bm.bytes.resize(static_cast<size_t>(bm.width * bm.height * 4));
          for (int y = 0; y < bm.height; ++y) {
            std::memcpy(bm.bytes.data() + static_cast<size_t>(y * bm.width * 4),
                        rgba.constScanLine(y), static_cast<size_t>(bm.width * 4));
          }
          on_cell(index, std::move(bm));
        });
  };
  m_source = std::make_unique<ThumtooTileSource>(std::string{"path"}, std::move(fetch));
  m_session = std::make_unique<TileSession>(m_source.get());
}

void TileLodController::setContentSize(int w, int h)
{
  ensureSource();
  if (m_session) {
    m_session->set_content_size(w, h);
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

void TileLodController::tick(int requestBudget)
{
  if (!m_session || !m_enabled) {
    return;
  }
  m_session->pump();
  m_session->issue_requests(requestBudget);
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

int TileLodController::targetScale() const
{
  return m_session ? m_session->target_scale() : 0;
}

}  // namespace tilelod
