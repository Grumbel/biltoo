// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYEDGEPOLICY_H
#define DISPLAYEDGEPOLICY_H

#include <QImage>
#include <QSize>
#include <QtGlobal>

/**
 * Pure long-edge coverage and ladder-cap rules for PreferCache / soft delivery.
 * ImageView supplies known native sizes; this module does not touch caches.
 */
namespace DisplayEdgePolicy {

/** Raise climb floor to on-screen need when need > 0. */
inline int escalateClimbTo(int baseEdge, int needEdge)
{
    return needEdge > 0 ? qMax(baseEdge, needEdge) : baseEdge;
}

/** Bind/tile want count: at least one pending bind, else have. */
inline int wantedBindCount(int have, int pendingBinds)
{
    return qMax(have, pendingBinds > 0 ? pendingBinds : 1);
}

/** Cap tile-synth request to overview ladder edge. */
inline int tileSynthEdge(int qualityEdge, int overviewEdge)
{
    return qMin(qualityEdge, overviewEdge);
}

/**
 * True when have fully meets target long edge (have >= target).
 * No fuzzy ratio — PreferCache settle is terminal per request edge elsewhere;
 * coverage for climb/display is strict so policy stays deterministic.
 */
bool coversEdge(int haveLongEdge, int targetEdge);

/**
 * Cap @p wantEdge to the image ladder, then to known native long edge, with
 * ceil-ladder snap that does not exceed native again.
 * @p nativeLongEdge ≤ 0 means native unknown (no native clamp).
 */
int cappedDisplayEdge(int wantEdge, int nativeLongEdge);

/** Native long edge from size; 0 if invalid. */
inline int nativeLongEdge(const QSize &knownNative)
{
    if (!knownNative.isValid() || knownNative.width() <= 0 || knownNative.height() <= 0) {
        return 0;
    }
    return qMax(knownNative.width(), knownNative.height());
}

/** Same as above; native long edge from size (0 if invalid). */
inline int cappedDisplayEdge(int wantEdge, const QSize &knownNative)
{
    return cappedDisplayEdge(wantEdge, nativeLongEdge(knownNative));
}

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

/** Coarse quality tier for HUD labels (caller translates). */
enum class QualityTier {
    Loading = 0,
    Placeholder,
    QuickPreview,
    Thumbnail,
    Preview,
    HighQuality,
    FullResolution,
};

/**
 * Classify on-screen long edge vs known native.
 * @p nativeLongEdge ≤ 0 means native unknown (no FullResolution).
 * Thresholds: overview / gallery / filmstrip / LQIP max from ThumtooCache constants
 * are passed in so this stays free of cache includes in the header.
 */
QualityTier classifyQualityTier(int displayLongEdge, int nativeLongEdge,
                                 bool hasDecodedPixels, int overviewEdge,
                                 int galleryEdge, int filmstripEdge,
                                 int lqipMaxEdge);

} // namespace DisplayEdgePolicy

#endif // DISPLAYEDGEPOLICY_H
