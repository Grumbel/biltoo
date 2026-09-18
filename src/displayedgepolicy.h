// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYEDGEPOLICY_H
#define DISPLAYEDGEPOLICY_H

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

} // namespace DisplayEdgePolicy

#endif // DISPLAYEDGEPOLICY_H
