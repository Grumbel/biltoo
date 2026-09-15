// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONTENTXFORM_H
#define CONTENTXFORM_H

#include "coloradjust.h"
#include "imageview_types.h"

#include <QRect>
#include <QSize>
#include <QSizeF>

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

/** Max long edge for materializeDisplay on the GUI thread (matches SessionAppearance). */
inline constexpr int kGuiMaterializeMaxEdge = 512;

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

} // namespace ContentXform

#endif
