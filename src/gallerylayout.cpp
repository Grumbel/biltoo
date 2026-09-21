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

    if (params.mode == Mode::SideBySide) {
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses =
            packPosesSideBySide(sizes, margin, gap, availH);
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            if (!item || i >= poses.size()) {
                continue;
            }
            applyPackPose(item, poses.at(i).center, poses.at(i).scale);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Vertical) {
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses =
            packPosesVertical(sizes, margin, gap, availW);
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            if (!item || i >= poses.size()) {
                continue;
            }
            applyPackPose(item, poses.at(i).center, poses.at(i).scale);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Grid) {
        // Width-driven square cells; vertical scroll. Fewer columns → larger tiles.
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses =
            packPosesGrid(sizes, margin, gap, availW, params.gridColumns);
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            if (!item || i >= poses.size()) {
                continue;
            }
            applyPackPose(item, poses.at(i).center, poses.at(i).scale);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::GridCrop) {
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses =
            packPosesGridCrop(sizes, margin, gap, availW, params.gridColumns);
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
    } else if (params.mode == Mode::Masonry) {
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses =
            packPosesMasonry(sizes, margin, gap, availW, params.masonryColumns);
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            if (!item || i >= poses.size()) {
                continue;
            }
            applyPackPose(item, poses.at(i).center, poses.at(i).scale);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::MasonryRows) {
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses =
            packPosesMasonryRows(sizes, margin, gap, availH, params.masonryRows);
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            if (!item || i >= poses.size()) {
                continue;
            }
            applyPackPose(item, poses.at(i).center, poses.at(i).scale);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::MasonryFill) {
        // Column masonry, then scale each column so heights match (clean rectangle).
        const int cols = resolvedBandCount(params.masonryColumns, n);
        const qreal colW = cellAxisLength(availW, gap, cols);
        struct Entry {
            ImageItem *item = nullptr;
            QSizeF ns;
            qreal scale = 1.0;
            qreal h = 0.0;
        };
        QVector<QVector<Entry>> columns(cols);
        QVector<qreal> colHeights(cols, 0.0);
        for (ImageItem *item : items) {
            const QSizeF ns = layoutSize(item);
            const qreal scale = axisFillScale(colW, ns.width());
            const qreal h = ns.height() * scale;
            int best = 0;
            for (int c = 1; c < cols; ++c) {
                if (colHeights.at(c) < colHeights.at(best)) {
                    best = c;
                }
            }
            columns[best].append(Entry{item, ns, scale, h});
            colHeights[best] += h + gap;
        }
        qreal maxH = 0.0;
        for (int c = 0; c < cols; ++c) {
            if (columns.at(c).isEmpty()) {
                continue;
            }
            // Strip trailing gap from height sum.
            const qreal h = colHeights.at(c) - gap;
            maxH = qMax(maxH, h);
        }
        qreal x = margin;
        for (int c = 0; c < cols; ++c) {
            if (columns.at(c).isEmpty()) {
                continue;
            }
            const qreal colH = colHeights.at(c) - gap;
            const qreal s = (colH > 1e-6) ? (maxH / colH) : 1.0;
            const qreal colWidth = colW * s;
            qreal y = margin;
            for (Entry &e : columns[c]) {
                const qreal scale = e.scale * s;
                const qreal w = e.ns.width() * scale;
                const qreal h = e.ns.height() * scale;
                applyPackPose(e.item, QPointF(x + w / 2.0, y + h / 2.0), scale);
                y += h + gap * s;
                finish(e.item, afterEach);
            }
            x += colWidth + gap;
        }
    } else if (params.mode == Mode::MasonryRowsFill) {
        // Row masonry, then scale each row so widths match (clean rectangle).
        const int rows = resolvedBandCount(params.masonryRows, n);
        const qreal rowH = cellAxisLength(availH, gap, rows);
        struct Entry {
            ImageItem *item = nullptr;
            QSizeF ns;
            qreal scale = 1.0;
            qreal w = 0.0;
        };
        QVector<QVector<Entry>> rowItems(rows);
        QVector<qreal> rowWidths(rows, 0.0);
        for (ImageItem *item : items) {
            const QSizeF ns = layoutSize(item);
            const qreal scale = axisFillScale(rowH, ns.height());
            const qreal w = ns.width() * scale;
            int best = 0;
            for (int r = 1; r < rows; ++r) {
                if (rowWidths.at(r) < rowWidths.at(best)) {
                    best = r;
                }
            }
            rowItems[best].append(Entry{item, ns, scale, w});
            rowWidths[best] += w + gap;
        }
        qreal maxW = 0.0;
        for (int r = 0; r < rows; ++r) {
            if (rowItems.at(r).isEmpty()) {
                continue;
            }
            maxW = qMax(maxW, rowWidths.at(r) - gap);
        }
        qreal y = margin;
        for (int r = 0; r < rows; ++r) {
            if (rowItems.at(r).isEmpty()) {
                continue;
            }
            const qreal rowW = rowWidths.at(r) - gap;
            const qreal s = (rowW > 1e-6) ? (maxW / rowW) : 1.0;
            const qreal rowHeight = rowH * s;
            qreal x = margin;
            for (Entry &e : rowItems[r]) {
                const qreal scale = e.scale * s;
                const qreal w = e.ns.width() * scale;
                const qreal h = e.ns.height() * scale;
                applyPackPose(e.item, QPointF(x + w / 2.0, y + h / 2.0), scale);
                x += w + gap * s;
                finish(e.item, afterEach);
            }
            y += rowHeight + gap;
        }
    } else if (params.mode == Mode::Flow || params.mode == Mode::FlowFill) {
        // Order-preserving wrap: L→R then T→B. Width budget from columns.
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses = packPosesFlow(
            sizes, margin, gap, availW, params.gridColumns,
            params.mode == Mode::FlowFill);
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            if (!item || i >= poses.size()) {
                continue;
            }
            applyPackPose(item, poses.at(i).center, poses.at(i).scale);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Facing) {
        // Cover alone, then height-matched pairs (verso | recto), stacked.
        QVector<QSizeF> sizes;
        sizes.reserve(n);
        for (ImageItem *item : items) {
            sizes.append(layoutSize(item));
        }
        const QVector<PackPose> poses =
            packPosesFacing(sizes, margin, gap, availW, availH);
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            if (!item || i >= poses.size()) {
                continue;
            }
            applyPackPose(item, poses.at(i).center, poses.at(i).scale);
            finish(item, afterEach);
        }
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
