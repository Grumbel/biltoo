// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYLAYOUT_H
#define GALLERYLAYOUT_H

#include <QList>
#include <QVector>
#include <QSizeF>
#include "item/itemcomponents.h"
#include <cmath>
#include "view/viewtransform.h"
#include <functional>

class ImageItem;

/**
 * Pure packing for Gallery packaged layouts (no QGraphicsView dependency).
 * Mutates item scale, position, and optional crop cell size.
 */
namespace GalleryLayout {

/** Writes ImageItem gallery cell size (ImageItem mutator is private). */
void setItemGalleryCellSize(ImageItem *item, const QSizeF &sceneSize);
/** Writes live pose (ImageItem::applyPlacement is private). */
void applyItemPlacement(ImageItem *item, const ItemComponents::Placement &pl);

/**
 * True when item rotation is nearer ±90°/±270° than axis-aligned — pack
 * aspect uses swapped native axes (Gallery ±90 content steps).
 */
inline bool axesSwapForItemRotation(qreal rotationDeg)
{
    qreal r = std::fmod(std::abs(rotationDeg), 360.0);
    if (r > 180.0) {
        r = 360.0 - r;
    }
    return r > 45.0 && r < 135.0;
}

/**
 * Pack aspect size from logical native size and placement rotation (Stage 3 pure).
 * Soft sample pixels must never be passed as @p native — only intrinsic size.
 */
inline QSizeF layoutSizeForNative(const QSizeF &native, qreal rotationDeg)
{
    if (native.isEmpty()) {
        return native;
    }
    if (axesSwapForItemRotation(rotationDeg)) {
        return QSizeF(native.height(), native.width());
    }
    return native;
}

enum class Mode {
    SideBySide,
    Vertical,
    Grid,
    GridCrop, // disabled in UI; kept for load compatibility
    Masonry,
    MasonryRows,
    /** Column masonry then per-column scale so all columns share one bottom edge. */
    MasonryFill,
    /** Row masonry then per-row scale so all rows share one right edge. */
    MasonryRowsFill,
    /** Session order L→R, T→B; wrap at layout width (columns ≈ pages across). */
    Flow,
    /** Flow, then scale each row to exactly fill layout width. */
    FlowFill,
    /** Two-up spreads; page 1 alone as cover, then pairs (2–3), (4–5), … */
    Facing
};

struct Params {
    static constexpr qreal kDefaultMargin = 16.0;
    static constexpr qreal kDefaultGap = 12.0;

    Mode mode = Mode::Masonry;
    qreal margin = kDefaultMargin;
    qreal gap = kDefaultGap;
    qreal availW = 800.0;
    qreal availH = 600.0;
    int masonryColumns = 3;
    int masonryRows = 3;
    /** Grid / GridCrop / Flow column count; 0 = automatic (ceil sqrt n for Grid, 3 for Flow). */
    int gridColumns = 0;
};

/** Grid/Flow columns: explicit count or auto ceil(√n), always ≥ 1. */
inline int resolvedColumns(int n, int gridColumns)
{
    if (gridColumns > 0) {
        return gridColumns < 1 ? 1 : gridColumns;
    }
    if (n <= 0) {
        return 1;
    }
    // ceil(sqrt(n)) without pulling <cmath> into every TU that includes this header.
    int cols = 1;
    while (cols * cols < n) {
        ++cols;
    }
    return cols;
}

/** Flow/FlowFill columns: explicit or default 3 (not √n). */
inline int resolvedFlowColumns(int gridColumns, int defaultCols = 3)
{
    if (gridColumns > 0) {
        return gridColumns;
    }
    return defaultCols < 1 ? 1 : defaultCols;
}

/** Masonry column/row count clamped to [1, n] (n≤0 → 1). */
inline int resolvedBandCount(int requested, int n)
{
    if (n <= 0) {
        return 1;
    }
    if (requested < 1) {
        return 1;
    }
    return requested > n ? n : requested;
}

/** Axis length of one cell when packing @p count bands into @p avail with @p gap. */
inline qreal cellAxisLength(qreal avail, qreal gap, int count)
{
    const int c = count < 1 ? 1 : count;
    const qreal gaps = gap * qreal(c > 0 ? c - 1 : 0);
    return (avail - gaps) / qreal(c);
}

/** Scale to fit inside cell (contain). Native axes floored at 1. */
inline qreal containScale(qreal cellW, qreal cellH, qreal nativeW, qreal nativeH)
{
    return ViewTransform::containScale(cellW, cellH, nativeW, nativeH);
}

/** Scale to cover cell (may crop). */
inline qreal coverScale(qreal cellW, qreal cellH, qreal nativeW, qreal nativeH)
{
    return ViewTransform::coverScale(cellW, cellH, nativeW, nativeH);
}

/** Scale so one axis fills @p cellAxis (width- or height-driven bands). */
inline qreal axisFillScale(qreal cellAxis, qreal nativeAxis)
{
    const qreal na = nativeAxis > 1.0 ? nativeAxis : 1.0;
    return cellAxis / na;
}

/** Centre + uniform scale for one packed tile (Stage 3 pure data plane). */
struct PackPose {
    QPointF center;
    qreal scale = 1.0;
    /** Non-empty for GridCrop (gallery cell clip); empty clears cell size. */
    QSizeF cellSize;
};

/**
 * SideBySide: height fills @p availH; tiles march left→right from @p margin.
 * @p layoutSizes are rotation-aware pack sizes (see layoutSizeForNative).
 */
inline QVector<PackPose> packPosesSideBySide(const QVector<QSizeF> &layoutSizes,
                                              qreal margin, qreal gap, qreal availH)
{
    QVector<PackPose> out;
    out.reserve(layoutSizes.size());
    qreal x = margin;
    for (const QSizeF &ns : layoutSizes) {
        const qreal scale = axisFillScale(availH, ns.height());
        const qreal w = ns.width() * scale;
        const qreal h = ns.height() * scale;
        PackPose p;
        p.center = QPointF(x + w / 2.0, margin + h / 2.0);
        p.scale = scale;
        out.append(p);
        x += w + gap;
    }
    return out;
}

/**
 * Vertical: width fills @p availW; tiles march top→bottom from @p margin.
 */
inline QVector<PackPose> packPosesVertical(const QVector<QSizeF> &layoutSizes,
                                           qreal margin, qreal gap, qreal availW)
{
    QVector<PackPose> out;
    out.reserve(layoutSizes.size());
    qreal y = margin;
    for (const QSizeF &ns : layoutSizes) {
        const qreal scale = axisFillScale(availW, ns.width());
        const qreal w = ns.width() * scale;
        const qreal h = ns.height() * scale;
        PackPose p;
        p.center = QPointF(margin + w / 2.0, y + h / 2.0);
        p.scale = scale;
        out.append(p);
        y += h + gap;
    }
    return out;
}

/**
 * Grid: square cells from width / columns; contain scale; vertical scroll.
 * @p gridColumns 0 → ceil(√n).
 */
inline QVector<PackPose> packPosesGrid(const QVector<QSizeF> &layoutSizes,
                                       qreal margin, qreal gap, qreal availW,
                                       int gridColumns)
{
    const int n = layoutSizes.size();
    const int cols = resolvedColumns(n, gridColumns);
    const qreal cellW = cellAxisLength(availW, gap, cols);
    const qreal cellH = cellW;
    QVector<PackPose> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QSizeF &ns = layoutSizes.at(i);
        const int col = i % cols;
        const int row = i / cols;
        PackPose p;
        p.scale = containScale(cellW, cellH, ns.width(), ns.height());
        p.center = QPointF(margin + col * (cellW + gap) + cellW / 2.0,
                           margin + row * (cellH + gap) + cellH / 2.0);
        out.append(p);
    }
    return out;
}

/**
 * GridCrop: square cells, cover scale, cellSize set for gallery clip.
 */
inline QVector<PackPose> packPosesGridCrop(const QVector<QSizeF> &layoutSizes,
                                           qreal margin, qreal gap, qreal availW,
                                           int gridColumns)
{
    const int n = layoutSizes.size();
    const int cols = resolvedColumns(n, gridColumns);
    const qreal cell = cellAxisLength(availW, gap, cols);
    QVector<PackPose> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QSizeF &ns = layoutSizes.at(i);
        const int col = i % cols;
        const int row = i / cols;
        PackPose p;
        p.scale = coverScale(cell, cell, ns.width(), ns.height());
        p.center = QPointF(margin + col * (cell + gap) + cell / 2.0,
                           margin + row * (cell + gap) + cell / 2.0);
        p.cellSize = QSizeF(cell, cell);
        out.append(p);
    }
    return out;
}

/**
 * Column masonry: width fills band; place into shortest column.
 * @p masonryColumns requested band count (clamped to [1, n]).
 */
inline QVector<PackPose> packPosesMasonry(const QVector<QSizeF> &layoutSizes,
                                          qreal margin, qreal gap, qreal availW,
                                          int masonryColumns)
{
    const int n = layoutSizes.size();
    const int cols = resolvedBandCount(masonryColumns, n);
    const qreal colW = cellAxisLength(availW, gap, cols);
    QVector<qreal> colHeights(cols, 0.0);
    QVector<PackPose> out;
    out.reserve(n);
    for (const QSizeF &ns : layoutSizes) {
        const qreal scale = axisFillScale(colW, ns.width());
        const qreal h = ns.height() * scale;
        int best = 0;
        for (int c = 1; c < cols; ++c) {
            if (colHeights.at(c) < colHeights.at(best)) {
                best = c;
            }
        }
        PackPose p;
        p.scale = scale;
        p.center = QPointF(margin + best * (colW + gap) + colW / 2.0,
                           margin + colHeights.at(best) + h / 2.0);
        out.append(p);
        colHeights[best] += h + gap;
    }
    return out;
}

/**
 * Row masonry: height fills band; place into shortest row.
 */
inline QVector<PackPose> packPosesMasonryRows(const QVector<QSizeF> &layoutSizes,
                                              qreal margin, qreal gap, qreal availH,
                                              int masonryRows)
{
    const int n = layoutSizes.size();
    const int rows = resolvedBandCount(masonryRows, n);
    const qreal rowH = cellAxisLength(availH, gap, rows);
    QVector<qreal> rowWidths(rows, 0.0);
    QVector<PackPose> out;
    out.reserve(n);
    for (const QSizeF &ns : layoutSizes) {
        const qreal scale = axisFillScale(rowH, ns.height());
        const qreal w = ns.width() * scale;
        int best = 0;
        for (int r = 1; r < rows; ++r) {
            if (rowWidths.at(r) < rowWidths.at(best)) {
                best = r;
            }
        }
        PackPose p;
        p.scale = scale;
        p.center = QPointF(margin + rowWidths.at(best) + w / 2.0,
                           margin + best * (rowH + gap) + rowH / 2.0);
        out.append(p);
        rowWidths[best] += w + gap;
    }
    return out;
}

/**
 * Flow / FlowFill: order-preserving wrap L→R then T→B.
 * Initial scale fills target column width; @p fill stretches each row to layoutW.
 */
inline QVector<PackPose> packPosesFlow(const QVector<QSizeF> &layoutSizes,
                                       qreal margin, qreal gap, qreal availW,
                                       int gridColumns, bool fill)
{
    const int cols = resolvedFlowColumns(gridColumns);
    const qreal layoutW = availW;
    const qreal targetW = cellAxisLength(layoutW, gap, cols);

    struct Entry {
        QSizeF ns;
        qreal scale = 1.0;
        qreal w = 0.0;
        qreal h = 0.0;
    };
    QVector<QVector<Entry>> rows;
    QVector<Entry> cur;
    qreal rowW = 0.0;

    auto flushRow = [&]() {
        if (cur.isEmpty()) {
            return;
        }
        rows.append(cur);
        cur.clear();
        rowW = 0.0;
    };

    for (const QSizeF &ns : layoutSizes) {
        const qreal scale = axisFillScale(targetW, ns.width());
        const qreal w = ns.width() * scale;
        const qreal h = ns.height() * scale;
        if (!cur.isEmpty() && rowW + gap + w > layoutW + 1e-6) {
            flushRow();
        }
        cur.append(Entry{ns, scale, w, h});
        rowW += (cur.size() == 1 ? w : gap + w);
    }
    flushRow();

    QVector<PackPose> out;
    out.reserve(layoutSizes.size());
    qreal y = margin;
    for (const QVector<Entry> &row : rows) {
        qreal contentW = 0.0;
        for (const Entry &e : row) {
            contentW += e.w;
        }
        contentW += gap * ViewTransform::nonNeg(qint64(row.size()) - 1);
        const qreal s = (fill && contentW > 1e-6) ? (layoutW / contentW) : 1.0;
        qreal x = margin;
        qreal placedH = 0.0;
        for (const Entry &e : row) {
            const qreal scale = e.scale * s;
            const qreal w = e.ns.width() * scale;
            const qreal h = e.ns.height() * scale;
            PackPose p;
            p.scale = scale;
            p.center = QPointF(x + w / 2.0, y + h / 2.0);
            out.append(p);
            x += w + gap * s;
            placedH = qMax(placedH, h);
        }
        y += placedH + gap;
    }
    return out;
}

/**
 * Facing: cover alone (contain in avail), then height-matched pairs (verso|recto).
 */
inline QVector<PackPose> packPosesFacing(const QVector<QSizeF> &layoutSizes,
                                         qreal margin, qreal gap,
                                         qreal availW, qreal availH)
{
    const int n = layoutSizes.size();
    QVector<PackPose> out;
    out.reserve(n);
    if (n <= 0) {
        return out;
    }

    const qreal pairGap = gap;
    qreal y = margin;
    int i = 0;

    // Cover: contain in full avail.
    {
        const QSizeF &ns = layoutSizes.at(0);
        const qreal scale = containScale(availW, availH, ns.width(), ns.height());
        const qreal w = ns.width() * scale;
        const qreal h = ns.height() * scale;
        PackPose p;
        p.scale = scale;
        p.center = QPointF(margin + w / 2.0, y + h / 2.0);
        out.append(p);
        y += h + gap;
        i = 1;
    }

    const qreal halfW = (availW - pairGap) / 2.0;
    while (i < n) {
        const QSizeF &nsL = layoutSizes.at(i);
        const bool hasRight = (i + 1 < n);
        qreal scaleL = axisFillScale(halfW, nsL.width());
        qreal scaleR = scaleL;
        if (hasRight) {
            const QSizeF &nsR = layoutSizes.at(i + 1);
            const qreal hFromL = nsL.height() * axisFillScale(halfW, nsL.width());
            const qreal hFromR = nsR.height() * axisFillScale(halfW, nsR.width());
            const qreal targetH = qMin(hFromL, hFromR);
            scaleL = axisFillScale(targetH, nsL.height());
            scaleR = axisFillScale(targetH, nsR.height());
            if (nsL.width() * scaleL > halfW) {
                scaleL = axisFillScale(halfW, nsL.width());
            }
            if (nsR.width() * scaleR > halfW) {
                scaleR = axisFillScale(halfW, nsR.width());
            }
        }
        const qreal wL = nsL.width() * scaleL;
        const qreal hL = nsL.height() * scaleL;
        PackPose left;
        left.scale = scaleL;
        left.center = QPointF(margin + wL / 2.0, y + hL / 2.0);
        out.append(left);
        qreal rowH = hL;
        if (hasRight) {
            const QSizeF &nsR = layoutSizes.at(i + 1);
            const qreal wR = nsR.width() * scaleR;
            const qreal hR = nsR.height() * scaleR;
            PackPose right;
            right.scale = scaleR;
            right.center = QPointF(margin + halfW + pairGap + wR / 2.0, y + hR / 2.0);
            out.append(right);
            rowH = qMax(rowH, hR);
            i += 2;
        } else {
            i += 1;
        }
        y += rowH + gap;
    }
    return out;
}


/**
 * Uniform scale about pack origin so content width equals @p availW (width-fit).
 * Used after MasonryFill column equalization, which can widen short columns.
 */
inline void fitPackPosesToAvailWidth(QVector<PackPose> &out,
                                     const QVector<QSizeF> &layoutSizes,
                                     qreal margin, qreal availW)
{
    if (out.isEmpty() || availW <= 1e-6) {
        return;
    }
    qreal maxRight = margin;
    for (int i = 0; i < out.size() && i < layoutSizes.size(); ++i) {
        if (out.at(i).scale <= 0.0) {
            continue;
        }
        const qreal w = layoutSizes.at(i).width() * out.at(i).scale;
        maxRight = qMax(maxRight, out.at(i).center.x() + w / 2.0);
    }
    const qreal contentW = maxRight - margin;
    if (contentW <= 1e-6) {
        return;
    }
    const qreal g = availW / contentW;
    if (qAbs(g - 1.0) < 1e-9) {
        return;
    }
    const QPointF origin(margin, margin);
    for (int i = 0; i < out.size(); ++i) {
        out[i].center = origin + (out.at(i).center - origin) * g;
        out[i].scale *= g;
    }
}

/**
 * Uniform scale about pack origin so content height equals @p availH (height-fit).
 * Used after MasonryRowsFill row equalization.
 */
inline void fitPackPosesToAvailHeight(QVector<PackPose> &out,
                                      const QVector<QSizeF> &layoutSizes,
                                      qreal margin, qreal availH)
{
    if (out.isEmpty() || availH <= 1e-6) {
        return;
    }
    qreal maxBottom = margin;
    for (int i = 0; i < out.size() && i < layoutSizes.size(); ++i) {
        if (out.at(i).scale <= 0.0) {
            continue;
        }
        const qreal h = layoutSizes.at(i).height() * out.at(i).scale;
        maxBottom = qMax(maxBottom, out.at(i).center.y() + h / 2.0);
    }
    const qreal contentH = maxBottom - margin;
    if (contentH <= 1e-6) {
        return;
    }
    const qreal g = availH / contentH;
    if (qAbs(g - 1.0) < 1e-9) {
        return;
    }
    const QPointF origin(margin, margin);
    for (int i = 0; i < out.size(); ++i) {
        out[i].center = origin + (out.at(i).center - origin) * g;
        out[i].scale *= g;
    }
}

/**
 * MasonryFill: column masonry then uniform scale per column so heights match.
 * Per-column scale widens short columns past availW; a final global fit restores
 * exact layout width (bottoms stay aligned). Poses in @p layoutSizes order.
 */
inline QVector<PackPose> packPosesMasonryFill(const QVector<QSizeF> &layoutSizes,
                                              qreal margin, qreal gap, qreal availW,
                                              int masonryColumns)
{
    const int n = layoutSizes.size();
    QVector<PackPose> out(n);
    if (n <= 0) {
        return out;
    }
    const int cols = resolvedBandCount(masonryColumns, n);
    const qreal colW = cellAxisLength(availW, gap, cols);
    struct Entry {
        int index = 0;
        QSizeF ns;
        qreal scale = 1.0;
        qreal h = 0.0;
    };
    QVector<QVector<Entry>> columns(cols);
    QVector<qreal> colHeights(cols, 0.0);
    for (int i = 0; i < n; ++i) {
        const QSizeF &ns = layoutSizes.at(i);
        const qreal scale = axisFillScale(colW, ns.width());
        const qreal h = ns.height() * scale;
        int best = 0;
        for (int c = 1; c < cols; ++c) {
            if (colHeights.at(c) < colHeights.at(best)) {
                best = c;
            }
        }
        columns[best].append(Entry{i, ns, scale, h});
        colHeights[best] += h + gap;
    }
    qreal maxH = 0.0;
    for (int c = 0; c < cols; ++c) {
        if (columns.at(c).isEmpty()) {
            continue;
        }
        maxH = qMax(maxH, colHeights.at(c) - gap);
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
        for (const Entry &e : columns.at(c)) {
            const qreal scale = e.scale * s;
            const qreal w = e.ns.width() * scale;
            const qreal h = e.ns.height() * scale;
            PackPose p;
            p.scale = scale;
            p.center = QPointF(x + w / 2.0, y + h / 2.0);
            out[e.index] = p;
            y += h + gap * s;
        }
        x += colWidth + gap;
    }
    fitPackPosesToAvailWidth(out, layoutSizes, margin, availW);
    return out;
}

/**
 * MasonryRowsFill: row masonry then uniform scale per row so widths match.
 * Per-row scale can grow height past availH; a final global fit restores exact
 * layout height (right edges stay aligned). Poses in @p layoutSizes order.
 */
inline QVector<PackPose> packPosesMasonryRowsFill(const QVector<QSizeF> &layoutSizes,
                                                  qreal margin, qreal gap, qreal availH,
                                                  int masonryRows)
{
    const int n = layoutSizes.size();
    QVector<PackPose> out(n);
    if (n <= 0) {
        return out;
    }
    const int rows = resolvedBandCount(masonryRows, n);
    const qreal rowH = cellAxisLength(availH, gap, rows);
    struct Entry {
        int index = 0;
        QSizeF ns;
        qreal scale = 1.0;
        qreal w = 0.0;
    };
    QVector<QVector<Entry>> rowItems(rows);
    QVector<qreal> rowWidths(rows, 0.0);
    for (int i = 0; i < n; ++i) {
        const QSizeF &ns = layoutSizes.at(i);
        const qreal scale = axisFillScale(rowH, ns.height());
        const qreal w = ns.width() * scale;
        int best = 0;
        for (int r = 1; r < rows; ++r) {
            if (rowWidths.at(r) < rowWidths.at(best)) {
                best = r;
            }
        }
        rowItems[best].append(Entry{i, ns, scale, w});
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
        for (const Entry &e : rowItems.at(r)) {
            const qreal scale = e.scale * s;
            const qreal w = e.ns.width() * scale;
            const qreal h = e.ns.height() * scale;
            PackPose p;
            p.scale = scale;
            p.center = QPointF(x + w / 2.0, y + h / 2.0);
            out[e.index] = p;
            x += w + gap * s;
        }
        y += rowHeight + gap;
    }
    fitPackPosesToAvailHeight(out, layoutSizes, margin, availH);
    return out;
}

/**
 * Dispatch pure pack poses for @p mode (Stage 3 single entry for tests + pack).
 */
inline QVector<PackPose> packPosesForMode(Mode mode, const QVector<QSizeF> &layoutSizes,
                                          const Params &params)
{
    const qreal margin = params.margin;
    const qreal gap = params.gap;
    const qreal availW = params.availW;
    const qreal availH = params.availH;
    switch (mode) {
    case Mode::SideBySide:
        return packPosesSideBySide(layoutSizes, margin, gap, availH);
    case Mode::Vertical:
        return packPosesVertical(layoutSizes, margin, gap, availW);
    case Mode::Grid:
        return packPosesGrid(layoutSizes, margin, gap, availW, params.gridColumns);
    case Mode::GridCrop:
        return packPosesGridCrop(layoutSizes, margin, gap, availW, params.gridColumns);
    case Mode::Masonry:
        return packPosesMasonry(layoutSizes, margin, gap, availW, params.masonryColumns);
    case Mode::MasonryRows:
        return packPosesMasonryRows(layoutSizes, margin, gap, availH, params.masonryRows);
    case Mode::MasonryFill:
        return packPosesMasonryFill(layoutSizes, margin, gap, availW, params.masonryColumns);
    case Mode::MasonryRowsFill:
        return packPosesMasonryRowsFill(layoutSizes, margin, gap, availH, params.masonryRows);
    case Mode::Flow:
        return packPosesFlow(layoutSizes, margin, gap, availW, params.gridColumns, false);
    case Mode::FlowFill:
        return packPosesFlow(layoutSizes, margin, gap, availW, params.gridColumns, true);
    case Mode::Facing:
        return packPosesFacing(layoutSizes, margin, gap, availW, availH);
    }
    return {};
}

/**
 * Arrange @p items in scene coordinates. Clears gallery crop except for GridCrop.
 * @p afterEach is invoked after each item is placed (e.g. to snapshot state).
 */
void pack(const QList<ImageItem *> &items, const Params &params,
          const std::function<void(ImageItem *)> &afterEach = {});

} // namespace GalleryLayout

#endif // GALLERYLAYOUT_H
