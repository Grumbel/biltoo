// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONTENTXFORM_H
#define CONTENTXFORM_H

#include "coloradjust.h"
#include "imageview_types.h"

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QTransform>
#include <QtGlobal>

/**
 * Pure content transform + layout helpers (no ImageItem / GUI).
 *
 * Display attachment stores the xform applied to a sample; the next install
 * compares want vs applied instead of guessing from aspect ratio.
 *
 * Pixel pipeline order (see SessionAppearance::materializeDisplay):
 *   flips → quarter turns → crop → color grade
 *
 * cropRect lives in **post-orient** full-frame space (after flips + turns).
 * cropSourceSize is the size of that space when the rect was recorded.
 * When quarterTurns change, callers must map crop through
 * mapCropRectThroughContentRotate90 (or map the whole Value) so cropRect and
 * cropSourceSize stay in the new post-orient space.
 */
namespace ContentXform {

/** Width/height ratio; height treated as at least 1. */
inline int longEdge(const QSize &s)
{
    return qMax(s.width(), s.height());
}

/** Cap a long edge for display/full-raster requests (min 1 when edge > 0). */
inline int clampLongEdge(int edge, int maxEdge)
{
    if (edge <= 0) {
        return 0;
    }
    if (maxEdge <= 0) {
        return edge;
    }
    return qMin(edge, maxEdge);
}

inline qreal aspectRatio(const QSize &s)
{
    return double(s.width()) / double(qMax(1, s.height()));
}

/** True when relative aspect differs by more than @p eps. */
inline bool aspectChanged(const QSize &a, const QSize &b, qreal eps = 0.02)
{
    return qAbs(aspectRatio(a) - aspectRatio(b)) > eps;
}

/** View scale factor so @p after keeps @p before on-screen width (same aspect). */
inline qreal footprintScaleFactor(const QSize &before, const QSize &after)
{
    if (before.width() < 1 || after.width() < 1) {
        return 1.0;
    }
    return qreal(before.width()) / qreal(after.width());
}

/** Integer size from continuous crop/layout extents (min 1 per axis). */
inline QSize roundedSizeAtLeast1(qreal w, qreal h)
{
    return QSize(qMax(1, qRound(w)), qMax(1, qRound(h)));
}

inline QSize roundedSizeAtLeast1(const QSizeF &s)
{
    return roundedSizeAtLeast1(s.width(), s.height());
}

/** Clamp projected post-crop long edge into [1, hostLongEdge]. */
inline int clampEstimatedEdge(qint64 scaled, int hostLongEdge)
{
    if (hostLongEdge < 1) {
        return 1;
    }
    return int(qBound(1LL, scaled, qint64(hostLongEdge)));
}

/** Height for export width keeping aspect (min 1); zero/invalid aspect → 1. */
inline int heightForAspectWidth(int width, qreal aspectW, qreal aspectH)
{
    if (width < 1 || aspectW <= 1e-9) {
        return 1;
    }
    return qMax(1, qRound(qreal(width) * (aspectH / aspectW)));
}

inline int heightForAspectWidth(int width, const QSizeF &aspect)
{
    return heightForAspectWidth(width, aspect.width(), aspect.height());
}

/** Map scale from crop extent to source pixel extent (min source 1). */
inline qreal invAxisScale(qreal cropExtent, int sourceExtent)
{
    return cropExtent / qreal(qMax(1, sourceExtent));
}


/** Max long edge for materializeDisplay on the GUI thread (matches SessionAppearance). */
inline constexpr int kGuiMaterializeMaxEdge = 512;
inline constexpr int kGuiMaterializePreviewEdge = 256;

/** Cap used when materializing a GUI preview sample. */
inline int materializePreviewEdge()
{
    return qMin(kGuiMaterializeMaxEdge, kGuiMaterializePreviewEdge);
}


struct Value {
    int quarterTurns = 0; // normalized 0..3
    bool hFlip = false;
    bool vFlip = false;
    bool hasCrop = false;
    QRect cropRect;
    QSize cropSourceSize;
    qreal cropRotation = 0.0;
    ColorAdjustments colorAdjust;

    static Value fromState(const WorkspaceItemState &s);
    void applyToState(WorkspaceItemState &s) const;
};

int normalizeQuarterTurns(int quarterTurns);
bool swapsAspect(const Value &x);
bool equal(const Value &a, const Value &b);

/**
 * Map an axis-aligned rect through the same 90° steps QImage uses
 * (QTransform::rotate(90*turns) + QImage::trueMatrix on @p space).
 * Returns the rect in the destination space; @p space is updated to the
 * post-transform size (axes swap on odd turns).
 *
 * One step of +1 (CCW in QTransform / image Y-down):
 *   (x, y, w, h) on (W, H) → (H - y - h, x, h, w) on (H, W)
 */
/**
 * Scale an axis-aligned crop rect from @p recorded source size to @p live size.
 * Empty if inputs invalid; identity when recorded == live.
 */
QRect scaleCropRect(const QRect &crop, const QSize &recorded, const QSize &live);

QRect mapCropRectThroughContentRotate90(QRect crop, QSize &space, int quarterTurns);

/**
 * Map crop geometry on a Value through content ±90° steps (same as above).
 * Updates cropRect and cropSourceSize. cropRotation is shifted by −90°×turns
 * only when the crop was already free-rotated (|angle| > ε); axis-aligned crops
 * keep cropRotation = 0 so materialize does not stack freeRot on content turns.
 * No-op if no crop or 0 turns.
 */
void mapCropThroughContentRotate90(Value &x, int quarterTurns);

/**
 * File-native size → display intrinsic after content turns and crop.
 * Orient full frame, then crop box in post-orient space (matches
 * materializeDisplay). Cropped want must not return full-frame size
 * (that stretched pixels into the wrong aspect). See CONTENT_PIPELINE.md.
 *
 * If cropSourceSize is orientation-mismatched vs the oriented frame (stale
 * record that was not mapped through a later rotate), the crop is first
 * mapped through 90° steps into the oriented orientation, then resolution-
 * scaled. Linear scale alone is wrong for orientation change except when
 * crop aspect equals the full frame.
 */
QSize layoutSize(const QSize &native, const Value &x);
QSize layoutSize(const QSize &native, const WorkspaceItemState &state);

/**
 * Whether a new raw sample must be materialized for @p want.
 * Blank shown, xform mismatch, or strict long-edge upgrade → true.
 */
bool needsRematerialize(const Value &applied, const Value &want,
                        int shownLongEdge, int incomingLongEdge);

/**
 * Expected display long edge after materializeDisplay of a host sample whose
 * long edge is @p hostLongEdge under @p want (crop / orient).
 *
 * Used by DisplaySurface::decide so FullSource+matching-want does not settle
 * on a Soft/overview crop bake while a better host is available. Without crop:
 * returns hostLongEdge. Crop rotation uses the axis-aligned crop rect size
 * (same space as cropRect after orient).
 */
int estimatedDisplayLongEdge(int hostLongEdge, const Value &want);

/**
 * Scale factors so @p logical content size occupies @p footW × @p footH
 * in scene units (Workspace crop Apply must not shrink the tile).
 */
QSizeF scaleToPreserveFootprint(qreal footW, qreal footH, const QSize &logical);

/**
 * Map an axis-aligned rect between **source** (full native raster) and
 * **display** (layout / contentRect) spaces for identity free-rotation.
 *
 * Pipeline matches materializeDisplay: flips → quarter turns → crop translate.
 * Corners are mapped; the result is the AABB (exact for axis-aligned crop
 * with cropRotation ≈ 0). Empty if inputs are invalid.
 *
 * Free-rotated crop uses the same centre/rotate window as
 * SessionAppearance::materializeDisplay (AABB of mapped corners).
 */
QRectF mapSourceRectToDisplay(const QRectF &sourceRect, const QSize &native,
                              const Value &x);
QRectF mapDisplayRectToSource(const QRectF &displayRect, const QSize &native,
                              const Value &x);

/** Source → post-flip/turn oriented space (no crop / free-rot). */
QRectF mapSourceRectToOriented(const QRectF &sourceRect, const QSize &native,
                               const Value &x);

/**
 * QTransform mapping source pixel coords → display (crop-local) coords.
 * Same pipeline as mapSourceRectToDisplay / materializeDisplay:
 * flips → quarter turns → axis-aligned crop translate or free-rot window.
 * Use for tile paint so UV stays source-aligned while dest follows orient.
 */
QTransform sourceToDisplayTransform(const QSize &native, const Value &x);

} // namespace ContentXform

#endif
