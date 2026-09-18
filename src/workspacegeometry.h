// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WORKSPACEGEOMETRY_H
#define WORKSPACEGEOMETRY_H

#include <QSizeF>
#include <QtGlobal>

/**
 * Pure workspace placement / scene-margin policy (no ImageView state).
 */
namespace WorkspaceGeometry {

/** Scene-rect halo margin from a viewport axis length. */
inline qreal sceneMargin(qreal viewSceneAxis, qreal floorPx = 96.0, qreal frac = 0.35)
{
    return qMax(floorPx, viewSceneAxis * frac);
}

/** Cap collision footprint so huge items still leave room nearby. */
inline qreal placementMaxEdge(qreal viewW, qreal viewH, qreal floorPx = 120.0,
                              qreal frac = 0.45)
{
    return qMax(floorPx, qMin(viewW, viewH) * frac);
}

/** Scale @p size so its long edge ≤ @p maxEdge (identity when already small). */
inline QSizeF cappedFootprint(QSizeF size, qreal maxEdge)
{
    const qreal longest = qMax(size.width(), size.height());
    if (longest > maxEdge && longest > 1e-9) {
        const qreal f = maxEdge / longest;
        return QSizeF(size.width() * f, size.height() * f);
    }
    return size;
}

} // namespace WorkspaceGeometry

#endif // WORKSPACEGEOMETRY_H
