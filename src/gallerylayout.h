// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYLAYOUT_H
#define GALLERYLAYOUT_H

#include <QList>
#include <QVector>
#include <QSizeF>
#include "itemcomponents.h"
#include <cmath>
#include "viewtransform.h"
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
 * Arrange @p items in scene coordinates. Clears gallery crop except for GridCrop.
 * @p afterEach is invoked after each item is placed (e.g. to snapshot state).
 */
void pack(const QList<ImageItem *> &items, const Params &params,
          const std::function<void(ImageItem *)> &afterEach = {});

} // namespace GalleryLayout

#endif // GALLERYLAYOUT_H
