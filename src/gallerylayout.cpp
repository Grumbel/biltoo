// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerylayout.h"
#include "imageitem.h"

#include <QtMath>
#include <QVector>

namespace GalleryLayout {

namespace {

QSizeF nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    return QSizeF(item->pixmap().size());
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

    // Gallery tiles are a clean overview: full opacity, upright, no residual
    // Workspace opacity/rotation bleeding through between layout switches.
    for (ImageItem *item : items) {
        item->setItemRotation(0.0);
        item->setItemOpacity(1.0);
        if (params.mode != Mode::GridCrop) {
            item->setGalleryCellSize({});
        }
    }

    if (params.mode == Mode::SideBySide) {
        qreal x = margin;
        for (ImageItem *item : items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = availH / qMax(1.0, ns.height());
            item->setItemScale(scale);
            const qreal w = ns.width() * scale;
            const qreal h = ns.height() * scale;
            item->setPos(x + w / 2.0, margin + h / 2.0);
            x += w + gap;
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Vertical) {
        qreal y = margin;
        for (ImageItem *item : items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = availW / qMax(1.0, ns.width());
            item->setItemScale(scale);
            const qreal w = ns.width() * scale;
            const qreal h = ns.height() * scale;
            item->setPos(margin + w / 2.0, y + h / 2.0);
            y += h + gap;
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Grid) {
        const int cols = qMax(1, static_cast<int>(std::ceil(std::sqrt(double(n)))));
        const int rows = qMax(1, static_cast<int>(std::ceil(double(n) / double(cols))));
        const qreal cellW = (availW - gap * qMax(0, cols - 1)) / cols;
        const qreal cellH = (availH - gap * qMax(0, rows - 1)) / rows;
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            const int col = i % cols;
            const int row = i / cols;
            const QSizeF ns = nativeSize(item);
            const qreal scale = qMin(cellW / qMax(1.0, ns.width()),
                                    cellH / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            const qreal cx = margin + col * (cellW + gap) + cellW / 2.0;
            const qreal cy = margin + row * (cellH + gap) + cellH / 2.0;
            item->setPos(cx, cy);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::GridCrop) {
        const int cols = qMax(1, static_cast<int>(std::ceil(std::sqrt(double(n)))));
        const qreal cell = (availW - gap * qMax(0, cols - 1)) / cols;
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            const int col = i % cols;
            const int row = i / cols;
            const QSizeF ns = nativeSize(item);
            const qreal scale = qMax(cell / qMax(1.0, ns.width()),
                                    cell / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            item->setGalleryCellSize(QSizeF(cell, cell));
            const qreal cx = margin + col * (cell + gap) + cell / 2.0;
            const qreal cy = margin + row * (cell + gap) + cell / 2.0;
            item->setPos(cx, cy);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Masonry) {
        const int cols = qBound(1, params.masonryColumns, n);
        const qreal colW = (availW - gap * qMax(0, cols - 1)) / cols;
        QVector<qreal> colHeights(cols, 0.0);
        for (ImageItem *item : items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = colW / qMax(1.0, ns.width());
            item->setItemScale(scale);
            const qreal h = ns.height() * scale;
            int best = 0;
            for (int c = 1; c < cols; ++c) {
                if (colHeights.at(c) < colHeights.at(best)) {
                    best = c;
                }
            }
            const qreal cx = margin + best * (colW + gap) + colW / 2.0;
            const qreal cy = margin + colHeights.at(best) + h / 2.0;
            item->setPos(cx, cy);
            colHeights[best] += h + gap;
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::MasonryRows) {
        const int rows = qBound(1, params.masonryRows, n);
        const qreal rowH = (availH - gap * qMax(0, rows - 1)) / rows;
        QVector<qreal> rowWidths(rows, 0.0);
        for (ImageItem *item : items) {
            const QSizeF ns = nativeSize(item);
            const qreal scale = rowH / qMax(1.0, ns.height());
            item->setItemScale(scale);
            const qreal w = ns.width() * scale;
            int best = 0;
            for (int r = 1; r < rows; ++r) {
                if (rowWidths.at(r) < rowWidths.at(best)) {
                    best = r;
                }
            }
            const qreal cx = margin + rowWidths.at(best) + w / 2.0;
            const qreal cy = margin + best * (rowH + gap) + rowH / 2.0;
            item->setPos(cx, cy);
            rowWidths[best] += w + gap;
            finish(item, afterEach);
        }
    }
}

} // namespace GalleryLayout
