// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILMSTRIPGEOMETRY_H
#define FILMSTRIPGEOMETRY_H

#include <QSize>
#include <QtGlobal>

/**
 * Pure filmstrip / ThumbnailBar layout helpers (no QListView state).
 * See docs/FILMSTRIP_LAYOUT.md for letterbox vs square cell policy.
 */
namespace FilmstripGeometry {

/** Dog-ear fold for Workspace membership badge (viewport CSS px). */
inline int membershipFoldPx(int contentWidth)
{
    return qBound(10, contentWidth / 4, 28);
}

/** Cell pad from logical thumb size (edge margin). */
inline int cellPadFromThumb(int thumbSize)
{
    return qBound(1, thumbSize / 20, 6);
}

/** Alternate pad when sizing from bar extent. */
inline int cellPadFromThumbAlt(int thumbSize)
{
    return qBound(1, thumbSize / 16, 6);
}

/** Clamp logical thumb size to product range. */
inline int clampThumbSize(int pixels, int minPx = 48, int maxPx = 1024)
{
    return qBound(minPx, pixels, maxPx);
}

/**
 * Letterbox content size: cross-axis = @p thumbSize, other axis from aspect.
 * @p horizontalBar true when the strip scrolls along X (height fixed).
 */
inline QSize letterboxContentSize(int thumbSize, QSize aspect, bool horizontalBar)
{
    if (aspect.width() < 1 || aspect.height() < 1) {
        return QSize(thumbSize, thumbSize);
    }
    if (horizontalBar) {
        const int h = thumbSize;
        const int w = qMax(1, int(qRound(qreal(thumbSize) * qreal(aspect.width())
                                         / qreal(aspect.height()))));
        return QSize(w, h);
    }
    const int w = thumbSize;
    const int h = qMax(1, int(qRound(qreal(thumbSize) * qreal(aspect.height())
                                     / qreal(aspect.width()))));
    return QSize(w, h);
}

/** Fit content size inside an inner cell (at least 1×1). */
inline QSize fitContentInInner(QSize contentSz, QSize inner)
{
    return QSize(qBound(1, contentSz.width(), inner.width()),
                 qBound(1, contentSz.height(), inner.height()));
}

/** Selection/corner badge size from slot. */
inline int cornerBadgeSize(int slotW, int slotH)
{
    return qBound(6, qMin(slotW, slotH) / 4, 18);
}

} // namespace FilmstripGeometry

#endif // FILMSTRIPGEOMETRY_H
