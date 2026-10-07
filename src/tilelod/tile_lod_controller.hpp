// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_LOD_CONTROLLER_HPP
#define BILTOO_TILELOD_TILE_LOD_CONTROLLER_HPP

#include "tilelod/tile_lod_registry.hpp"
#include "tilelod/core.hpp"
#include <thumtoo/lod/tile_session.hpp>
#include "tilelod/tile_painter.hpp"

#include <QImage>
#include <QRectF>
#include <QString>
#include <functional>
#include <memory>
#include <string>

namespace tilelod {

/**
 * Host glue for one view surface (ImageItem, filmstrip cell, slideshow phase,
 * neighbour prefetch, Gallery peek): binds a TileSession to the process-wide
 * path loader. GUI thread only.
 *
 * Loading is driven by TileScheduler (event-driven); this class only plans,
 * publishes demand and paints. `refresh()` renews the demand lease while the
 * view is visible. Cell changes call the `onChange` hook (repaint).
 *
 * setPath / destroy release registry interest only; Ready tiles for the path
 * stay in TileLodRegistry until global LRU eviction (see TILE_LOD.md).
 */
class TileLodController {
public:
  /// Issue priority classes (TileSession::set_priority_class).
  enum Priority : int {
    kPrioritySpeculative = 0,  ///< neighbour prefetch
    kPriorityStrip = 1,        ///< filmstrip cells
    kPriorityVisible = 2,      ///< Gallery / Workspace items in view
    kPriorityFocus = 3,        ///< Image mode / slideshow
  };

  TileLodController();
  ~TileLodController();

  TileLodController(TileLodController const&) = delete;
  TileLodController& operator=(TileLodController const&) = delete;

  void setPath(QString path);
  QString path() const { return m_path; }

  /// Repaint hook (GUI thread), re-installed on every path bind.
  void setOnChange(std::function<void()> cb);
  void setPriority(int cls);
  /// Paint-only: plan and draw, never publish demand.
  void setPassive(bool on);

  void setContentSize(int w, int h, int minScale = 0);
  void setHasLqip(bool on);

  /**
   * @param contentVisible region in content (scale-0) pixels visible on screen
   * @param devicePerContent screen pixels per content pixel
   */
  void updateViewport(QRectF const& contentVisible, double devicePerContent,
                      double marginContent = 0.0);

  /// Keep demand alive while visible; returns true while still loading.
  bool refresh();

  /** Paint tiles into content space; returns true if any tile was drawn. */
  bool paint(QPainter* painter, QImage const& lqipUnderlay = {}) const;

  bool hasAnyTile() const;
  /** Ready tiles in the shared path loader (retained from earlier views). */
  bool hasRetainedTiles() const;
  bool viewportFullyCovered() const;
  /** Nothing left that will change without user action (not Loading). */
  bool viewportSettled() const;
  bool isLoading() const;
  TileSession::Phase phase() const;
  /** "" while fine; the failure reason when Degraded / Error. */
  std::string statusLine() const;
  int targetScale() const;
  TileSession* session() { return m_session.get(); }
  TileSession const* session() const { return m_session.get(); }

  /**
   * When the file is large enough for a pyramid and on-screen footprint is
   * meaningful (~32px+), tiles own display. Soft/HOST whole-frame is underlay only.
   */
  static bool shouldUseTiles(double devicePerContent, int contentLongEdge);

private:
  void bindSession();
  void unbind();

  QString m_path;
  double m_device_per_content = 0.0;
  int m_priority = kPriorityVisible;
  bool m_passive = false;
  std::function<void()> m_on_change;
  std::shared_ptr<SharedPathTiles> m_shared;
  std::unique_ptr<TileSession> m_session;
};

}  // namespace tilelod

#endif
