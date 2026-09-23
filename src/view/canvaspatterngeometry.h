// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CANVASPATTERNGEOMETRY_H
#define CANVASPATTERNGEOMETRY_H

#include <QtGlobal>
#include <cmath>

/**
 * Pure checker / image-tile cell sizing for canvas background fill.
 * Keeps on-screen cell size above a minimum while zooming out.
 */
namespace CanvasPatternGeometry {

/** Checker cell in scene units so cell*viewScale ≥ minScreenPx (cap 4096). */
inline qreal checkerCellScene(qreal viewScale, qreal baseCell = 16.0,
                              qreal minScreenPx = 16.0, qreal maxCell = 4096.0)
{
    viewScale = qMax(1e-6, viewScale);
    qreal cell = baseCell;
    while (cell * viewScale < minScreenPx && cell < maxCell) {
        cell *= 2.0;
    }
    return cell;
}

/**
 * Integer LOD multiplier for image tiles so tile*lod*viewScale ≥ minScreenPx
 * (cap lod 64).
 */
inline qreal tileLodFactor(qreal tileWidth, qreal viewScale,
                           qreal minScreenPx = 24.0, qreal maxLod = 64.0)
{
    viewScale = qMax(1e-6, viewScale);
    tileWidth = qMax(1.0, tileWidth);
    qreal lod = 1.0;
    while (tileWidth * lod * viewScale < minScreenPx && lod < maxLod) {
        lod *= 2.0;
    }
    return lod;
}

} // namespace CanvasPatternGeometry

#endif // CANVASPATTERNGEOMETRY_H
