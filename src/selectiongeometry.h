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

} // namespace SelectionGeometry

#endif // SELECTIONGEOMETRY_H
