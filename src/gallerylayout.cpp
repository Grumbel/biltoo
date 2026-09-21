// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerylayout.h"
#include "viewtransform.h"
#include "gallerypackfit.h"
#include "imageitem.h"
#include "itemcomponents.h"

#include <QtMath>
#include <cmath>
#include <QRectF>
#include <QVector>

namespace GalleryLayout {

void setItemGalleryCellSize(ImageItem *item, const QSizeF &sceneSize)
{
    if (!item) {
        return;
    }
    item->setGalleryCellSize(sceneSize);
}

void applyItemPlacement(ImageItem *item, const ItemComponents::Placement &pl)
{
    if (!item) {
        return;
    }
    item->applyPlacement(pl);
}

namespace {

QSizeF nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    // Logical size (intrinsic) only — soft samples never define pack geometry.
    return QSizeF(item->imageSize());
}

/** Pixmap size with 90°-class rotation applied (for packing aspect ratio). */
QSizeF layoutSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    return layoutSizeForNative(nativeSize(item), item->placement().rotation);
}

/** Uniform pack pose: center + scale, shear cleared; rotation/flips/z preserved. */
void applyPackPose(ImageItem *item, const QPointF &center, qreal scale)
{
    if (!item) {
        return;
    }
    ItemComponents::Placement pl = item->placement();
    pl.pos = center;
    pl.scale = scale;
    pl.scaleY = scale;
    pl.shear = 0.0;
    pl.opacity = 1.0;
    applyItemPlacement(item, pl);
}

void finish(ImageItem *item, const std::function<void(ImageItem *)> &afterEach)
{
    if (afterEach) {
        afterEach(item);
    }
}

} // namespace

void pack(const QList<ImageItem *> &items, const Params &params,
          const std::function<void(ImageItem *)> &afterEach)
{
    if (items.isEmpty()) {
        return;
    }

    const qreal margin = params.margin;
    const qreal gap = params.gap;
    const qreal availW = params.availW;
    const qreal availH = params.availH;
    const int n = items.size();

    // Full opacity for the overview; keep intentional rotate/flip from Gallery
    // (or Image). Residual free-form angles are cleared when *entering* Gallery
    // from Workspace — pack itself must not wipe user transforms.
    for (ImageItem *item : items) {
        // applyPackPose also forces opacity 1; set early so layoutSize reads match.
        ItemComponents::Placement pl = item->placement();
        pl.opacity = 1.0;
        applyItemPlacement(item, pl);
        if (params.mode != Mode::GridCrop) {
            setItemGalleryCellSize(item, {});
        }
    }

    QVector<QSizeF> sizes;
    sizes.reserve(n);
    for (ImageItem *item : items) {
        sizes.append(layoutSize(item));
    }

    QVector<PackPose> poses;
    switch (params.mode) {
    case Mode::SideBySide:
        poses = packPosesSideBySide(sizes, margin, gap, availH);
        break;
    case Mode::Vertical:
        poses = packPosesVertical(sizes, margin, gap, availW);
        break;
    case Mode::Grid:
        poses = packPosesGrid(sizes, margin, gap, availW, params.gridColumns);
        break;
    case Mode::GridCrop:
        poses = packPosesGridCrop(sizes, margin, gap, availW, params.gridColumns);
        break;
    case Mode::Masonry:
        poses = packPosesMasonry(sizes, margin, gap, availW, params.masonryColumns);
        break;
    case Mode::MasonryRows:
        poses = packPosesMasonryRows(sizes, margin, gap, availH, params.masonryRows);
        break;
    case Mode::MasonryFill:
        poses = packPosesMasonryFill(sizes, margin, gap, availW, params.masonryColumns);
        break;
    case Mode::MasonryRowsFill:
        poses = packPosesMasonryRowsFill(sizes, margin, gap, availH, params.masonryRows);
        break;
    case Mode::Flow:
    case Mode::FlowFill:
        poses = packPosesFlow(sizes, margin, gap, availW, params.gridColumns,
                              params.mode == Mode::FlowFill);
        break;
    case Mode::Facing:
        poses = packPosesFacing(sizes, margin, gap, availW, availH);
        break;
    }

    for (int i = 0; i < n; ++i) {
        ImageItem *item = items.at(i);
        if (!item || i >= poses.size()) {
            continue;
        }
        if (!poses.at(i).cellSize.isEmpty()) {
            setItemGalleryCellSize(item, poses.at(i).cellSize);
        }
        applyPackPose(item, poses.at(i).center, poses.at(i).scale);
        finish(item, afterEach);
    }

        // Floating-point packing can leave the fitted axis a fraction of a pixel
    // over target → dual scrollbars. GalleryPackFit corrects uniform overshoot.
    qreal targetW = -1.0;
    qreal targetH = -1.0;
    GalleryPackFit::fittedTargets(params.mode, availW, availH, &targetW, &targetH);
    if (targetW > 0.0 || targetH > 0.0) {
        QRectF content;
        for (ImageItem *item : items) {
            if (!item) {
                continue;
            }
            QSizeF sz = item->galleryCellSize();
            if (sz.isEmpty()) {
                const ItemComponents::Placement pl = item->placement();
                sz = GalleryPackFit::scaledDisplaySize(layoutSize(item), pl.scale, pl.scaleY);
            }
            if (sz.isEmpty()) {
                continue;
            }
            content = content.united(
                GalleryPackFit::centeredTileBounds(item->pos(), sz));
        }
        const qreal s = GalleryPackFit::overshootUniformScale(content, targetW, targetH);
        if (s < 1.0) {
            const QPointF origin(margin, margin);
            for (ImageItem *item : items) {
                if (!item) {
                    continue;
                }
                ItemComponents::Placement pl = item->placement();
                pl.pos = origin + (pl.pos - origin) * s;
                pl.scale *= s;
                pl.scaleY *= s;
                applyItemPlacement(item, pl);
                if (!item->galleryCellSize().isEmpty()) {
                    const QSizeF cs = item->galleryCellSize();
                    setItemGalleryCellSize(item, QSizeF(cs.width() * s, cs.height() * s));
                }
            }
            if (afterEach) {
                for (ImageItem *item : items) {
                    if (item) {
                        afterEach(item);
                    }
                }
            }
        }
    }

}

} // namespace GalleryLayout
