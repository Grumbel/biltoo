// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYPACKFIT_H
#define GALLERYPACKFIT_H

#include "gallerylayout.h"
#include "imageview_types.h"

#include <QRectF>
#include <QtGlobal>

/**
 * Pure post-pack fit: correct sub-pixel overshoot so sceneRect does not
 * force dual scrollbars on the fitted axis (see GalleryLayout::pack).
 */
namespace GalleryPackFit {

/** Map ImageView LayoutMode to GalleryLayout pack mode (FreeForm → Masonry). */
inline GalleryLayout::Mode modeFromLayoutMode(LayoutMode mode)
{
    switch (mode) {
    case LayoutMode::SideBySide:
        return GalleryLayout::Mode::SideBySide;
    case LayoutMode::Vertical:
        return GalleryLayout::Mode::Vertical;
    case LayoutMode::Grid:
        return GalleryLayout::Mode::Grid;
    case LayoutMode::GridCrop:
        return GalleryLayout::Mode::GridCrop;
    case LayoutMode::Masonry:
        return GalleryLayout::Mode::Masonry;
    case LayoutMode::MasonryRows:
        return GalleryLayout::Mode::MasonryRows;
    case LayoutMode::MasonryFill:
        return GalleryLayout::Mode::MasonryFill;
    case LayoutMode::MasonryRowsFill:
        return GalleryLayout::Mode::MasonryRowsFill;
    case LayoutMode::Flow:
        return GalleryLayout::Mode::Flow;
    case LayoutMode::FlowFill:
        return GalleryLayout::Mode::FlowFill;
    case LayoutMode::Facing:
        return GalleryLayout::Mode::Facing;
    case LayoutMode::FreeForm:
    default:
        return GalleryLayout::Mode::Masonry;
    }
}

/** Viewport CSS overscan for gallery soft-decode window. */
constexpr int kDecodeOverscanPx = 400;

/** Usable pack axis length after margin (min @p floor). */
inline qreal packAvailAxis(int viewportAxis, qreal margin, qreal floor = 32.0)
{
    return qMax(floor, qreal(viewportAxis) - 2.0 * margin);
}

/**
 * Fitted-axis targets for @p mode (-1 = unconstrained).
 * Height-fitted: SideBySide / MasonryRows*; width-fitted: the rest of pack modes.
 */
inline void fittedTargets(GalleryLayout::Mode mode, qreal availW, qreal availH,
                          qreal *targetW, qreal *targetH)
{
    if (targetW) {
        *targetW = -1.0;
    }
    if (targetH) {
        *targetH = -1.0;
    }
    switch (mode) {
    case GalleryLayout::Mode::SideBySide:
    case GalleryLayout::Mode::MasonryRows:
    case GalleryLayout::Mode::MasonryRowsFill:
        if (targetH) {
            *targetH = availH;
        }
        break;
    case GalleryLayout::Mode::Vertical:
    case GalleryLayout::Mode::Grid:
    case GalleryLayout::Mode::GridCrop:
    case GalleryLayout::Mode::Masonry:
    case GalleryLayout::Mode::MasonryFill:
    case GalleryLayout::Mode::Flow:
    case GalleryLayout::Mode::FlowFill:
    case GalleryLayout::Mode::Facing:
        if (targetW) {
            *targetW = availW;
        }
        break;
    }
}

/**
 * Display size from pack layout size and placement scales (uniform when
 * @p scaleY ≤ 0). Pure — no ImageItem.
 */
inline QSizeF scaledDisplaySize(const QSizeF &layoutSize, qreal scale, qreal scaleY = 0.0)
{
    if (layoutSize.isEmpty()) {
        return {};
    }
    const qreal sy = scaleY > 0.0 ? scaleY : scale;
    return QSizeF(layoutSize.width() * scale, layoutSize.height() * sy);
}

/**
 * Axis-aligned bounds of a tile centered at @p center with display size @p sz.
 * Matches Gallery pack placement (pos is centre).
 */
inline QRectF centeredTileBounds(const QPointF &center, const QSizeF &sz)
{
    if (sz.isEmpty()) {
        return {};
    }
    return QRectF(center.x() - sz.width() / 2.0, center.y() - sz.height() / 2.0,
                  sz.width(), sz.height());
}

/**
 * Uniform scale ≤ 1 about the pack origin so content fits the target box.
 * Returns 1.0 when there is no overshoot beyond @p epsilon.
 */
inline qreal overshootUniformScale(const QRectF &content, qreal targetW, qreal targetH,
                                   qreal epsilon = 1e-4)
{
    if (content.isEmpty()) {
        return 1.0;
    }
    qreal s = 1.0;
    if (targetW > 0.0 && content.width() > targetW + epsilon) {
        s = qMin(s, targetW / content.width());
    }
    if (targetH > 0.0 && content.height() > targetH + epsilon) {
        s = qMin(s, targetH / content.height());
    }
    return s;
}

} // namespace GalleryPackFit

#endif // GALLERYPACKFIT_H
