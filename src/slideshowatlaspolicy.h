// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWATLASPOLICY_H
#define SLIDESHOWATLASPOLICY_H

#include "slideshowtypes.h"

#include <QImage>
#include <QPixmap>
#include <QSize>

/**
 * Pure slideshow atlas / sample-edge policy.
 *
 * ImageView supplies viewport size, DPR, and settings; schedules rebuilds and
 * decode climbs. This module answers coverage, headroom, and edge budgets.
 */
namespace SlideshowAtlasPolicy {

/**
 * True when @p atlas is still adequate for @p source under @p params.
 *
 * - Requires matching keyScale / viewport size and atlas long edge near longCap.
 * - Source at/above longCap: atlas must fill the budget.
 * - Soft-band sources (≤ ladder edge): keep soft-upscaled atlas (avoid thrash).
 * - Mid PreferCache samples while atlas is a soft upsample: force rebuild.
 */
bool coversSource(const QPixmap &atlas, qreal atlasScale, int atlasVw, int atlasVh,
                  const DwellAtlasParams &params, const QImage &source);

/**
 * Ken Burns / pan-scan sample headroom past 1:1 cover.
 * Off or inactive → 1.0; PanZoom uses clamped panZoomFactor; PanScan → 1.25.
 */
qreal motionHeadroom(SlideshowMotion motion, qreal panZoomFactor,
                     bool progressActive);

/**
 * Fraction of target edge treated as "good enough" for phase samples (7/10).
 */
int needEdge(int targetEdge);

/**
 * Viewport long-edge pixel budget: max(vw,vh) × dpr × headroom, ladder-snapped,
 * clamped to the overview band (never Full native PreferCache).
 * When @p viewportValid is false, returns the soft ladder edge.
 */
int targetLongEdge(bool viewportValid, int viewportW, int viewportH, qreal dpr,
                   qreal headroom);

/**
 * Fill DwellAtlasParams from viewport CSS pixels and headroom (no QWidget).
 * longCap uses CSS viewport × headroom (atlas texture budget, not DPR).
 */
DwellAtlasParams makeParams(int viewportW, int viewportH, qreal headroom);

} // namespace SlideshowAtlasPolicy

#endif // SLIDESHOWATLASPOLICY_H
