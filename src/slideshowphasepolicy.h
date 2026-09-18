// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWPHASEPOLICY_H
#define SLIDESHOWPHASEPOLICY_H

#include <QImage>
#include <QString>

/**
 * Pure policy for whether a sharper / content-oriented sample should replace
 * a slideshow phase buffer. ImageView supplies appearance presence.
 */
namespace SlideshowPhasePolicy {

/**
 * True when @p sampleEdge should replace @p phaseImg for the matching path.
 *
 * - Larger long-edge always wins.
 * - Same edge wins only when ContentXform is still pending (@p contentApplied
 *   is false and @p hasPendingContentAppearance is true).
 * - Smaller samples never replace.
 */
bool bufferWantsSample(const QString &phasePath, const QImage &phaseImg,
                       bool contentApplied, const QString &incomingPath,
                       int sampleEdge, bool hasPendingContentAppearance);

/** Long edge of a raster (0 if null). */
int longEdge(const QImage &img);

} // namespace SlideshowPhasePolicy

#endif // SLIDESHOWPHASEPOLICY_H
