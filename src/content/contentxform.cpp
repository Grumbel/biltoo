// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "content/contentxform.h"

#include <QtMath>
#include <QTransform>

namespace ContentXform {

int normalizeQuarterTurns(int quarterTurns)
{
    quarterTurns %= 4;
    if (quarterTurns < 0) {
        quarterTurns += 4;
    }
    return quarterTurns;
}

Value Value::fromState(const WorkspaceItemState &s)
{
    Value x;
    x.quarterTurns = normalizeQuarterTurns(s.contentQuarterTurns);
    x.hFlip = s.contentHFlip;
    x.vFlip = s.contentVFlip;
    x.hasCrop = s.hasCrop && !s.cropRect.isEmpty();
    x.cropRect = s.cropRect;
    x.cropSourceSize = s.cropSourceSize;
    x.cropRotation = s.cropRotation;
    x.colorAdjust = s.colorAdjust;
    return x;
}

void Value::applyToState(WorkspaceItemState &s) const
{
    s.contentQuarterTurns = normalizeQuarterTurns(quarterTurns);
    s.contentHFlip = hFlip;
    s.contentVFlip = vFlip;
    s.hasCrop = hasCrop;
    s.cropRect = cropRect;
    s.cropSourceSize = cropSourceSize;
    s.cropRotation = cropRotation;
    s.colorAdjust = colorAdjust;
}

bool swapsAspect(const Value &x)
{
    return (normalizeQuarterTurns(x.quarterTurns) % 2) != 0;
}

bool equal(const Value &a, const Value &b)
{
    if (normalizeQuarterTurns(a.quarterTurns) != normalizeQuarterTurns(b.quarterTurns)) {
        return false;
    }
    if (a.hFlip != b.hFlip || a.vFlip != b.vFlip) {
        return false;
    }
    if (a.hasCrop != b.hasCrop) {
        return false;
    }
    if (a.hasCrop) {
        if (a.cropRect != b.cropRect || a.cropSourceSize != b.cropSourceSize) {
            return false;
        }
        if (!qFuzzyCompare(1.0 + a.cropRotation, 1.0 + b.cropRotation)) {
            return false;
        }
    }
    const ColorAdjustments &ca = a.colorAdjust;
    const ColorAdjustments &cb = b.colorAdjust;
    if (ca.brightness != cb.brightness || ca.contrast != cb.contrast
        || ca.saturation != cb.saturation || ca.hue != cb.hue
        || ca.invert != cb.invert
        || !qFuzzyCompare(ca.gamma, cb.gamma)) {
        return false;
    }
    return true;
}

QRect mapCropRectThroughContentRotate90(QRect crop, QSize &space, int quarterTurns)
{
    quarterTurns = normalizeQuarterTurns(quarterTurns);
    if (quarterTurns == 0 || crop.isEmpty()) {
        return crop.normalized();
    }
    if (space.width() < 1 || space.height() < 1) {
        // Degenerate canvas: still swap axes of the rect for odd turns.
        QRect r = crop.normalized();
        if ((quarterTurns % 2) != 0) {
            r = QRect(r.y(), r.x(), r.height(), r.width());
            space = QSize(space.height(), space.width());
        }
        return r;
    }
    QRect r = crop.normalized();
    // Match QImage::trueMatrix(QTransform::rotate(90*k), W, H):
    // one +90° step: (x,y,w,h) on (W,H) → (H-y-h, x, h, w) on (H,W).
    for (int i = 0; i < quarterTurns; ++i) {
        const int W = space.width();
        const int H = space.height();
        r = QRect(H - r.y() - r.height(), r.x(), r.height(), r.width());
        if (r.width() < 1) {
            r.setWidth(1);
        }
        if (r.height() < 1) {
            r.setHeight(1);
        }
        space = QSize(H, W);
    }
    return r.normalized();
}

void mapCropThroughContentRotate90(Value &x, int quarterTurns)
{
    if (!x.hasCrop || x.cropRect.isEmpty() || quarterTurns == 0) {
        return;
    }
    quarterTurns = normalizeQuarterTurns(quarterTurns);
    if (quarterTurns == 0) {
        return;
    }
    QSize sz = x.cropSourceSize;
    if (!sz.isValid() || sz.width() < 1 || sz.height() < 1) {
        const QRect r = x.cropRect.normalized();
        sz = QSize(r.x() + r.width(), r.y() + r.height());
    }
    x.cropRect = mapCropRectThroughContentRotate90(x.cropRect, sz, quarterTurns);
    x.cropSourceSize = sz;
    // Free-crop angle conjugates with content turns so the same pixels stay
    // selected. Axis-aligned crops (cropRotation ≈ 0) only need the AABB map —
    // subtracting 90° would arm materializeDisplay's freeRot path and stack a
    // second 90° on top of contentQuarterTurns (looks like 180°).
    constexpr qreal kFreeRotEps = 0.05;
    if (qAbs(x.cropRotation) > kFreeRotEps) {
        x.cropRotation -= 90.0 * quarterTurns;
        while (x.cropRotation > 180.0) {
            x.cropRotation -= 360.0;
        }
        while (x.cropRotation <= -180.0) {
            x.cropRotation += 360.0;
        }
    } else {
        x.cropRotation = 0.0;
    }
}

/** True when both sizes share landscape/portrait class (or either is square). */
static bool sameOrientationClass(const QSize &a, const QSize &b)
{
    if (a.width() < 1 || a.height() < 1 || b.width() < 1 || b.height() < 1) {
        return true;
    }
    if (a.width() == a.height() || b.width() == b.height()) {
        return true;
    }
    return (a.width() > a.height()) == (b.width() > b.height());
}

QRect scaleCropRect(const QRect &crop, const QSize &recorded, const QSize &live)
{
    if (crop.isEmpty() || live.width() < 1 || live.height() < 1) {
        return {};
    }
    if (!recorded.isValid() || recorded.width() < 1 || recorded.height() < 1
        || recorded == live) {
        return crop;
    }
    const QSize sz = roundedSizeAtLeast1(
        crop.width() * double(live.width()) / double(recorded.width()),
        crop.height() * double(live.height()) / double(recorded.height()));
    return QRect(
        qRound(crop.x() * double(live.width()) / double(recorded.width())),
        qRound(crop.y() * double(live.height()) / double(recorded.height())),
        sz.width(),
        sz.height());
}

QSize layoutSize(const QSize &native, const Value &x)
{
    if (!isPositiveSize(native)) {
        return native;
    }
    // Post-orient size first (cropRect is defined in that space).
    QSize oriented = swapsAspect(x) ? QSize(native.height(), native.width())
                                    : native;
    if (!x.hasCrop || x.cropRect.isEmpty()) {
        return oriented;
    }
    QSize basis = (x.cropSourceSize.isValid() && x.cropSourceSize.width() > 0
                   && x.cropSourceSize.height() > 0)
                      ? x.cropSourceSize
                      : oriented;
    QRect crop = x.cropRect.normalized();

    // Orientation mismatch: crop was recorded in a space whose axes do not
    // match the current oriented frame (e.g. turns bumped without mapping the
    // crop). Linear scale alone is wrong unless crop aspect == full aspect.
    // Map through one 90° step (same matrix as materialize / bakeRotate90).
    if (!sameOrientationClass(basis, oriented)) {
        crop = mapCropRectThroughContentRotate90(crop, basis, 1);
        // If still mismatched (shouldn't happen for axis-aligned sizes), try
        // one more step so basis matches oriented class.
        if (!sameOrientationClass(basis, oriented)) {
            crop = mapCropRectThroughContentRotate90(crop, basis, 1);
        }
    }

    if (basis != oriented) {
        crop = scaleCropRect(crop, basis, oriented);
    }
    if (crop.width() < 1 || crop.height() < 1) {
        return oriented;
    }
    return QSize(crop.width(), crop.height());
}

QSize layoutSize(const QSize &native, const WorkspaceItemState &state)
{
    return layoutSize(native, Value::fromState(state));
}

bool needsRematerialize(const Value &applied, const Value &want,
                        int shownLongEdge, int incomingLongEdge)
{
    if (incomingLongEdge <= 0) {
        return false;
    }
    if (shownLongEdge <= 0) {
        return true;
    }
    if (!equal(applied, want)) {
        return true;
    }
    // Crop-aware: compare expected post-crop display edges, not host vs shown.
    return estimatedDisplayLongEdge(incomingLongEdge, want) > shownLongEdge;
}

int estimatedDisplayLongEdge(int hostLongEdge, const Value &want)
{
    if (hostLongEdge <= 0) {
        return 0;
    }
    if (!want.hasCrop || want.cropRect.isEmpty()) {
        return hostLongEdge;
    }
    const QRect crop = want.cropRect.normalized();
    const int cropLong = qMax(crop.width(), crop.height());
    if (cropLong < 1) {
        return hostLongEdge;
    }
    int srcLong = 0;
    if (want.cropSourceSize.isValid()
        && want.cropSourceSize.width() > 0
        && want.cropSourceSize.height() > 0) {
        srcLong = qMax(want.cropSourceSize.width(), want.cropSourceSize.height());
    }
    if (srcLong < 1) {
        return hostLongEdge;
    }
    const qint64 scaled =
        (qint64(hostLongEdge) * qint64(cropLong) + qint64(srcLong) / 2)
        / qint64(srcLong);
    return clampEstimatedEdge(scaled, hostLongEdge);
}

QSizeF scaleToPreserveFootprint(qreal footW, qreal footH, const QSize &logical)
{
    if (!isPositiveSize(logical) || logical.width() < 1 || logical.height() < 1) {
        return QSizeF(1.0, 1.0);
    }
    if (!(footW > 1e-6) || !(footH > 1e-6)) {
        return QSizeF(1.0, 1.0);
    }
    const qreal sx = footW / qreal(logical.width());
    const qreal sy = footH / qreal(logical.height());
    if (!qIsFinite(sx) || !qIsFinite(sy) || sx < 1e-6 || sy < 1e-6) {
        return QSizeF(1.0, 1.0);
    }
    return QSizeF(sx, sy);
}


namespace {

constexpr qreal kFreeRotEps = 1e-3;

bool hasFreeCropRotation(const Value &x)
{
    return x.hasCrop && qAbs(x.cropRotation) > kFreeRotEps;
}

/** Map one point: source → oriented (flips then turns). Updates space size. */
QPointF sourcePointToOriented(QPointF p, QSize space, const Value &x)
{
    const qreal W0 = space.width();
    const qreal H0 = space.height();
    if (x.hFlip) {
        p.setX(W0 - p.x());
    }
    if (x.vFlip) {
        p.setY(H0 - p.y());
    }
    int turns = normalizeQuarterTurns(x.quarterTurns);
    qreal W = W0;
    qreal H = H0;
    for (int i = 0; i < turns; ++i) {
        // +90° (same discrete as mapCropRectThroughContentRotate90 for points):
        // (x,y) → (H - y, x); space (W,H) → (H,W)
        const qreal nx = H - p.y();
        const qreal ny = p.x();
        p = QPointF(nx, ny);
        const qreal nW = H;
        const qreal nH = W;
        W = nW;
        H = nH;
    }
    return p;
}

QPointF orientedPointToSource(QPointF p, QSize orientedSpace, const Value &x)
{
    // Inverse turns (CW = 4 - turns), then inverse flips.
    int turns = normalizeQuarterTurns(x.quarterTurns);
    int inv = normalizeQuarterTurns(4 - turns);
    qreal W = orientedSpace.width();
    qreal H = orientedSpace.height();
    for (int i = 0; i < inv; ++i) {
        // Inverse of (x,y)→(H-y, x) on (W,H)→(H,W):
        // from (x',y') on (H,W): x = y', y = H - x'  with H = old height = new width?
        // After forward: space became (H_old, W_old). So current (W,H) = (H_old, W_old).
        // Forward: nx = H_old - y, ny = x; newW = H_old, newH = W_old.
        // Inverse: x = y', y = newW - x' = H_old - x'?  Wait newW = H_old so y = W - x'
        const qreal nx = p.y();
        const qreal ny = W - p.x();
        p = QPointF(nx, ny);
        const qreal nW = H;
        const qreal nH = W;
        W = nW;
        H = nH;
    }
    // Now in pre-turn (post-flip) native orientation size (native if no swap).
    const qreal nW = W;
    const qreal nH = H;
    if (x.vFlip) {
        p.setY(nH - p.y());
    }
    if (x.hFlip) {
        p.setX(nW - p.x());
    }
    return p;
}

QRectF aabbFromCorners(const QPointF corners[4])
{
    qreal minX = corners[0].x(), maxX = corners[0].x();
    qreal minY = corners[0].y(), maxY = corners[0].y();
    for (int i = 1; i < 4; ++i) {
        minX = qMin(minX, corners[i].x());
        maxX = qMax(maxX, corners[i].x());
        minY = qMin(minY, corners[i].y());
        maxY = qMax(maxY, corners[i].y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

} // namespace

QRectF mapSourceRectToOriented(const QRectF &sourceRect, const QSize &native,
                               const Value &x)
{
    if (!isPositiveSize(native) || sourceRect.isEmpty()) {
        return {};
    }
    const QPointF c[4] = {
        sourceRect.topLeft(),
        sourceRect.topRight(),
        sourceRect.bottomRight(),
        sourceRect.bottomLeft(),
    };
    QPointF o[4];
    for (int i = 0; i < 4; ++i) {
        o[i] = sourcePointToOriented(c[i], native, x);
    }
    return aabbFromCorners(o);
}

QRectF mapSourceRectToDisplay(const QRectF &sourceRect, const QSize &native,
                              const Value &x)
{
    if (!isPositiveSize(native) || sourceRect.isEmpty()) {
        return {};
    }
    const QPointF c[4] = {
        sourceRect.topLeft(),
        sourceRect.topRight(),
        sourceRect.bottomRight(),
        sourceRect.bottomLeft(),
    };
    QPointF o[4];
    for (int i = 0; i < 4; ++i) {
        o[i] = sourcePointToOriented(c[i], native, x);
    }
    QRectF oriented = aabbFromCorners(o);
    if (!x.hasCrop || x.cropRect.isEmpty()) {
        return oriented;
    }
    const QRect crop = x.cropRect.normalized();
    const qreal dw = crop.width();
    const qreal dh = crop.height();
    if (hasFreeCropRotation(x)) {
        // Match SessionAppearance materializeDisplay free-rot window sample:
        // translate to crop centre, rotate -θ, into display (0..dw)×(0..dh).
        const QPointF srcCenter(crop.center());
        QTransform t;
        t.translate(dw / 2.0, dh / 2.0);
        t.rotate(-x.cropRotation);
        t.translate(-srcCenter.x(), -srcCenter.y());
        QPointF d[4];
        for (int i = 0; i < 4; ++i) {
            d[i] = t.map(o[i]);
        }
        QRectF local = aabbFromCorners(d);
        QRectF cropLocal(0, 0, dw, dh);
        return local.intersected(cropLocal);
    }
    // Axis-aligned crop: subtract crop origin.
    QRectF local = oriented.translated(-crop.x(), -crop.y());
    QRectF cropLocal(0, 0, dw, dh);
    return local.intersected(cropLocal);
}

QRectF mapDisplayRectToSource(const QRectF &displayRect, const QSize &native,
                              const Value &x)
{
    if (!isPositiveSize(native) || displayRect.isEmpty()) {
        return {};
    }
    QSize orientedSize = swapsAspect(x) ? QSize(native.height(), native.width())
                                        : native;
    QPointF orientedPts[4];
    if (x.hasCrop && !x.cropRect.isEmpty() && hasFreeCropRotation(x)) {
        const QRect crop = x.cropRect.normalized();
        const qreal dw = crop.width();
        const qreal dh = crop.height();
        const QPointF srcCenter(crop.center());
        // Inverse of materialize free-rot: from display → oriented.
        QTransform inv;
        inv.translate(srcCenter.x(), srcCenter.y());
        inv.rotate(x.cropRotation);
        inv.translate(-dw / 2.0, -dh / 2.0);
        const QPointF d[4] = {
            displayRect.topLeft(),
            displayRect.topRight(),
            displayRect.bottomRight(),
            displayRect.bottomLeft(),
        };
        for (int i = 0; i < 4; ++i) {
            orientedPts[i] = inv.map(d[i]);
        }
    } else {
        QRectF oriented = displayRect;
        if (x.hasCrop && !x.cropRect.isEmpty()) {
            const QRect crop = x.cropRect.normalized();
            oriented = displayRect.translated(crop.x(), crop.y());
        }
        orientedPts[0] = oriented.topLeft();
        orientedPts[1] = oriented.topRight();
        orientedPts[2] = oriented.bottomRight();
        orientedPts[3] = oriented.bottomLeft();
    }
    QPointF s[4];
    for (int i = 0; i < 4; ++i) {
        s[i] = orientedPointToSource(orientedPts[i], orientedSize, x);
    }
    return aabbFromCorners(s);
}

QTransform sourceToDisplayTransform(const QSize &native, const Value &x)
{
    if (!isPositiveSize(native)) {
        return {};
    }
    // Build the affine map from three basis points so the result cannot
    // disagree with sourcePointToOriented (no QTransform multiply-order
    // traps). Pipeline: flips → quarter turns → crop, same as
    // mapSourceRectToDisplay / materializeDisplay.
    //
    // Prior left-multiply composition (op * t) produced turn-then-flip on
    // hFlip+90 (TL 28,10); sourcePointToOriented is flip-then-turn (TL 52,70).
    auto mapPoint = [&](QPointF p) -> QPointF {
        p = sourcePointToOriented(p, native, x);
        if (!x.hasCrop || x.cropRect.isEmpty()) {
            return p;
        }
        const QRect crop = x.cropRect.normalized();
        if (hasFreeCropRotation(x)) {
            const qreal dw = crop.width();
            const qreal dh = crop.height();
            const QPointF srcCenter(crop.center());
            QTransform c;
            c.translate(dw / 2.0, dh / 2.0);
            c.rotate(-x.cropRotation);
            c.translate(-srcCenter.x(), -srcCenter.y());
            return c.map(p);
        }
        return QPointF(p.x() - crop.x(), p.y() - crop.y());
    };

    const QPointF o = mapPoint(QPointF(0, 0));
    const QPointF ox = mapPoint(QPointF(1, 0));
    const QPointF oy = mapPoint(QPointF(0, 1));
    // Affine: (x,y) → o + x*(ox-o) + y*(oy-o)
    // QTransform(m11, m12, m21, m22, dx, dy):
    //   x' = m11*x + m21*y + dx
    //   y' = m12*x + m22*y + dy
    const qreal m11 = ox.x() - o.x();
    const qreal m12 = ox.y() - o.y();
    const qreal m21 = oy.x() - o.x();
    const qreal m22 = oy.y() - o.y();
    return QTransform(m11, m12, m21, m22, o.x(), o.y());
}

} // namespace ContentXform
