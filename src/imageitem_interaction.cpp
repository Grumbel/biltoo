// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"
#include "placementlinear.h"
#include "imageview.h"

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLineF>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {
constexpr qreal kHandleScreenPx = 16.0;      // scale/rotate markers in *viewport* px (grow on hover)
constexpr qreal kRotateOffsetPx = 36.0;      // rotate handle distance from edge (viewport px)
// Chrome buttons (flip / raise / lower / reset): larger + roomier.
constexpr qreal kChromeBtnScreenPx = 34.0;   // diameter in viewport px
constexpr qreal kChromeHitScreenPx = 28.0;   // hit radius in viewport px
// Outside offset from the visual right edge to the button column centre.
constexpr qreal kChromeOutsidePx = 18.0;     // a little more air from the frame
constexpr qreal kChromeBtnGapPx = 14.0;      // gap within a chrome button group
constexpr qreal kChromeClearPx = 16.0;       // min air from group edge to rotate knob
constexpr qreal kChromeGroupGapPx = 22.0;    // extra gap between upper/lower groups (rotate lives here)
constexpr int kChromeUpperCount = 4;         // flip / flip / 90°CCW / 90°CW
constexpr int kChromeLowerCount = 5;         // raise / lower / 1:1 / 0° / shear
constexpr int kChromeCount = kChromeUpperCount + kChromeLowerCount;
// Opacity track length (along the left edge) and thickness (perpendicular).
constexpr qreal kSliderWidthPx = 100.0;   // track length in viewport px
constexpr qreal kSliderHeightPx = 10.0;   // track thickness in viewport px
// Outside offset from the visual left edge to the opacity track centre-line.
constexpr qreal kSliderOutsidePx = 18.0;
// Min air between opacity track and left scale/rotate clearance along the edge.
constexpr qreal kSliderClearPx = 16.0;
// Skip detailed chrome only when the frame is truly a few pixels across.
constexpr qreal kMinFrameDiagPx = 16.0;

struct FrameViewGeom {
    QPointF tl, tr, br, bl, center;
    QPointF midTop, midRight, midBottom, midLeft;
    QPointF dirTop, dirRight, dirBottom, dirLeft;
    QPointF outTop, outRight, outBottom, outLeft;
};

QPointF unitOr(const QPointF &v, const QPointF &fallback = QPointF(1, 0))
{
    const qreal len = qHypot(v.x(), v.y());
    return len > 1e-6 ? v / len : fallback;
}

FrameViewGeom makeFrameViewGeom(const QPointF &tl, const QPointF &tr,
                                const QPointF &br, const QPointF &bl)
{
    FrameViewGeom g;
    g.tl = tl;
    g.tr = tr;
    g.br = br;
    g.bl = bl;
    g.center = (tl + tr + br + bl) * 0.25;
    g.dirTop = unitOr(tr - tl);
    g.dirRight = unitOr(br - tr);
    g.dirBottom = unitOr(bl - br);
    g.dirLeft = unitOr(tl - bl);
    g.midTop = (tl + tr) * 0.5;
    g.midRight = (tr + br) * 0.5;
    g.midBottom = (br + bl) * 0.5;
    g.midLeft = (bl + tl) * 0.5;
    auto outward = [&](const QPointF &mid, const QPointF &along) {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - g.center) < 0) {
            n = -n;
        }
        return n;
    };
    g.outTop = outward(g.midTop, g.dirTop);
    g.outRight = outward(g.midRight, g.dirRight);
    g.outBottom = outward(g.midBottom, g.dirBottom);
    g.outLeft = outward(g.midLeft, g.dirLeft);
    return g;
}

// Top-right outside column. Stack runs along the right edge direction starting
// near the top-right corner. If the stack would collide with the right rotate
// knob, the whole column is shifted further "up" (toward / past the top edge).
// Two chrome groups outside the *rotated* right edge, split around the free-
// rotate knob (along-edge layout — tracks the content frame).
//   upper: FlipH, FlipV, Rotate90CCW, Rotate90CW  — prefer top of edge
//   lower: Raise, Lower, ResetScale, ResetRotation, ResetShear — prefer bottom of edge
void chromeCentersView(const FrameViewGeom &g, QPointF outCenters[kChromeCount])
{
    const qreal btn = kChromeBtnScreenPx;
    const qreal step = btn + kChromeBtnGapPx;
    const qreal colOffset = kChromeOutsidePx + btn * 0.5;
    const QPointF colBase = g.tr + g.outRight * colOffset;
    const QPointF along = g.dirRight; // top → bottom along the right edge

    const QPointF rotR = g.midRight + g.outRight * kRotateOffsetPx;
    const qreal rotClear = kHandleScreenPx * 0.5 + kChromeClearPx + btn * 0.5;
    auto distAlong = [&](const QPointF &p) {
        return QPointF::dotProduct(p - colBase, along);
    };
    const qreal rotAlong = distAlong(rotR);
    const qreal lateral = qAbs(colOffset - kRotateOffsetPx);
    const qreal needAlongClear = qMax(0.0, rotClear - lateral);

    // Reserved band around the free-rotate knob.
    const qreal upperLastAlong = rotAlong - needAlongClear - kChromeGroupGapPx * 0.5;
    const qreal lowerFirstAlong = rotAlong + needAlongClear + kChromeGroupGapPx * 0.5;

    // Upper group: prefer flush with the top-right corner when it fits above
    // the reserved band; otherwise pack against the band from above.
    const qreal preferTop = btn * 0.5 + 4.0;
    qreal firstUpper = preferTop;
    if (preferTop + (kChromeUpperCount - 1) * step > upperLastAlong) {
        firstUpper = upperLastAlong - (kChromeUpperCount - 1) * step;
    }

    // Lower group: prefer flush with the bottom-right corner when it fits below
    // the reserved band; otherwise pack against the band from below.
    const qreal edgeLen = distAlong(g.br + g.outRight * colOffset);
    const qreal preferBottomFirst = edgeLen - ((kChromeLowerCount - 1) * step + btn * 0.5 + 4.0);
    qreal firstLower = preferBottomFirst;
    if (preferBottomFirst < lowerFirstAlong) {
        firstLower = lowerFirstAlong;
    }

    for (int i = 0; i < kChromeUpperCount; ++i) {
        outCenters[i] = colBase + along * (firstUpper + i * step);
    }
    for (int i = 0; i < kChromeLowerCount; ++i) {
        outCenters[kChromeUpperCount + i] = colBase + along * (firstLower + i * step);
    }
}

// Bottom-right outside opacity track. Returns endpoints a→b along the bottom
// edge direction. Left end is kept clear of the bottom scale bar and bottom
// rotate knob; when the preferred right-aligned position would collide, the
// whole track shifts toward the bottom-right corner / further right.
void opacityTrackView(const FrameViewGeom &g, QPointF *aOut, QPointF *bOut)
{
    // Vertical track outside the *left* edge — same adaptive idea as
    // chromeCentersView lower group: constant size, prefer bottom, pack
    // against the mid-edge rotate clearance when the free span is tight.
    //
    // a = bottom end (opacity 5%), b = top end (opacity 100%).
    // Track length is always kSliderWidthPx (never shrinks).
    //
    // Free span = [cornerMargin, maxTop] where maxTop is just below the left
    // free-rotate knob. If the full track fits there, bottom-anchor it.
    // Otherwise pin the top to maxTop and keep full length (bottom may extend
    // past the image bottom), matching how chrome buttons spill when cramped.
    const QPointF alongUp = g.dirLeft; // bl → tl
    const qreal outDist = kSliderOutsidePx + kSliderHeightPx * 0.5;
    const qreal trackLen = kSliderWidthPx;
    const qreal cornerMargin = kHandleScreenPx * 0.6;

    auto projFromBl = [&](const QPointF &p) {
        return QPointF::dotProduct(p - g.bl, alongUp);
    };

    const QPointF rotL = g.midLeft + g.outLeft * kRotateOffsetPx;
    const qreal rotAlong = projFromBl(rotL);
    const qreal needClear = kHandleScreenPx * 0.5 + kSliderClearPx;
    const qreal maxTop = rotAlong - needClear;

    // Prefer bottom-anchored (clear of corner scale handle).
    qreal aAlong = cornerMargin;
    qreal bAlong = aAlong + trackLen;
    if (bAlong > maxTop) {
        // Not enough free space under the rotate knob: pin top to maxTop,
        // keep full track length (extends below the frame if needed).
        bAlong = maxTop;
        aAlong = bAlong - trackLen;
    }

    const QPointF origin = g.bl + g.outLeft * outDist;
    *aOut = origin + alongUp * aAlong;
    *bOut = origin + alongUp * bAlong;
}
} // namespace

QRectF ImageItem::boundingRect() const
{
    // Gallery Grid-Crop: report the cell, not the full pixmap, so layout/hit-tests match.
    if (!m_galleryCellSize.isEmpty()) {
        const QRectF clip = galleryClipLocal();
        if (!clip.isEmpty()) {
            return clip;
        }
    }
    // Modest expansion for external handles when selected. Precise hit-testing is
    // view-owned (viewport distances); this AABB only prevents scene clipping of
    // the painted chrome. Clamp pad so near-zero item scale cannot explode the
    // local rect to infinity (HANDLES.md).
    QRectF r = contentRect();
    if (isSelected() && m_interactive) {
        // Modest constant local pad only. Do not divide by view/item scale:
        // zoom used to change this AABB and QGraphicsView would scroll to keep
        // the selected item on screen, fighting free-form pan/zoom. Chrome is
        // painted in viewport space; hit-testing is view-owned.
        const qreal content = qMax(r.width(), r.height());
        const qreal pad = qMin(96.0, qMax(32.0, content * 0.12));
        r.adjust(-pad, -pad, pad, pad);
    }
    return r;
}

QPainterPath ImageItem::shape() const
{
    // Content body only. Transform chrome is hit-tested by ImageView in viewport
    // space (selected-item loop + handleAt view-pixel distances). Expanding the
    // item shape with /deviceScaleMin local radii caused the path to explode when
    // scale approached zero (HANDLES.md). External handles remain reachable
    // because the view queries selected items explicitly.
    QPainterPath path;
    if (!m_galleryCellSize.isEmpty()) {
        const QRectF clip = galleryClipLocal();
        if (!clip.isEmpty()) {
            path.addRect(clip);
            return path;
        }
    }
    path.addRect(contentRect());
    return path;
}

QPointF ImageItem::sceneToViewPx(const QPointF &scenePt) const
{
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            return QPointF(views.first()->mapFromScene(scenePt));
        }
    }
    return scenePt;
}

QPointF ImageItem::localToViewPx(const QPointF &local) const
{
    return sceneToViewPx(mapToScene(local));
}

static void singularValues2x2(qreal a, qreal b, qreal c, qreal d, qreal *sMax, qreal *sMin)
{
    // Singular values of [[a,b],[c,d]] = sqrt(eigenvalues of M^T M).
    const qreal e11 = a * a + c * c;
    const qreal e22 = b * b + d * d;
    const qreal e12 = a * b + c * d;
    const qreal tr = e11 + e22;
    const qreal disc = qMax(0.0, (e11 - e22) * (e11 - e22) + 4.0 * e12 * e12);
    const qreal root = qSqrt(disc);
    const qreal ev1 = qMax(0.0, 0.5 * (tr + root));
    const qreal ev2 = qMax(0.0, 0.5 * (tr - root));
    *sMax = qMax(qSqrt(ev1), qSqrt(ev2));
    *sMin = qMax(1e-6, qMin(qSqrt(ev1), qSqrt(ev2)));
}

qreal ImageItem::screenScale() const
{
    // Max stretch of local→view (draw chrome ~constant on screen).
    QTransform t = transform();
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            t = views.first()->transform() * t;
        }
    }
    qreal sMax = 1.0, sMin = 1.0;
    singularValues2x2(t.m11(), t.m12(), t.m21(), t.m22(), &sMax, &sMin);
    return qMax(0.01, sMax);
}

qreal ImageItem::deviceScaleMin() const
{
    // Min stretch of local→view: local radius covering a screen-pixel disc
    // under rotation / anisotropic scale (critical for shape() and delivery).
    QTransform t = transform();
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            t = views.first()->transform() * t;
        }
    }
    qreal sMax = 1.0, sMin = 1.0;
    singularValues2x2(t.m11(), t.m12(), t.m21(), t.m22(), &sMax, &sMin);
    return sMin;
}

qreal ImageItem::handleDrawSize() const
{
    // Constant size in screen pixels regardless of item or view zoom
    return kHandleScreenPx / screenScale();
}

qreal ImageItem::handleHitRadius() const
{
    return handleDrawSize() * 1.2;
}

void ImageItem::updateHandleLayout()
{
    prepareGeometryChange();
    update();
}

bool ImageItem::isChromeHandle(Handle h) const
{
    return h == Handle::FlipH || h == Handle::FlipV
        || h == Handle::Rotate90CCW || h == Handle::Rotate90CW
        || h == Handle::Raise || h == Handle::Lower
        || h == Handle::ResetScale || h == Handle::ResetRotation
        || h == Handle::ResetShear
        || h == Handle::OpacitySlider;
}

bool ImageItem::isRotateHandle(Handle h) const
{
    return h == Handle::RotateTop || h == Handle::RotateRight
        || h == Handle::RotateBottom || h == Handle::RotateLeft;
}

bool ImageItem::isCornerScaleHandle(Handle h) const
{
    return h == Handle::ScaleTopLeft || h == Handle::ScaleTopRight
        || h == Handle::ScaleBottomLeft || h == Handle::ScaleBottomRight;
}

bool ImageItem::isEdgeScaleHandle(Handle h) const
{
    return h == Handle::ScaleTop || h == Handle::ScaleRight
        || h == Handle::ScaleBottom || h == Handle::ScaleLeft;
}

bool ImageItem::isScaleHandle(Handle h) const
{
    return isCornerScaleHandle(h) || isEdgeScaleHandle(h);
}

bool ImageItem::isShearHandle(Handle h) const
{
    return h == Handle::ShearTop || h == Handle::ShearBottom
        || h == Handle::ShearLeft || h == Handle::ShearRight;
}

QPointF ImageItem::scaleAnchorLocal(Handle h) const
{
    // Opposite corner/edge — kept fixed when not scaling from the centre.
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    switch (h) {
    case Handle::ScaleTopLeft:
        return r.bottomRight();
    case Handle::ScaleTopRight:
        return r.bottomLeft();
    case Handle::ScaleBottomLeft:
        return r.topRight();
    case Handle::ScaleBottomRight:
        return r.topLeft();
    case Handle::ScaleTop:
        return QPointF(r.center().x(), r.bottom());
    case Handle::ScaleBottom:
        return QPointF(r.center().x(), r.top());
    case Handle::ScaleLeft:
        return QPointF(r.right(), r.center().y());
    case Handle::ScaleRight:
        return QPointF(r.left(), r.center().y());
    case Handle::ShearTop:
        return QPointF(r.center().x(), r.bottom());
    case Handle::ShearBottom:
        return QPointF(r.center().x(), r.top());
    case Handle::ShearLeft:
        return QPointF(r.right(), r.center().y());
    case Handle::ShearRight:
        return QPointF(r.left(), r.center().y());
    default:
        return r.center();
    }
}

void ImageItem::drawCornerBracket(QPainter *painter, const QPointF &c,
                                   qreal dx, qreal dy, qreal armPx, bool hot) const
{
    // @p dx/@p dy are unit directions in *device* space along the visual edges.
    QPen pen(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
    pen.setCosmetic(true);
    pen.setWidthF(hot ? 2.5 : 2.0);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(c + QPointF(dx * armPx, dy * 0.0), c);
    painter->drawLine(c, c + QPointF(dx * 0.0, dy * armPx));
}

void ImageItem::applyScaleHandleDrag(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    // Corner and edge (H/V) handles share modifier semantics (drawing-program usual):
    // default scales about the opposite corner/edge; Ctrl or Shift about centre.
    //
    // Edge stretch uses scene-space projections onto the *press-time* image axes
    // so the ratio stays stable when scale approaches the clamp (HANDLES.md).
    // Local ratios after each setItemScale amplify noise near zero.
    const bool modifier = mods & (Qt::ControlModifier | Qt::ShiftModifier);
    const bool fromCenter = modifier;
    const QPointF itemCentre = this->scenePos();
    constexpr qreal kMinDist = 1.0; // scene px; below this ignore the sample

    if (isCornerScaleHandle(m_activeHandle)) {
        if (fromCenter) {
            const qreal d0 = QLineF(itemCentre, m_pressScenePos).length();
            const qreal d1 = QLineF(itemCentre, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
                setItemShear(m_pressShear);
            }
        } else {
            const QPointF anchor = m_pressAnchorScene;
            const qreal d0 = QLineF(anchor, m_pressScenePos).length();
            const qreal d1 = QLineF(anchor, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
                setItemShear(m_pressShear);
                const QPointF now = mapToScene(m_pressAnchorLocal);
                setPos(pos() + (anchor - now));
            }
        }
        return;
    }

    if (isEdgeScaleHandle(m_activeHandle)) {
        // Image axes in *scene* at press — must match PlacementLinear/Qt (not a
        // textbook CCW formula; Qt rotate is clockwise with Y-down).
        const QTransform Lpress = PlacementLinear::make(
            m_pressScaleX, m_pressScaleY, m_pressShear, m_pressRotation);
        const QPointF axisX = Lpress.map(QPointF(1.0, 0.0));
        const QPointF axisY = Lpress.map(QPointF(0.0, 1.0));

        qreal sx = m_pressScaleX;
        qreal sy = m_pressScaleY;
        const bool stretchX = (m_activeHandle == Handle::ScaleLeft
                               || m_activeHandle == Handle::ScaleRight);

        if (fromCenter) {
            const QPointF v0 = m_pressScenePos - itemCentre;
            const QPointF v1 = scenePos - itemCentre;
            if (stretchX) {
                const qreal len0 = QPointF::dotProduct(v0, axisX) / qMax(1e-9, QPointF::dotProduct(axisX, axisX));
                const qreal len1 = QPointF::dotProduct(v1, axisX) / qMax(1e-9, QPointF::dotProduct(axisX, axisX));
                // len is in "press-scale units" of half-width; recover scale factor.
                if (qAbs(len0) > 1e-6) {
                    sx = m_pressScaleX * (qAbs(len1) / qAbs(len0));
                }
            } else {
                const qreal len0 = QPointF::dotProduct(v0, axisY) / qMax(1e-9, QPointF::dotProduct(axisY, axisY));
                const qreal len1 = QPointF::dotProduct(v1, axisY) / qMax(1e-9, QPointF::dotProduct(axisY, axisY));
                if (qAbs(len0) > 1e-6) {
                    sy = m_pressScaleY * (qAbs(len1) / qAbs(len0));
                }
            }
        } else {
            // Anchor fixed: project (pointer - anchor) onto the stretch axis.
            const QPointF anchor = m_pressAnchorScene;
            const QPointF v0 = m_pressScenePos - anchor;
            const QPointF v1 = scenePos - anchor;
            if (stretchX) {
                const qreal uAxis = qMax(1e-9, QPointF::dotProduct(axisX, axisX));
                const qreal len0 = QPointF::dotProduct(v0, axisX) / uAxis;
                const qreal len1 = QPointF::dotProduct(v1, axisX) / uAxis;
                if (qAbs(len0) > 1e-6) {
                    sx = m_pressScaleX * (qAbs(len1) / qAbs(len0));
                }
            } else {
                const qreal uAxis = qMax(1e-9, QPointF::dotProduct(axisY, axisY));
                const qreal len0 = QPointF::dotProduct(v0, axisY) / uAxis;
                const qreal len1 = QPointF::dotProduct(v1, axisY) / uAxis;
                if (qAbs(len0) > 1e-6) {
                    sy = m_pressScaleY * (qAbs(len1) / qAbs(len0));
                }
            }
        }
        setItemScale(sx, sy);
        // Scale-only drag must not disturb shear.
        setItemShear(m_pressShear);
        if (!fromCenter) {
            const QPointF now = mapToScene(m_pressAnchorLocal);
            setPos(pos() + (m_pressAnchorScene - now));
        }
    }
}

void ImageItem::applyShearHandleDrag(const QPointF &scenePos)
{
    // Horizontal shear k in local space: H = [[1,k],[0,1]] inside R·H·S.
    // Drag projects onto scene image of local +X (Qt rotation convention).
    // Opposite edge fixed; denom uses half-height so k matches make().
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal halfH = qMax(1e-6, r.height() * 0.5);
    const QTransform Ldir = PlacementLinear::make(1.0, 1.0, 0.0, m_pressRotation);
    QPointF dirX = Ldir.map(QPointF(1.0, 0.0));
    const qreal dirLen = qHypot(dirX.x(), dirX.y());
    if (dirLen > 1e-9) {
        dirX /= dirLen;
    }
    const QPointF anchor = m_pressAnchorScene;
    const QPointF v0 = m_pressScenePos - anchor;
    const QPointF v1 = scenePos - anchor;
    const qreal len0 = QPointF::dotProduct(v0, dirX);
    const qreal len1 = QPointF::dotProduct(v1, dirX);
    const qreal denom = qMax(1e-6, m_pressScaleY * halfH);
    const qreal delta = (len1 - len0) / denom;
    qreal k = m_pressShear;
    switch (m_activeHandle) {
    case Handle::ShearTop:
        // y_top < 0: increasing k moves top toward -localX.
        k = m_pressShear - delta;
        break;
    case Handle::ShearBottom:
        k = m_pressShear + delta;
        break;
    case Handle::ShearLeft:
        // Same horizontal shear; left grip uses opposite sign of top for a
        // consistent "drag with the edge" feel.
        k = m_pressShear - delta;
        break;
    case Handle::ShearRight:
        k = m_pressShear + delta;
        break;
    default:
        break;
    }
    setItemShear(k);
    const QPointF now = mapToScene(m_pressAnchorLocal);
    setPos(pos() + (anchor - now));
}

qreal ImageItem::chromeButtonSize() const
{
    return kChromeBtnScreenPx / screenScale();
}

QRectF ImageItem::opacitySliderRect() const
{
    // Approximate local rect for legacy callers only. Paint / hit / drag use
    // opacityTrackView() in viewport space (left outside, vertical, adaptive).
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal h = qMin(r.height() * 0.4, r.height());
    const qreal w = qMin(8.0, r.width() * 0.1);
    return QRectF(r.left() - w - 4.0, r.bottom() - h - 4.0, w, h);
}

void ImageItem::setOpacityFromSliderPos(const QPointF &scenePos)
{
    QGraphicsView *view = nullptr;
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty()) {
            view = views.first();
        }
    }
    if (!view) {
        return;
    }
    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };
    const QRectF localRect = QGraphicsPixmapItem::boundingRect();
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    if (QLineF(tl, br).length() < kMinFrameDiagPx) {
        return;
    }
    const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
    QPointF a, b;
    opacityTrackView(fg, &a, &b);
    const QPointF p = sceneToViewPx(scenePos);
    const QPointF ab = b - a;
    const qreal ab2 = QPointF::dotProduct(ab, ab);
    qreal tval = 0.0;
    if (ab2 > 1e-6) {
        tval = qBound(0.0, QPointF::dotProduct(p - a, ab) / ab2, 1.0);
    }
    setItemOpacity(0.05 + tval * 0.95);
}

QList<ImageItem::Handle> ImageItem::activeHandles() const
{
    QList<Handle> handles;
    if (m_scaleHandlesEnabled) {
        handles << Handle::ScaleTopLeft << Handle::ScaleTopRight
                << Handle::ScaleBottomLeft << Handle::ScaleBottomRight
                << Handle::ScaleTop << Handle::ScaleRight
                << Handle::ScaleBottom << Handle::ScaleLeft
                << Handle::ShearTop << Handle::ShearBottom
                << Handle::ShearLeft << Handle::ShearRight;
    }
    handles << Handle::RotateTop << Handle::RotateRight
            << Handle::RotateBottom << Handle::RotateLeft
            << Handle::FlipH << Handle::FlipV
            << Handle::Rotate90CCW << Handle::Rotate90CW
            << Handle::Raise << Handle::Lower
            << Handle::ResetScale << Handle::ResetRotation << Handle::ResetShear
            << Handle::OpacitySlider;
    return handles;
}

bool ImageItem::isUprightChromeHandle(Handle h) const
{
    // Raise/Lower glyphs stay screen-upright so "up" always means raise.
    return h == Handle::Raise || h == Handle::Lower;
}

qreal ImageItem::handleDistanceScreenPx(Handle h, const QPointF &itemPos) const
{
    return QLineF(localToViewPx(handleCenter(h)), localToViewPx(itemPos)).length();
}

QPointF ImageItem::handleCenter(Handle h) const
{
    // Local attachment points. Scale handles sit on the pixmap rect (align + rotate
    // with the image). Rotate / chrome use *clamped* local offsets so near-zero
    // scale cannot send centres to infinity; paint and hit-testing place the true
    // constant-screen-distance positions in viewport space (see paintInteractionChrome
    // and handleAt). Opacity uses the interior track.
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal content = qMax(r.width(), r.height());
    const qreal maxOff = qMax(40.0, content * 0.75);

    auto axisScreenPerLocal = [this](const QPointF &localAxis) -> qreal {
        const QPointF o = localToViewPx(QPointF(0, 0));
        const QPointF p = localToViewPx(localAxis);
        const qreal len = QLineF(o, p).length();
        return qMax(1e-6, len);
    };
    const qreal sx = axisScreenPerLocal(QPointF(1, 0));
    const qreal sy = axisScreenPerLocal(QPointF(0, 1));

    const qreal rotOffX = qMin(kRotateOffsetPx / sx, maxOff);
    const qreal rotOffY = qMin(kRotateOffsetPx / sy, maxOff);
    const qreal btn = qMin(kChromeBtnScreenPx / sy, maxOff / 6.0);
    const qreal gap = qMin(kChromeBtnGapPx / sy, maxOff / 12.0);
    const qreal outX = qMin((kChromeOutsidePx + kChromeBtnScreenPx * 0.5) / sx, maxOff / 3.0);

    // Outside top-right (approx local; paint/hit use chromeCentersView).
    const qreal stackX = r.right() + outX;
    const qreal stackTop = r.top() + 4.0 / sy;
    auto chromeBtnCenter = [&](int index) {
        // Approximate local centres; precise hit/paint use chromeCentersView.
        return QPointF(stackX, stackTop + index * (btn + gap) + btn / 2.0);
    };

    switch (h) {
    case Handle::ScaleTopLeft:
        return r.topLeft();
    case Handle::ScaleTopRight:
        return r.topRight();
    case Handle::ScaleBottomLeft:
        return r.bottomLeft();
    case Handle::ScaleBottomRight:
        return r.bottomRight();
    case Handle::ScaleTop:
        return QPointF(cx, r.top());
    case Handle::ScaleRight:
        return QPointF(r.right(), cy);
    case Handle::ScaleBottom:
        return QPointF(cx, r.bottom());
    case Handle::ScaleLeft:
        return QPointF(r.left(), cy);
    case Handle::ShearTop:
        // Offset along edge so scale mid-handle and shear grip do not coincide.
        return QPointF(cx - r.width() * 0.22, r.top());
    case Handle::ShearBottom:
        return QPointF(cx + r.width() * 0.22, r.bottom());
    case Handle::ShearLeft:
        return QPointF(r.left(), cy - r.height() * 0.22);
    case Handle::ShearRight:
        return QPointF(r.right(), cy + r.height() * 0.22);
    case Handle::RotateTop:
        return QPointF(cx, r.top() - rotOffY);
    case Handle::RotateRight:
        return QPointF(r.right() + rotOffX, cy);
    case Handle::RotateBottom:
        return QPointF(cx, r.bottom() + rotOffY);
    case Handle::RotateLeft:
        return QPointF(r.left() - rotOffX, cy);
    case Handle::FlipH:
        return chromeBtnCenter(0);
    case Handle::FlipV:
        return chromeBtnCenter(1);
    case Handle::Rotate90CCW:
        return chromeBtnCenter(2);
    case Handle::Rotate90CW:
        return chromeBtnCenter(3);
    case Handle::Raise:
        return chromeBtnCenter(4);
    case Handle::Lower:
        return chromeBtnCenter(5);
    case Handle::ResetScale:
        return chromeBtnCenter(6);
    case Handle::ResetRotation:
        return chromeBtnCenter(7);
    case Handle::ResetShear:
        return chromeBtnCenter(8);
    case Handle::OpacitySlider:
        return opacitySliderRect().center();
    default:
        return QPointF();
    }
}

ImageItem::Handle ImageItem::handleAt(const QPointF &itemPos) const
{
    if (!isSelected() || !m_interactive) {
        return Handle::None;
    }

    // All distances in viewport pixels so rotation / anisotropic scale / near-zero
    // item scale cannot break hit testing (HANDLES.md). Centres for rotate and
    // chrome match paintInteractionChrome exactly.
    QGraphicsView *view = nullptr;
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty()) {
            view = views.first();
        }
    }
    if (!view) {
        return Handle::None;
    }

    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };
    auto norm = [](QPointF v) -> QPointF {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };

    const QRectF localRect = QGraphicsPixmapItem::boundingRect();
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    const QPointF centerV = toView(localRect.center());
    const QPointF p = toView(itemPos);

    // Degenerate frame: no chrome hits (matches paint early-out).
    if (QLineF(tl, br).length() < kMinFrameDiagPx) {
        return Handle::None;
    }

    const QPointF dirTop = norm(tr - tl);
    const QPointF dirRight = norm(br - tr);
    const QPointF dirBottom = norm(bl - br);
    const QPointF dirLeft = norm(tl - bl);
    auto outward = [&](const QPointF &mid, const QPointF &along) -> QPointF {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - centerV) < 0) {
            n = -n;
        }
        return n;
    };
    const QPointF midTop = (tl + tr) * 0.5;
    const QPointF midRight = (tr + br) * 0.5;
    const QPointF midBottom = (br + bl) * 0.5;
    const QPointF midLeft = (bl + tl) * 0.5;
    const QPointF outTop = outward(midTop, dirTop);
    const QPointF outRight = outward(midRight, dirRight);
    const QPointF outBottom = outward(midBottom, dirBottom);
    const QPointF outLeft = outward(midLeft, dirLeft);

    // Opacity + chrome: same adaptive outside layout as paint.
    {
        const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
        QPointF a, b;
        opacityTrackView(fg, &a, &b);
        const QPointF ab = b - a;
        const qreal ab2 = QPointF::dotProduct(ab, ab);
        qreal tt = 0.0;
        if (ab2 > 1e-6) {
            tt = qBound(0.0, QPointF::dotProduct(p - a, ab) / ab2, 1.0);
        }
        if (QLineF(p, a + ab * tt).length() <= kChromeHitScreenPx) {
            return Handle::OpacitySlider;
        }

        QPointF centers[kChromeCount];
        chromeCentersView(fg, centers);
        const Handle chromeHandles[] = {
            Handle::FlipH, Handle::FlipV, Handle::Rotate90CCW, Handle::Rotate90CW,
            Handle::Raise, Handle::Lower, Handle::ResetScale, Handle::ResetRotation,
            Handle::ResetShear
        };
        Handle best = Handle::None;
        qreal bestDist = 1e300;
        for (int i = 0; i < kChromeCount; ++i) {
            const bool isToggle = (chromeHandles[i] == Handle::FlipH
                                   || chromeHandles[i] == Handle::FlipV);
            if (isToggle) {
                // Rounded-square toggles: axis-aligned hit box (viewport px).
                const qreal half = kChromeBtnScreenPx * 0.55 + 3.0;
                const qreal dx = qAbs(p.x() - centers[i].x());
                const qreal dy = qAbs(p.y() - centers[i].y());
                if (dx <= half && dy <= half) {
                    const qreal d = qMax(dx, dy);
                    if (d <= bestDist) {
                        bestDist = d;
                        best = chromeHandles[i];
                    }
                }
            } else {
                const qreal d = QLineF(p, centers[i]).length();
                if (d <= kChromeHitScreenPx && d <= bestDist) {
                    bestDist = d;
                    best = chromeHandles[i];
                }
            }
        }
        if (best != Handle::None) {
            return best;
        }
    }

    // Scale corners + rotate knobs
    Handle best = Handle::None;
    qreal bestDist = kHandleScreenPx * 1.75;
    struct PointH {
        Handle h;
        QPointF c;
    };
    QList<PointH> points;
    if (m_scaleHandlesEnabled) {
        points << PointH{Handle::ScaleTopLeft, tl}
               << PointH{Handle::ScaleTopRight, tr}
               << PointH{Handle::ScaleBottomRight, br}
               << PointH{Handle::ScaleBottomLeft, bl};
    }
    points << PointH{Handle::RotateTop, midTop + outTop * kRotateOffsetPx}
           << PointH{Handle::RotateRight, midRight + outRight * kRotateOffsetPx}
           << PointH{Handle::RotateBottom, midBottom + outBottom * kRotateOffsetPx}
           << PointH{Handle::RotateLeft, midLeft + outLeft * kRotateOffsetPx};
    for (const PointH &ph : points) {
        const qreal d = QLineF(p, ph.c).length();
        if (d <= bestDist) {
            bestDist = d;
            best = ph.h;
        }
    }

    // Edge stretch bars (short segment around mid-edge in view space)
    if (m_scaleHandlesEnabled) {
        const qreal halfLen = kHandleScreenPx * 1.2;
        const qreal edgeHit = kHandleScreenPx * 0.85;
        struct EdgeH {
            Handle h;
            QPointF mid;
            QPointF along;
        };
        const EdgeH edges[] = {
            {Handle::ScaleTop, midTop, dirTop},
            {Handle::ScaleRight, midRight, dirRight},
            {Handle::ScaleBottom, midBottom, dirBottom},
            {Handle::ScaleLeft, midLeft, dirLeft},
        };
        for (const EdgeH &ed : edges) {
            const QPointF a = ed.mid - ed.along * halfLen;
            const QPointF b = ed.mid + ed.along * halfLen;
            const QPointF ab = b - a;
            const qreal ab2 = QPointF::dotProduct(ab, ab);
            qreal t = 0.0;
            if (ab2 > 1e-6) {
                t = qBound(0.0, QPointF::dotProduct(p - a, ab) / ab2, 1.0);
            }
            const qreal d = QLineF(p, a + ab * t).length();
            if (d <= edgeHit && d <= bestDist) {
                bestDist = d;
                best = ed.h;
            }
        }
        // Shear diamonds: offset along each edge from mid (view space).
        const qreal shearAlong = kHandleScreenPx * 2.2;
        const QPointF shearPts[] = {
            midTop - dirTop * shearAlong,
            midBottom + dirBottom * shearAlong,
            midLeft - dirLeft * shearAlong,
            midRight + dirRight * shearAlong,
        };
        const Handle shearHs[] = {
            Handle::ShearTop, Handle::ShearBottom,
            Handle::ShearLeft, Handle::ShearRight,
        };
        for (int i = 0; i < 4; ++i) {
            const qreal d = QLineF(p, shearPts[i]).length();
            if (d <= kHandleScreenPx * 1.1 && d <= bestDist) {
                bestDist = d;
                best = shearHs[i];
            }
        }
    }
    return best;
}

bool ImageItem::hasHandleAt(const QPointF &itemPos) const
{
    return handleAt(itemPos) != Handle::None;
}

void ImageItem::notifyViewStatus()
{
    if (!scene()) {
        return;
    }
    for (QGraphicsView *v : scene()->views()) {
        QMetaObject::invokeMethod(v, "refreshStatus", Qt::DirectConnection);
        // Chrome lives in the viewport paint path (flip toggles, etc.).
        if (v->viewport()) {
            v->viewport()->update();
        }
    }
}

void ImageItem::activateChromeHandle(Handle h)
{
    switch (h) {
    case Handle::FlipH:
    case Handle::FlipV:
    case Handle::Rotate90CCW:
    case Handle::Rotate90CW: {
        if (scene()) {
            for (QGraphicsView *v : scene()->views()) {
                if (auto *iv = qobject_cast<ImageView *>(v)) {
                    if (h == Handle::FlipH) {
                        iv->bakeItemFlip(this, true, false);
                    } else if (h == Handle::FlipV) {
                        iv->bakeItemFlip(this, false, true);
                    } else if (h == Handle::Rotate90CCW) {
                        iv->bakeItemRotate90(this, -1);
                    } else {
                        iv->bakeItemRotate90(this, 1);
                    }
                    notifyViewStatus();
                    return;
                }
            }
        }
        // Fallback without a view: bake pixels and toggle content indicators.
        if (h == Handle::FlipH) {
            bakeFlip(true, false);
            m_contentHFlip = !m_contentHFlip;
        } else if (h == Handle::FlipV) {
            bakeFlip(false, true);
            m_contentVFlip = !m_contentVFlip;
        } else if (h == Handle::Rotate90CCW) {
            bakeRotate90(-1);
        } else {
            bakeRotate90(1);
        }
        break;
    }
    case Handle::Raise:
    case Handle::Lower: {
        if (scene()) {
            for (QGraphicsView *v : scene()->views()) {
                if (auto *iv = qobject_cast<ImageView *>(v)) {
                    if (h == Handle::Raise) {
                        iv->raiseItem(this);
                    } else {
                        iv->lowerItem(this);
                    }
                    break;
                }
            }
        }
        break;
    }
    case Handle::ResetScale:
        setItemScale(1.0, 1.0);
        setItemShear(0.0);
        break;
    case Handle::ResetRotation:
        setItemRotation(0.0);
        break;
    case Handle::ResetShear:
        setItemShear(0.0);
        break;
    default:
        break;
    }
    // Persist session state + filmstrip (flip / 90° / reset). Raise/Lower only
    // change z-order but still refresh status.
    if (scene()) {
        for (QGraphicsView *v : scene()->views()) {
            if (auto *iv = qobject_cast<ImageView *>(v)) {
                iv->commitItemSessionEdit(this);
                break;
            }
        }
    }
    notifyViewStatus();
}

QVariant ImageItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged) {
        prepareGeometryChange();
        refreshStackingOrder();
    }
    return QGraphicsPixmapItem::itemChange(change, value);
}

void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget)
{
    const QRectF crop = galleryClipLocal();
    const bool cropped = !crop.isEmpty();

    // Pixmap at item opacity; handles always fully opaque
    {
        QStyleOptionGraphicsItem opt = *option;
        opt.state &= ~QStyle::State_Selected;
        painter->save();
        if (cropped) {
            painter->setClipRect(crop);
        }
        painter->setOpacity(m_opacity);
        if (m_source.isNull() || pixmap().isNull()) {
            // Placeholder while Gallery decode is pending or unloaded.
            painter->fillRect(contentRect(), QColor(48, 48, 48));
            painter->setPen(QPen(QColor(70, 70, 70), 0));
            painter->drawRect(contentRect());
        } else {
            QGraphicsPixmapItem::paint(painter, &opt, widget);
        }
        painter->restore();
    }

    const QRectF r = cropped ? crop : contentRect();

    // Gallery: selection frame only (classic multi-select). Hover is for HUD
    // filename, not a full-tile wash — near-fullscreen packs stay usable.
    if (!m_interactive) {
        const bool selected = option->state & QStyle::State_Selected;
        if (selected) {
            painter->save();
            painter->setOpacity(1.0);
            QPen pen(QColor(0, 180, 255), 0);
            pen.setCosmetic(true);
            pen.setWidthF(4.0);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            // Inset by half the stroke so the frame sits on the tile edge.
            painter->drawRect(r.adjusted(2.0, 2.0, -2.0, -2.0));
            painter->restore();
        }
        return;
    }

    // Selection transform chrome is painted by ImageView::paintEvent (viewport
    // device space) so handles sit above every image and stay scale-invariant.
    Q_UNUSED(r);
}


void ImageItem::paintSelectionFrame(QPainter *painter) const
{
    if (!painter || !scene()) {
        return;
    }
    const QList<QGraphicsView *> views = scene()->views();
    if (views.isEmpty()) {
        return;
    }
    QGraphicsView *view = views.first();
    const QRectF localRect = contentRect();
    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    QPolygonF poly;
    poly << tl << tr << br << bl;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(60, 140, 255, 220));
    pen.setWidthF(0);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPolygon(poly);
    painter->restore();
}

void ImageItem::paintInteractionChrome(QPainter *painter) const
{
    if (!m_interactive || !isSelected()) {
        return;
    }
    const QRectF crop = galleryClipLocal();
    const QRectF r = crop.isEmpty() ? QGraphicsPixmapItem::boundingRect() : crop;
    paintInteractionChrome(painter, r);
}

void ImageItem::setHoverHandle(Handle h)
{
    if (h == m_hoverHandle) {
        return;
    }
    m_hoverHandle = h;
    // Chrome is painted by ImageView::paintEvent; the view refreshes the
    // viewport. Avoid item-only update which would miss external handle pads.
}

void ImageItem::paintInteractionChrome(QPainter *painter, const QRectF &localRect) const
{
    // HARD RULE (AGENTS.md): draw only in viewport logical pixels.
    QGraphicsView *view = nullptr;
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty()) {
            view = views.first();
        }
    }
    if (!view || !painter) {
        return;
    }

    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };

    // Four corners of the content frame in viewport pixels.
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    const QPointF centerV = toView(localRect.center());

    auto norm = [](QPointF v) -> QPointF {
        const qreal len = qHypot(v.x(), v.y());
        if (len > 1e-6) {
            return v / len;
        }
        return QPointF(1, 0);
    };
    // Unit edge directions (view space) and outward normals (away from centre).
    const QPointF dirTop = norm(tr - tl);
    const QPointF dirRight = norm(br - tr);
    const QPointF dirBottom = norm(bl - br);
    const QPointF dirLeft = norm(tl - bl);
    auto outward = [&](const QPointF &mid, const QPointF &along) -> QPointF {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - centerV) < 0) {
            n = -n;
        }
        return n;
    };
    const QPointF midTop = (tl + tr) * 0.5;
    const QPointF midRight = (tr + br) * 0.5;
    const QPointF midBottom = (br + bl) * 0.5;
    const QPointF midLeft = (bl + tl) * 0.5;
    const QPointF outTop = outward(midTop, dirTop);
    const QPointF outRight = outward(midRight, dirRight);
    const QPointF outBottom = outward(midBottom, dirBottom);
    const QPointF outLeft = outward(midLeft, dirLeft);

    painter->save();
    painter->setOpacity(1.0);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal hs = kHandleScreenPx;

    // Degenerate frame (item scale near zero / collapsed): skip detailed chrome
    // so we never feed zero-length edges into norm() or draw Inf positions.
    // Threshold is deliberately higher than a few pixels so handles stay visible
    // while the image is still clearly on screen.
    const qreal frameDiag = QLineF(tl, br).length();
    const bool frameOk = frameDiag >= kMinFrameDiagPx;

    // Selection frame
    QPen framePen(QColor(0, 160, 255), 0);
    framePen.setCosmetic(true);
    framePen.setWidthF(1.5);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    if (frameOk) {
        painter->drawPolygon(QPolygonF({tl, tr, br, bl}));
    } else {
        // Minimal centred marker so the selection is still visible.
        painter->drawEllipse(centerV, 6.0, 6.0);
        painter->restore();
        return;
    }

    // Rotate stems + knobs: constant viewport-pixel offset along outward normal
    // of each edge (HANDLES.md). Never derive from local / sx.
    auto drawRotate = [&](Handle h, const QPointF &edgeMid, const QPointF &outN) {
        const QPointF c = edgeMid + outN * kRotateOffsetPx;
        const bool hot = (m_hoverHandle == h || m_activeHandle == h);
        QPen stem(QColor(0, 160, 255), 0);
        stem.setCosmetic(true);
        stem.setWidthF(1.25);
        painter->setPen(stem);
        painter->drawLine(edgeMid, c);
        const qreal rad = hs * (hot ? 0.52 : 0.34);
        painter->setBrush(hot ? QColor(255, 230, 80) : QColor(255, 200, 40));
        QPen hp(hot ? QColor(255, 255, 255) : QColor(40, 40, 40), 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.75 : 1.15);
        painter->setPen(hp);
        painter->drawEllipse(c, rad, rad);
    };
    drawRotate(Handle::RotateTop, midTop, outTop);
    drawRotate(Handle::RotateRight, midRight, outRight);
    drawRotate(Handle::RotateBottom, midBottom, outBottom);
    drawRotate(Handle::RotateLeft, midLeft, outLeft);

    if (m_scaleHandlesEnabled) {
        // Corner L-brackets: arms along the two adjacent edges in *view* space.
        struct Corner {
            Handle h;
            QPointF corner;
            QPointF alongA;
            QPointF alongB;
        };
        const Corner corners[] = {
            {Handle::ScaleTopLeft, tl, dirTop, -dirLeft},
            {Handle::ScaleTopRight, tr, -dirTop, dirRight},
            {Handle::ScaleBottomRight, br, dirBottom, -dirRight},
            {Handle::ScaleBottomLeft, bl, -dirBottom, dirLeft},
        };
        for (const Corner &co : corners) {
            // Single continuous L: two arms through the corner with RoundJoin so
            // the elbow is a smooth fillet. Stroke weight matches edge scale bars.
            const bool hot = (m_hoverHandle == co.h || m_activeHandle == co.h);
            const QPointF c = co.corner;
            const QPointF d1 = norm(co.alongA);
            const QPointF d2 = norm(co.alongB);
            const qreal arm = hs * (hot ? 1.7 : 1.35);
            // Match edge-bar thickness (hs * ~0.30–0.48), not a hairline.
            const qreal thick = hs * (hot ? 0.48 : 0.36);
            const QColor strokeCol = hot ? QColor(255, 255, 255) : QColor(0, 160, 255);
            QPainterPath path;
            path.moveTo(c + d1 * arm);
            path.lineTo(c);
            path.lineTo(c + d2 * arm);
            QPen hp(strokeCol, 0);
            hp.setCosmetic(true);
            hp.setWidthF(thick);
            hp.setCapStyle(Qt::RoundCap);
            hp.setJoinStyle(Qt::RoundJoin);
            painter->setPen(hp);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
            // Soft fill under the stroke so hot state reads solid like edge bars.
            if (hot) {
                QPen glow(QColor(0, 160, 255, 180), 0);
                glow.setCosmetic(true);
                glow.setWidthF(thick * 0.55);
                glow.setCapStyle(Qt::RoundCap);
                glow.setJoinStyle(Qt::RoundJoin);
                painter->setPen(glow);
                painter->drawPath(path);
            }
        }

        // Edge stretch bars along each edge, mid-edge, constant view size.
        struct EdgeBar {
            Handle h;
            QPointF mid;
            QPointF along;
        };
        const EdgeBar edges[] = {
            {Handle::ScaleTop, midTop, dirTop},
            {Handle::ScaleRight, midRight, dirRight},
            {Handle::ScaleBottom, midBottom, dirBottom},
            {Handle::ScaleLeft, midLeft, dirLeft},
        };
        for (const EdgeBar &ed : edges) {
            const bool hot = (m_hoverHandle == ed.h || m_activeHandle == ed.h);
            const QPointF along = norm(ed.along);
            const QPointF perp(-along.y(), along.x());
            const qreal len = hs * (hot ? 2.4 : 1.7);
            const qreal thick = hs * (hot ? 0.48 : 0.30);
            const QColor base = hot ? QColor(0, 160, 255, 255) : QColor(0, 160, 255, 230);
            const QColor edgeCol = hot ? QColor(255, 255, 255) : QColor(0, 120, 200);
            QPen hp(edgeCol, 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 1.6 : 1.15);
            painter->setPen(hp);
            painter->setBrush(base);
            QPolygonF bar;
            bar << ed.mid + along * (len / 2) + perp * (thick / 2)
                << ed.mid - along * (len / 2) + perp * (thick / 2)
                << ed.mid - along * (len / 2) - perp * (thick / 2)
                << ed.mid + along * (len / 2) - perp * (thick / 2);
            painter->drawPolygon(bar);
            painter->setBrush(Qt::NoBrush);
        }

        // Shear grips: small diamonds on all four edges (offset from mid).
        const qreal shearAlong = hs * 2.2;
        struct ShearD {
            Handle h;
            QPointF c;
        };
        const ShearD shears[] = {
            {Handle::ShearTop, midTop - dirTop * shearAlong},
            {Handle::ShearBottom, midBottom + dirBottom * shearAlong},
            {Handle::ShearLeft, midLeft - dirLeft * shearAlong},
            {Handle::ShearRight, midRight + dirRight * shearAlong},
        };
        for (const ShearD &sh : shears) {
            const bool hot = (m_hoverHandle == sh.h || m_activeHandle == sh.h);
            const qreal rad = hs * (hot ? 0.55 : 0.40);
            QPolygonF dia;
            dia << sh.c + QPointF(0, -rad)
                << sh.c + QPointF(rad, 0)
                << sh.c + QPointF(0, rad)
                << sh.c + QPointF(-rad, 0);
            painter->setBrush(hot ? QColor(255, 200, 60) : QColor(180, 120, 255));
            QPen hp(hot ? QColor(255, 255, 255) : QColor(60, 40, 100), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 1.6 : 1.1);
            painter->setPen(hp);
            painter->drawPolygon(dia);
            painter->setBrush(Qt::NoBrush);
        }
    }

    // Chrome buttons: top-right *outside*. Adaptive — stack shifts upward when
    // split upper/lower groups around the right rotate knob (see chromeCentersView).
    {
        const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
        QPointF centers[kChromeCount];
        chromeCentersView(fg, centers);

        // Design language (HANDLES.md):
        //   circle  = momentary action (click once)
        //   rounded square = latching toggle
        //   open strokes on the frame = geometry grips (elsewhere)
        const qreal btnR = kChromeBtnScreenPx / 2.0;
        const qreal toggleHalf = kChromeBtnScreenPx * 0.52; // half-side of toggle square

        auto drawFlipToggle = [&](Handle h, int index, bool on, const QString &glyph) {
            const QPointF c = centers[index];
            const bool hovered = (m_hoverHandle == h);
            const qreal half = toggleHalf * (hovered ? 1.08 : 1.0);
            const qreal corner = half * 0.28; // squircle-ish
            const QRectF box(c.x() - half, c.y() - half, half * 2.0, half * 2.0);
            QColor fill = on ? QColor(0, 160, 255, 245)
                             : QColor(40, 40, 40, 220);
            if (hovered && !on) {
                fill = QColor(0, 100, 180, 200);
            }
            QPen border(on || hovered ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(on ? 2.0 : (hovered ? 1.75 : 1.25));
            border.setCosmetic(true);
            painter->setPen(border);
            painter->setBrush(fill);
            painter->drawRoundedRect(box, corner, corner);
            if (on) {
                // Inner inset mark so "latched" reads differently from hover.
                QPen ring(QColor(255, 255, 255, 210));
                ring.setWidthF(1.2);
                ring.setCosmetic(true);
                painter->setPen(ring);
                painter->setBrush(Qt::NoBrush);
                const qreal inset = half * 0.28;
                painter->drawRoundedRect(box.adjusted(inset, inset, -inset, -inset),
                                         corner * 0.7, corner * 0.7);
            }
            painter->setPen(on ? QColor(255, 255, 255) : QColor(230, 230, 230));
            QFont f = painter->font();
            f.setPointSizeF(qMax(8.0, half * 0.72));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(box, Qt::AlignCenter, glyph);
        };

        auto drawBtn = [&](Handle h, int index, const QString &glyph) {
            const QPointF c = centers[index];
            const bool hovered = (m_hoverHandle == h);
            const bool active = (m_activeHandle == h);
            const qreal rad = btnR * (hovered || active ? 1.08 : 1.0);
            QColor fill = hovered || active ? QColor(0, 140, 255, 240) : QColor(50, 50, 50, 230);
            QPen border(hovered || active ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(hovered || active ? 1.75 : 1.0);
            border.setCosmetic(true);
            painter->setPen(border);
            painter->setBrush(fill);
            painter->drawEllipse(c, rad, rad);
            painter->setPen(QColor(240, 240, 240));
            QFont f = painter->font();
            f.setPointSizeF(qMax(7.0, rad * 0.55));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(QRectF(c.x() - rad, c.y() - rad, rad * 2, rad * 2),
                              Qt::AlignCenter, glyph);
        };

        drawFlipToggle(Handle::FlipH, 0, m_contentHFlip, QStringLiteral("↔"));
        drawFlipToggle(Handle::FlipV, 1, m_contentVFlip, QStringLiteral("↕"));
        drawBtn(Handle::Rotate90CCW, 2, QStringLiteral("↺"));
        drawBtn(Handle::Rotate90CW, 3, QStringLiteral("↻"));
        drawBtn(Handle::Raise, 4, QStringLiteral("↑"));
        drawBtn(Handle::Lower, 5, QStringLiteral("↓"));
        drawBtn(Handle::ResetScale, 6, QStringLiteral("1:1"));
        drawBtn(Handle::ResetRotation, 7, QStringLiteral("0°"));
        drawBtn(Handle::ResetShear, 8, QStringLiteral("//"));
    }

    // Opacity: left *outside*, vertical. Bottom end = 5%, top end = 100%.
    // Clears the left scale bar and rotate knob (see opacityTrackView).
    {
        const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
        QPointF a, b;
        opacityTrackView(fg, &a, &b);
        const QPointF ab = b - a;
        const qreal abLen = qHypot(ab.x(), ab.y());
        const QPointF along = abLen > 1e-6 ? ab / abLen : QPointF(0, -1);
        Q_UNUSED(along);
        const QPointF perp = fg.outLeft;
        const qreal thick = kSliderHeightPx;
        const bool hot = (m_hoverHandle == Handle::OpacitySlider
                          || m_activeHandle == Handle::OpacitySlider);
        const qreal tval = (m_opacity - 0.05) / 0.95;
        QPen trackPen(hot ? QColor(200, 180, 255) : QColor(120, 100, 160), 0);
        trackPen.setCosmetic(true);
        trackPen.setWidthF(1.15);
        painter->setPen(trackPen);
        painter->setBrush(QColor(40, 40, 40, hot ? 230 : 200));
        QPolygonF trackPoly;
        trackPoly << a + perp * (thick / 2) << b + perp * (thick / 2)
                  << b - perp * (thick / 2) << a - perp * (thick / 2);
        painter->drawPolygon(trackPoly);
        const QPointF mid = a + ab * qBound(0.0, tval, 1.0);
        painter->setBrush(QColor(140, 100, 200, 230));
        QPolygonF filled;
        filled << a + perp * (thick / 2) << mid + perp * (thick / 2)
               << mid - perp * (thick / 2) << a - perp * (thick / 2);
        painter->drawPolygon(filled);
        painter->setBrush(QColor(230, 230, 230));
        painter->setPen(QPen(QColor(30, 30, 30), 0));
        painter->drawEllipse(mid, 5.5, 5.5);
    }

    painter->restore();
}


bool ImageItem::beginHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mods);
    if (!m_interactive || !isSelected()) {
        return false;
    }
    const Handle h = handleAt(mapFromScene(scenePos));
    if (h == Handle::None) {
        return false;
    }
    if (h == Handle::FlipH || h == Handle::FlipV
        || h == Handle::Rotate90CCW || h == Handle::Rotate90CW
        || h == Handle::Raise || h == Handle::Lower
        || h == Handle::ResetScale || h == Handle::ResetRotation
        || h == Handle::ResetShear) {
        activateChromeHandle(h);
        return true;
    }
    m_activeHandle = h;
    m_pressScenePos = scenePos;
    m_pressScaleX = m_scaleX;
    m_pressScaleY = m_scaleY;
    m_pressShear = m_shear;
    m_pressRotation = m_rotation;
    m_pressItemPos = mapFromScene(scenePos);
    m_pressAnchorLocal = scaleAnchorLocal(h);
    m_pressAnchorScene = mapToScene(m_pressAnchorLocal);
    if (h == Handle::OpacitySlider) {
        setOpacityFromSliderPos(scenePos);
        notifyViewStatus();
    }
    return true;
}

void ImageItem::updateHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (m_activeHandle == Handle::None) {
        return;
    }
    if (m_activeHandle == Handle::OpacitySlider) {
        setOpacityFromSliderPos(scenePos);
    } else if (isRotateHandle(m_activeHandle)) {
        const QPointF itemCentre = this->scenePos();
        const QPointF v0 = m_pressScenePos - itemCentre;
        const QPointF v1 = scenePos - itemCentre;
        const qreal a0 = qAtan2(v0.y(), v0.x());
        const qreal a1 = qAtan2(v1.y(), v1.x());
        const qreal deltaDeg = qRadiansToDegrees(a1 - a0);
        qreal angle = m_pressRotation + deltaDeg;
        // Ctrl → 45° (includes 90°); Shift (alone or with Ctrl) → 15°.
        if (mods & Qt::ShiftModifier) {
            angle = qRound(angle / 15.0) * 15.0;
        } else if (mods & Qt::ControlModifier) {
            angle = qRound(angle / 45.0) * 45.0;
        }
        setItemRotation(angle);
    } else if (isScaleHandle(m_activeHandle)) {
        applyScaleHandleDrag(scenePos, mods);
    } else if (isShearHandle(m_activeHandle)) {
        applyShearHandleDrag(scenePos);
    }
    notifyViewStatus();
}

void ImageItem::endHandleInteraction()
{
    // Free-rotate / scale: persist when the gesture finishes.
    if (m_activeHandle != Handle::None && m_activeHandle != Handle::OpacitySlider) {
        if (scene()) {
            for (QGraphicsView *v : scene()->views()) {
                if (auto *iv = qobject_cast<ImageView *>(v)) {
                    iv->commitItemSessionEdit(this);
                    break;
                }
            }
        }
    }
    m_activeHandle = Handle::None;
}

void ImageItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // AUDIT H2: transform chrome is owned exclusively by ImageView (viewport
    // hit-testing + paintEvent). Item only handles body selection / move.
    QGraphicsPixmapItem::mousePressEvent(event);
}

void ImageItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsPixmapItem::mouseMoveEvent(event);
}

void ImageItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsPixmapItem::mouseReleaseEvent(event);
}

void ImageItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    // Gallery / Workspace hover chrome and cursors are driven by ImageView.
    // Keep a neutral item cursor so it does not fight the viewport cursor.
    unsetCursor();
    QGraphicsPixmapItem::hoverMoveEvent(event);
}

void ImageItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    if (m_galleryHovered) {
        m_galleryHovered = false;
    }
    if (m_hoverHandle != Handle::None) {
        m_hoverHandle = Handle::None;
        setToolTip(QString());
    }
    unsetCursor();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}
