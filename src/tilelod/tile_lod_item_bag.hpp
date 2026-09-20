// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_TILE_LOD_ITEM_BAG_HPP
#define BILTOO_TILELOD_TILE_LOD_ITEM_BAG_HPP

/**
 * Per-ImageItem tile LOD runtime state (Stage 2 demotion prep).
 *
 * Owns the TileLodController plus paint/plan scratch that used to be scattered
 * on ImageItem. The bag can later move under DisplayPipelineController keyed by
 * item/path without changing the field layout.
 */

#include "tilelod/tile_lod_controller.hpp"

#include <QCache>
#include <QImage>
#include <QRectF>
#include <cstdint>
#include <memory>

namespace tilelod {

struct ItemBag {
  std::unique_ptr<TileLodController> controller;
  /** Crop-draft (and similar) freeze: no tile requests or paint. */
  bool suppressed = false;
  bool repaintQueued = false;
  /** false after destruction — pending tickTileLod singleShot must not touch item. */
  std::shared_ptr<bool> alive{std::make_shared<bool>(true)};
  double lastDpc = -1.0;
  QRectF lastVisSource;
  /** Last tile plan generation that triggered update() (avoid 250ms repaint spam). */
  std::uint64_t lastUpdateGen = 0;
  /** QImage cells for paint; cost ≈ KiB of rgba. Evicts LRU instead of full clear. */
  mutable QCache<quint64, QImage> gradedCache;
  mutable quint64 gradeSig = 0;

  /** Drop controller + plan/paint scratch; leave suppressed unchanged. */
  void resetSession()
  {
    if (alive) {
      *alive = false;
    }
    alive = std::make_shared<bool>(true);
    controller.reset();
    lastUpdateGen = 0;
    repaintQueued = false;
    lastDpc = -1.0;
    lastVisSource = QRectF();
    gradedCache.clear();
    gradeSig = 0;
  }
};

}  // namespace tilelod

#endif
