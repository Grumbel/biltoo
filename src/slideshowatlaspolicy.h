// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWATLASPOLICY_H
#define SLIDESHOWATLASPOLICY_H

#include "slideshowtypes.h"

#include <QImage>
#include <QPixmap>

/**
 * Pure policy for whether an existing dwell / phase motion atlas still covers
 * the current source sample under the viewport budget.
 *
 * ImageView supplies DwellAtlasParams (viewport × headroom) and schedules
 * rebuilds; this module only answers coverage.
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

} // namespace SlideshowAtlasPolicy

#endif // SLIDESHOWATLASPOLICY_H
