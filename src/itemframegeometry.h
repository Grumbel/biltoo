// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMFRAMEGEOMETRY_H
#define ITEMFRAMEGEOMETRY_H

#include <QPointF>

/**
 * Pure viewport-space geometry of a (possibly rotated) content frame:
 * corners, edge mids, unit edge directions, and outward normals.
 * Used by ImageItem chrome layout / hit-test. No item or view state.
 */
namespace ItemFrameGeometry {

struct FrameViewGeom {
    QPointF tl, tr, br, bl, center;
    QPointF midTop, midRight, midBottom, midLeft;
    QPointF dirTop, dirRight, dirBottom, dirLeft;
    QPointF outTop, outRight, outBottom, outLeft;
};

/** Unit vector of @p v, or @p fallback when near-zero length. */
QPointF unitOr(const QPointF &v, const QPointF &fallback = QPointF(1, 0));

/**
 * Build frame geometry from the four content corners in the same space
 * (typically viewport pixels after mapFromScene).
 */
FrameViewGeom makeFrameViewGeom(const QPointF &tl, const QPointF &tr,
                                const QPointF &br, const QPointF &bl);

} // namespace ItemFrameGeometry

#endif // ITEMFRAMEGEOMETRY_H
