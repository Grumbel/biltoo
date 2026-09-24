// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SELECTIONGEOMETRY_H
#define SELECTIONGEOMETRY_H

#include <QRectF>
#include <QVector>

/**
 * Pure scene-space bounds helpers for multi-item selection chrome.
 */
namespace SelectionGeometry {

/** Axis-aligned union of content AABBs; empty if none valid. */
inline QRectF unionContentAabbs(const QVector<QRectF> &rects)
{
    QRectF bounds;
    for (const QRectF &r : rects) {
        if (!r.isValid() || r.isEmpty()) {
            continue;
        }
        bounds = bounds.isValid() ? bounds.united(r) : r;
    }
    return bounds;
}

/**
 * Path-item preference for duplicate opens / crop: unique selected match wins,
 * else unique live instance. Ambiguous multi-match → nullptr.
 */
template<typename Item>
Item *preferUniquePathItem(Item *selectedMatch, int selectedMatches,
                           Item *liveOnly, int liveMatches)
{
    if (selectedMatches == 1) {
        return selectedMatch;
    }
    if (liveMatches == 1) {
        return liveOnly;
    }
    return nullptr;
}

} // namespace SelectionGeometry

#endif // SELECTIONGEOMETRY_H
