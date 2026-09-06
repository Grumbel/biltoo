// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerylayout.h"
#include "imageitem.h"

#include <QtMath>
#include <cmath>
#include <QVector>

namespace GalleryLayout {

namespace {

QSizeF nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    // Prefer intrinsic/decoded image size so placeholders pack correctly.
    return QSizeF(item->imageSize());
}

/** Pixmap size with 90°-class rotation applied (for packing aspect ratio). */
QSizeF layoutSize(const ImageItem *item)
{
    const QSizeF ns = nativeSize(item);
    if (!item || ns.isEmpty()) {
        return ns;
    }
    // Snap-ish: treat near-90 / near-270 as swapped axes (Gallery uses ±90 steps).
    qreal r = std::fmod(std::abs(item->itemRotation()), 360.0);
    if (r > 180.0) {
        r = 360.0 - r;
    }
    if (r > 45.0 && r < 135.0) {
        return QSizeF(ns.height(), ns.width());
    }
    return ns;
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
        item->setItemOpacity(1.0);
        if (params.mode != Mode::GridCrop) {
            item->setGalleryCellSize({});
        }
    }

    if (params.mode == Mode::SideBySide) {
        qreal x = margin;
        for (ImageItem *item : items) {
            const QSizeF ns = layoutSize(item);
            const qreal scale = availH / qMax(1.0, ns.height());
            item->setItemScale(scale);
            item->setItemShear(0.0);
            const qreal w = ns.width() * scale;
            const qreal h = ns.height() * scale;
            item->setPos(x + w / 2.0, margin + h / 2.0);
            x += w + gap;
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Vertical) {
        qreal y = margin;
        for (ImageItem *item : items) {
            const QSizeF ns = layoutSize(item);
            const qreal scale = availW / qMax(1.0, ns.width());
            item->setItemScale(scale);
            item->setItemShear(0.0);
            const qreal w = ns.width() * scale;
            const qreal h = ns.height() * scale;
            item->setPos(margin + w / 2.0, y + h / 2.0);
            y += h + gap;
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::Grid) {
        const int cols = params.gridColumns > 0
                             ? qMax(1, params.gridColumns)
                             : qMax(1, static_cast<int>(std::ceil(std::sqrt(double(n)))));
        // Width-driven square cells; vertical scroll. Fewer columns → larger tiles.
        // Previously cellH packed all rows into availH, which shrank tiles as
        // column count decreased (more rows into the same window height).
        const qreal cellW = (availW - gap * qMax(0, cols - 1)) / cols;
        const qreal cellH = cellW;
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            const int col = i % cols;
            const int row = i / cols;
            const QSizeF ns = layoutSize(item);
            const qreal scale = qMin(cellW / qMax(1.0, ns.width()),
                                    cellH / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            item->setItemShear(0.0);
            const qreal cx = margin + col * (cellW + gap) + cellW / 2.0;
            const qreal cy = margin + row * (cellH + gap) + cellH / 2.0;
            item->setPos(cx, cy);
            finish(item, afterEach);
        }
    } else if (params.mode == Mode::GridCrop) {
        const int cols = params.gridColumns > 0
                             ? qMax(1, params.gridColumns)
                             : qMax(1, static_cast<int>(std::ceil(std::sqrt(double(n)))));
        const qreal cell = (availW - gap * qMax(0, cols - 1)) / cols;
        for (int i = 0; i < n; ++i) {
            ImageItem *item = items.at(i);
            const int col = i % cols;
            const int row = i / cols;
            const QSizeF ns = layoutSize(item);
            const qreal scale = qMax(cell / qMax(1.0, ns.width()),
                                    cell / qMax(1.0, ns.height()));
            item->setItemScale(scale);
            item->setItemShear(0.0);
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
            const QSizeF ns = layoutSize(item);
            const qreal scale = colW / qMax(1.0, ns.width());
            item->setItemScale(scale);
            item->setItemShear(0.0);
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
            const QSizeF ns = layoutSize(item);
            const qreal scale = rowH / qMax(1.0, ns.height());
            item->setItemScale(scale);
            item->setItemShear(0.0);
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
    } else if (params.mode == Mode::MasonryFill) {
        // Column masonry, then scale each column so heights match (clean rectangle).
        const int cols = qBound(1, params.masonryColumns, n);
        const qreal colW = (availW - gap * qMax(0, cols - 1)) / cols;
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
            const qreal scale = colW / qMax(1.0, ns.width());
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
                e.item->setItemScale(scale);
                e.item->setItemShear(0.0);
                const qreal w = e.ns.width() * scale;
                const qreal h = e.ns.height() * scale;
                e.item->setPos(x + w / 2.0, y + h / 2.0);
                y += h + gap * s;
                finish(e.item, afterEach);
            }
            x += colWidth + gap;
        }
    } else if (params.mode == Mode::MasonryRowsFill) {
        // Row masonry, then scale each row so widths match (clean rectangle).
        const int rows = qBound(1, params.masonryRows, n);
        const qreal rowH = (availH - gap * qMax(0, rows - 1)) / rows;
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
            const qreal scale = rowH / qMax(1.0, ns.height());
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
                e.item->setItemScale(scale);
                e.item->setItemShear(0.0);
                const qreal w = e.ns.width() * scale;
                const qreal h = e.ns.height() * scale;
                e.item->setPos(x + w / 2.0, y + h / 2.0);
                x += w + gap * s;
                finish(e.item, afterEach);
            }
            y += rowHeight + gap;
        }
    } else if (params.mode == Mode::Flow || params.mode == Mode::FlowFill) {
        // Order-preserving wrap: L→R then T→B. Width budget from columns.
        const bool fill = (params.mode == Mode::FlowFill);
        const int cols = params.gridColumns > 0
                             ? qMax(1, params.gridColumns)
                             : 3;
        const qreal layoutW = availW;
        const qreal targetW = (layoutW - gap * qMax(0, cols - 1)) / qMax(1, cols);

        struct Entry {
            ImageItem *item = nullptr;
            QSizeF ns;
            qreal scale = 1.0;
            qreal w = 0.0;
            qreal h = 0.0;
        };
        QVector<QVector<Entry>> rows;
        QVector<Entry> cur;
        qreal rowW = 0.0;
        qreal rowH = 0.0;

        auto flushRow = [&]() {
            if (cur.isEmpty()) {
                return;
            }
            rows.append(cur);
            cur.clear();
            rowW = 0.0;
            rowH = 0.0;
        };

        for (ImageItem *item : items) {
            const QSizeF ns = layoutSize(item);
            const qreal scale = targetW / qMax(1.0, ns.width());
            const qreal w = ns.width() * scale;
            const qreal h = ns.height() * scale;
            if (!cur.isEmpty() && rowW + gap + w > layoutW + 1e-6) {
                flushRow();
            }
            cur.append(Entry{item, ns, scale, w, h});
            rowW += (cur.size() == 1 ? w : gap + w);
            rowH = qMax(rowH, h);
        }
        flushRow();

        qreal y = margin;
        for (QVector<Entry> &row : rows) {
            qreal contentW = 0.0;
            qreal contentH = 0.0;
            for (const Entry &e : row) {
                contentW += e.w;
                contentH = qMax(contentH, e.h);
            }
            contentW += gap * qMax(0, row.size() - 1);
            const qreal s = (fill && contentW > 1e-6) ? (layoutW / contentW) : 1.0;
            qreal x = margin;
            for (Entry &e : row) {
                const qreal scale = e.scale * s;
                e.item->setItemScale(scale);
                e.item->setItemShear(0.0);
                const qreal w = e.ns.width() * scale;
                const qreal h = e.ns.height() * scale;
                e.item->setPos(x + w / 2.0, y + h / 2.0);
                x += w + gap * s;
                finish(e.item, afterEach);
            }
            y += contentH * s + gap;
        }
    } else if (params.mode == Mode::Facing) {
        // Cover alone, then height-matched pairs (verso | recto), stacked.
        const qreal pairGap = gap;
        qreal y = margin;
        int i = 0;

        auto placeScaled = [&](ImageItem *item, qreal scale, qreal xLeft, qreal yTop) {
            const QSizeF ns = layoutSize(item);
            item->setItemScale(scale);
            item->setItemShear(0.0);
            const qreal w = ns.width() * scale;
            const qreal h = ns.height() * scale;
            item->setPos(xLeft + w / 2.0, yTop + h / 2.0);
            finish(item, afterEach);
            return QSizeF(w, h);
        };

        if (n >= 1) {
            const QSizeF ns = layoutSize(items.at(0));
            const qreal scale = qMin(availW / qMax(1.0, ns.width()),
                                    availH / qMax(1.0, ns.height()));
            const QSizeF sz = placeScaled(items.at(0), scale, margin, y);
            y += sz.height() + gap;
            i = 1;
        }
        const qreal halfW = (availW - pairGap) / 2.0;
        while (i < n) {
            ImageItem *left = items.at(i);
            ImageItem *right = (i + 1 < n) ? items.at(i + 1) : nullptr;
            const QSizeF nsL = layoutSize(left);
            qreal scaleL = halfW / qMax(1.0, nsL.width());
            qreal scaleR = scaleL;
            if (right) {
                const QSizeF nsR = layoutSize(right);
                // Shared height: min of height-from-halfW for each page.
                const qreal hFromL = nsL.height() * (halfW / qMax(1.0, nsL.width()));
                const qreal hFromR = nsR.height() * (halfW / qMax(1.0, nsR.width()));
                const qreal targetH = qMin(hFromL, hFromR);
                scaleL = targetH / qMax(1.0, nsL.height());
                scaleR = targetH / qMax(1.0, nsR.height());
                if (nsL.width() * scaleL > halfW) {
                    scaleL = halfW / qMax(1.0, nsL.width());
                }
                if (nsR.width() * scaleR > halfW) {
                    scaleR = halfW / qMax(1.0, nsR.width());
                }
            }
            const qreal hL = nsL.height() * scaleL;
            const QSizeF szL = placeScaled(left, scaleL, margin, y);
            qreal rowH = szL.height();
            if (right) {
                const QSizeF nsR = layoutSize(right);
                const QSizeF szR = placeScaled(right, scaleR,
                    margin + halfW + pairGap, y);
                rowH = qMax(rowH, szR.height());
                i += 2;
            } else {
                i += 1;
            }
            Q_UNUSED(hL);
            y += rowH + gap;
        }
    }

}

} // namespace GalleryLayout
