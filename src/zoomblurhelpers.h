// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZOOMBLURHELPERS_H
#define ZOOMBLURHELPERS_H

#include "slideshowtypes.h"

#include <QString>
#include <QtGlobal>
#include <QImage>

/**
 * Pure ZoomBlur slot-book helpers (no QObject, no QThreadPool).
 * Async build scheduling stays on ImageView.
 */
namespace ZoomBlur {

/** Minimum edge for the low-res blur workspace. */
constexpr int kWorkMinEdge = 8;
/** Cap long edge of the blur workspace (CPU budget). */
constexpr int kWorkMaxLongEdge = 128;
/** Viewport divisor for initial work size (vw/16, vh/16). */
constexpr int kWorkViewportDiv = 16;

/** Low-res work size for cover-blur underlay (min edge, max long edge). */
inline void workSize(int vw, int vh, int *workW, int *workH)
{
    if (!workW || !workH) {
        return;
    }
    int w = qMax(kWorkMinEdge, vw / kWorkViewportDiv);
    int h = qMax(kWorkMinEdge, vh / kWorkViewportDiv);
    const int longEdge = qMax(w, h);
    if (longEdge > kWorkMaxLongEdge) {
        const qreal s = qreal(kWorkMaxLongEdge) / qreal(longEdge);
        w = qMax(kWorkMinEdge, int(w * s));
        h = qMax(kWorkMinEdge, int(h * s));
    }
    *workW = w;
    *workH = h;
}

/** Stable slot key for path + viewport size. Empty path or non-positive size → 0. */
qint64 key(const QString &path, int vw, int vh);

bool keyCached(const SlideshowZoomBlurState &st, qint64 key);
bool keyInFlight(const SlideshowZoomBlurState &st, qint64 key);

/**
 * Claim an in-flight slot for @p key under the current generation.
 * @return slot index 0/1, or -1 if both slots are busy with other keys.
 */
int claimFlightSlot(SlideshowZoomBlurState *st, qint64 key);

/**
 * Drop underlays and in-flight markers whose keys are neither keepA nor keepB.
 * Does not bump generation (in-flight work for the kept pair continues).
 */
void pruneOutsidePair(SlideshowZoomBlurState *st, qint64 keepA, qint64 keepB);

/** Clear sized underlays; leave lastGood for paint stretch. */
void clearSizedSlots(SlideshowZoomBlurState *st);

/** Clear sized underlays and lastGood stretch fallback. */
void clearAllSlots(SlideshowZoomBlurState *st);

/** Bump generation and clear in-flight markers (cancel pending jobs). */
void invalidateQueue(SlideshowZoomBlurState *st);

/**
 * Install a finished blur into a slot matching @p key (or first empty).
 * @return false if generation is stale.
 */
bool installResult(SlideshowZoomBlurState *st, const QPixmap &blurred, qint64 key,
                   quint64 gen);

/** Cover-scale + box-blur underlay pixels (worker-thread safe). */
QImage makeCover(const QImage &src, int vw, int vh);

} // namespace ZoomBlur

#endif // ZOOMBLURHELPERS_H
