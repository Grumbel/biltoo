// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_LOD_CONTROLLER_HPP
#define BILTOO_TILELOD_TILE_LOD_CONTROLLER_HPP

#include "tilelod/tile_lod_registry.hpp"
#include "tilelod/tile_session.hpp"
#include "tilelod/tile_painter.hpp"

#include <QImage>
#include <QRectF>
#include <QString>
#include <memory>

namespace tilelod {

/**
 * Host glue for one ImageItem: shared path cache + private viewport session.
 * Call from GUI thread: setPath, setContentSize, updateViewport, tick, paint.
 */
class TileLodController {
public:
  TileLodController();
  ~TileLodController();

  TileLodController(TileLodController const&) = delete;
  TileLodController& operator=(TileLodController const&) = delete;

  void setPath(QString path);
  QString path() const { return m_path; }

  void setContentSize(int w, int h);
  void setHasLqip(bool on);

  /**
   * @param contentVisible region in content (scale-0) pixels visible on screen
   * @param devicePerContent screen pixels per content pixel
   */
  void updateViewport(QRectF const& contentVisible, double devicePerContent,
                      double marginContent = 0.0);

  /** pump completions + issue up to budget requests. @return completions applied. */
  int tick(int requestBudget = 8);

  /** Paint tiles into content space; returns true if any tile was drawn. */
  bool paint(QPainter* painter, QImage const& lqipUnderlay = {}) const;

  bool enabled() const { return m_enabled; }
  void setEnabled(bool on) { m_enabled = on; }

  bool hasAnyTile() const;
  int targetScale() const;
  TileSession* session() { return m_session.get(); }
  TileSession const* session() const { return m_session.get(); }

  /**
   * When device density is high enough that soft ≤512 is insufficient,
   * tiles should own the display path.
   */
  static bool shouldUseTiles(double devicePerContent, int contentLongEdge);

private:
  void bindSession();
  void unbind();

  QString m_path;
  bool m_enabled = true;
  std::shared_ptr<SharedPathTiles> m_shared;
  std::unique_ptr<TileSession> m_session;
};

}  // namespace tilelod

#endif
