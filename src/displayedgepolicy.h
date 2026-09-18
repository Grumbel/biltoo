// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYEDGEPOLICY_H
#define DISPLAYEDGEPOLICY_H

#include <QImage>

/**
 * Pure long-edge coverage and ladder-cap rules for PreferCache / soft delivery.
 * ImageView supplies known native sizes; this module does not touch caches.
 */
namespace DisplayEdgePolicy {

/** ~90% of target long edge counts as delivered (soft/overview stops). */
bool coversEdge(int haveLongEdge, int targetEdge);

/**
 * Cap @p wantEdge to the image ladder, then to known native long edge, with
 * ceil-ladder snap that does not exceed native again.
 * @p nativeLongEdge ≤ 0 means native unknown (no native clamp).
 */
int cappedDisplayEdge(int wantEdge, int nativeLongEdge);

/**
 * Whether @p sampleLongEdge is enough to treat as covering native logical size.
 * Overview band (≤ @p overviewEdge) never final; unknown native needs
 * ≥ @p imageLadderEdge; known native uses coversEdge.
 */
bool sampleCoversNative(int sampleLongEdge, int nativeLongEdge, bool nativeKnown,
                        int overviewEdge, int imageLadderEdge);

/**
 * Gallery soft paint budget: shrink attached soft when the cell needs far less
 * than the sample. Full sample remains in ImageCache for zoom-in.
 */
QImage clampSoftForCell(const QImage &pixels, int needEdge, int minEdge);

/**
 * Ladder-snapped on-screen long-edge need from device-pixel span.
 * When @p allowHighRes is false, clamps to the soft gallery ladder edge.
 */
int needEdgeFromScreenLongPx(qreal longPx, bool allowHighRes);

} // namespace DisplayEdgePolicy

#endif // DISPLAYEDGEPOLICY_H
