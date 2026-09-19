// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cropgeometry.h"

#include <QPainter>
#include <QPainterPath>
#include "contentxform.h"

#include <QLineF>
#include <QTransform>
#include <QtMath>

namespace CropGeometry {

QPolygonF rotatedCorners(const QRectF &rect, qreal degrees)
{
    const QPointF c = rect.center();
    QTransform tr;
    tr.translate(c.x(), c.y());
    tr.rotate(degrees);
    tr.translate(-c.x(), -c.y());
    QPolygonF poly;
    poly << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
    return tr.map(poly);
}

bool pointInsideBounds(const QPointF &p, const QRectF &bounds)
{
    return p.x() >= bounds.left() && p.x() <= bounds.right()
        && p.y() >= bounds.top() && p.y() <= bounds.bottom();
}

bool cornersInside(const QRectF &rect, qreal degrees, const QRectF &bounds)
{
    const QPolygonF poly = rotatedCorners(rect, degrees);
    for (const QPointF &pt : poly) {
        if (!pointInsideBounds(pt, bounds)) {
            return false;
        }
    }
    return true;
}

QRectF translateInside(QRectF rect, qreal degrees, const QRectF &bounds)
{
    for (int pass = 0; pass < 6; ++pass) {
        const QPolygonF poly = rotatedCorners(rect, degrees);
        qreal dx = 0.0;
        qreal dy = 0.0;
        for (const QPointF &pt : poly) {
            if (pt.x() < bounds.left()) {
                dx = qMax(dx, bounds.left() - pt.x());
            } else if (pt.x() > bounds.right()) {
                dx = qMin(dx, bounds.right() - pt.x());
            }
            if (pt.y() < bounds.top()) {
                dy = qMax(dy, bounds.top() - pt.y());
            } else if (pt.y() > bounds.bottom()) {
                dy = qMin(dy, bounds.bottom() - pt.y());
            }
        }
        if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy)) {
            break;
        }
        rect.translate(dx, dy);
    }
    return rect;
}

QRectF shrinkInside(QRectF rect, qreal degrees, const QRectF &bounds, qreal minSide)
{
    if (cornersInside(rect, degrees, bounds)) {
        return rect;
    }
    const QPointF c = rect.center();
    qreal lo = 0.0;
    qreal hi = 1.0;
    QRectF best(c.x() - minSide / 2.0, c.y() - minSide / 2.0, minSide, minSide);
    for (int i = 0; i < 18; ++i) {
        const qreal mid = (lo + hi) * 0.5;
        QRectF r(0, 0, rect.width() * mid, rect.height() * mid);
        r.moveCenter(c);
        if (r.width() < minSide || r.height() < minSide) {
            hi = mid;
            continue;
        }
        if (cornersInside(r, degrees, bounds)) {
            best = r;
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return best;
}

QRectF constrainToContent(QRectF rect, qreal degrees, const QRectF &bounds,
                          qreal minSide)
{
    rect = rect.normalized();
    if (rect.width() < minSide) {
        rect.setWidth(minSide);
    }
    if (rect.height() < minSide) {
        rect.setHeight(minSide);
    }
    if (qAbs(degrees) < kFreeRotationEps) {
        rect = rect.intersected(bounds);
        if (rect.width() < minSide) {
            rect.setWidth(minSide);
        }
        if (rect.height() < minSide) {
            rect.setHeight(minSide);
        }
        if (rect.right() > bounds.right()) {
            rect.moveRight(bounds.right());
        }
        if (rect.bottom() > bounds.bottom()) {
            rect.moveBottom(bounds.bottom());
        }
        if (rect.left() < bounds.left()) {
            rect.moveLeft(bounds.left());
        }
        if (rect.top() < bounds.top()) {
            rect.moveTop(bounds.top());
        }
        return rect;
    }
    rect = translateInside(rect, degrees, bounds);
    if (!cornersInside(rect, degrees, bounds)) {
        rect = shrinkInside(rect, degrees, bounds, minSide);
        rect = translateInside(rect, degrees, bounds);
    }
    return rect;
}

bool axisAlignedOutside(const QRectF &rect, const QRectF &bounds)
{
    return rect.left() < bounds.left() || rect.top() < bounds.top()
        || rect.right() > bounds.right() || rect.bottom() > bounds.bottom();
}

bool priorDraftNeedsExpand(const QRectF &priorInImage, const QRectF &imageBounds,
                           const QRectF &draftLocal, qreal rotationDeg,
                           const QRectF &contentRect)
{
    if (axisAlignedOutside(priorInImage, imageBounds)) {
        return true;
    }
    // Match ensureCropRectValid / initCropRect: ignore near-zero rotation noise.
    if (qAbs(rotationDeg) > kFreeRotationEps
        && !cornersInside(draftLocal, rotationDeg, contentRect)) {
        return true;
    }
    return false;
}

CropButtonLayout cropButtonLayout(const QRectF &cropView, const QRect &viewportRect)
{
    CropButtonLayout L;
    if (!cropView.isValid() || !viewportRect.isValid()) {
        return L;
    }
    constexpr int kW = kChromeBtnW;
    constexpr int kH = kChromeBtnH;
    constexpr int kGap = kChromeBtnGap;
    // Bottom rotate knobs sit ~22px outside the edge; clear them plus air.
    constexpr int kOutsideGap = kChromeOutsideGap;
    constexpr int kInsideInset = kChromeInsideInset;
    constexpr int kMargin = kChromeMargin;
    constexpr int kGroupGapMin = kChromeGroupGapMin;

    const int rightGroupW = kW * 3 + kGap * 2;
    const int leftGroupW = kW * 2 + kGap; // Expand + Auto

    auto pickY = [&](int spanLeft, int spanW) -> int {
        const int yOutside = qRound(cropView.bottom()) + kOutsideGap;
        const int yInside = qRound(cropView.bottom()) - kH - kInsideInset;
        auto fullyVisible = [&](int y) {
            return viewportRect.contains(QRect(spanLeft, y, spanW, kH));
        };
        if (fullyVisible(yOutside)) {
            return yOutside;
        }
        if (fullyVisible(yInside)) {
            return yInside;
        }
        return qBound(viewportRect.top() + kMargin,
                      yOutside,
                      viewportRect.bottom() - kMargin - kH);
    };

    // Right group: align Apply to crop right edge.
    int right = qRound(cropView.right()) - kW;
    right = qBound(viewportRect.left() + kMargin + rightGroupW - kW,
                   right,
                   viewportRect.right() - kMargin - kW);
    const int cancelX = right - kGap - kW;
    const int resetX = cancelX - kGap - kW;
    const int yRight = pickY(resetX, rightGroupW);

    // Left: Expand + Auto, aligned to crop left edge.
    int expandX = qRound(cropView.left());
    expandX = qBound(viewportRect.left() + kMargin,
                     expandX,
                     viewportRect.right() - kMargin - leftGroupW);
    if (expandX + leftGroupW + kGroupGapMin > resetX) {
        expandX = qMax(viewportRect.left() + kMargin,
                       resetX - kGroupGapMin - leftGroupW);
    }
    const int smartX = expandX + kW + kGap;
    const int yLeft = pickY(expandX, leftGroupW);
    // Prefer a shared baseline when both bands land near the same y.
    const int yExpand = (qAbs(yLeft - yRight) <= 2) ? yRight : yLeft;
    const int yGroup = yRight;

    L.expand = QRect(expandX, yExpand, kW, kH);
    L.autoBtn = QRect(smartX, yExpand, kW, kH);
    L.reset = QRect(resetX, yGroup, kW, kH);
    L.cancel = QRect(cancelX, yGroup, kW, kH);
    L.apply = QRect(right, yGroup, kW, kH);
    L.valid = true;
    return L;
}

static QPointF rotateKnobOutward(QPointF mid, QPointF edgeAlong, const QPointF &centre,
                                 qreal outset)
{
    qreal alen = qHypot(edgeAlong.x(), edgeAlong.y());
    if (alen > 1e-6) {
        edgeAlong /= alen;
    }
    QPointF outward(-edgeAlong.y(), edgeAlong.x());
    if (QPointF::dotProduct(outward, mid - centre) < 0) {
        outward = -outward;
    }
    return mid + outward * outset;
}

CropFrameViewAnchors frameViewAnchors(const QPolygonF &viewPoly, qreal rotateOutset)
{
    CropFrameViewAnchors a;
    if (viewPoly.size() < 4) {
        return a;
    }
    a.tl = viewPoly.at(0);
    a.tr = viewPoly.at(1);
    a.br = viewPoly.at(2);
    a.bl = viewPoly.at(3);
    a.tm = (a.tl + a.tr) * 0.5;
    a.bm = (a.bl + a.br) * 0.5;
    a.lm = (a.tl + a.bl) * 0.5;
    a.rm = (a.tr + a.br) * 0.5;
    a.centre = (a.tl + a.tr + a.br + a.bl) * 0.25;
    a.rotTop = rotateKnobOutward(a.tm, a.tr - a.tl, a.centre, rotateOutset);
    a.rotRight = rotateKnobOutward(a.rm, a.br - a.tr, a.centre, rotateOutset);
    a.rotBottom = rotateKnobOutward(a.bm, a.bl - a.br, a.centre, rotateOutset);
    a.rotLeft = rotateKnobOutward(a.lm, a.tl - a.bl, a.centre, rotateOutset);
    a.valid = true;
    return a;
}

CropHandle hitTestCropChrome(const QPoint &viewPos, const CropButtonLayout &buttons,
                             const CropFrameViewAnchors &anchors,
                             qreal handleHitPx, qreal moveHitPx)
{
    if (buttons.valid) {
        if (buttons.expand.contains(viewPos)) {
            return CropHandle::ExpandToggle;
        }
        if (buttons.autoBtn.contains(viewPos)) {
            return CropHandle::Auto;
        }
        if (buttons.reset.contains(viewPos)) {
            return CropHandle::Reset;
        }
        if (buttons.cancel.contains(viewPos)) {
            return CropHandle::Cancel;
        }
        if (buttons.apply.contains(viewPos)) {
            return CropHandle::Close;
        }
    }
    if (!anchors.valid) {
        return CropHandle::None;
    }
    auto nearPt = [&](const QPointF &p, qreal r) {
        return QLineF(QPointF(viewPos), p).length() <= r;
    };
    if (nearPt(anchors.rotTop, handleHitPx) || nearPt(anchors.rotRight, handleHitPx)
        || nearPt(anchors.rotBottom, handleHitPx) || nearPt(anchors.rotLeft, handleHitPx)) {
        return CropHandle::Rotate;
    }
    if (nearPt(anchors.tl, handleHitPx)) {
        return CropHandle::TopLeft;
    }
    if (nearPt(anchors.tr, handleHitPx)) {
        return CropHandle::TopRight;
    }
    if (nearPt(anchors.bl, handleHitPx)) {
        return CropHandle::BottomLeft;
    }
    if (nearPt(anchors.br, handleHitPx)) {
        return CropHandle::BottomRight;
    }
    if (nearPt(anchors.tm, handleHitPx)) {
        return CropHandle::Top;
    }
    if (nearPt(anchors.bm, handleHitPx)) {
        return CropHandle::Bottom;
    }
    if (nearPt(anchors.lm, handleHitPx)) {
        return CropHandle::Left;
    }
    if (nearPt(anchors.rm, handleHitPx)) {
        return CropHandle::Right;
    }
    if (nearPt(anchors.centre, moveHitPx)) {
        return CropHandle::Move;
    }
    return CropHandle::None;
}

static QPointF rotateVec(QPointF v, qreal degrees)
{
    QTransform tr;
    tr.rotate(degrees);
    return tr.map(v);
}

QRectF resizeDraftRect(CropHandle handle, const QPointF &local,
                       const QRectF &dragStartRect, qreal rotationDeg,
                       qreal minSide, bool fromCenter, bool forceSquare)
{
    const QPointF c0 = dragStartRect.center();
    const qreal w0 = dragStartRect.width();
    const qreal h0 = dragStartRect.height();
    const QPointF pLocal = rotateVec(local - c0, -rotationDeg);
    qreal L = -w0 / 2.0;
    qreal R = w0 / 2.0;
    qreal T = -h0 / 2.0;
    qreal B = h0 / 2.0;

    const bool left = (handle == CropHandle::Left || handle == CropHandle::TopLeft
                       || handle == CropHandle::BottomLeft);
    const bool right = (handle == CropHandle::Right || handle == CropHandle::TopRight
                        || handle == CropHandle::BottomRight);
    const bool top = (handle == CropHandle::Top || handle == CropHandle::TopLeft
                      || handle == CropHandle::TopRight);
    const bool bottom = (handle == CropHandle::Bottom || handle == CropHandle::BottomLeft
                         || handle == CropHandle::BottomRight);

    if (fromCenter) {
        if (left || right) {
            const qreal half = qMax(minSide / 2.0, qAbs(pLocal.x()));
            L = -half;
            R = half;
        }
        if (top || bottom) {
            const qreal half = qMax(minSide / 2.0, qAbs(pLocal.y()));
            T = -half;
            B = half;
        }
    } else {
        if (left) {
            L = qMin(pLocal.x(), R - minSide);
        }
        if (right) {
            R = qMax(pLocal.x(), L + minSide);
        }
        if (top) {
            T = qMin(pLocal.y(), B - minSide);
        }
        if (bottom) {
            B = qMax(pLocal.y(), T + minSide);
        }
    }

    if (forceSquare) {
        qreal side = qMax(R - L, B - T);
        if (fromCenter) {
            const qreal half = side / 2.0;
            L = -half;
            R = half;
            T = -half;
            B = half;
        } else {
            if (right && !left) {
                R = L + side;
            } else if (left && !right) {
                L = R - side;
            } else {
                const qreal cx = (L + R) / 2.0;
                L = cx - side / 2.0;
                R = cx + side / 2.0;
            }
            if (bottom && !top) {
                B = T + side;
            } else if (top && !bottom) {
                T = B - side;
            } else {
                const qreal cy = (T + B) / 2.0;
                T = cy - side / 2.0;
                B = cy + side / 2.0;
            }
        }
    }

    const QPointF cLocal((L + R) / 2.0, (T + B) / 2.0);
    const qreal newW = R - L;
    const qreal newH = B - T;
    const QPointF c1 = c0 + rotateVec(cLocal, rotationDeg);
    return QRectF(c1.x() - newW / 2.0, c1.y() - newH / 2.0, newW, newH);
}

qreal normalizeRotationDeg(qreal degrees)
{
    while (degrees > 180.0) {
        degrees -= 360.0;
    }
    while (degrees <= -180.0) {
        degrees += 360.0;
    }
    return degrees;
}

qreal snapRotationDeg(qreal degrees, bool snap15, bool snap45)
{
    if (snap15) {
        return qRound(degrees / 15.0) * 15.0;
    }
    if (snap45) {
        return qRound(degrees / 45.0) * 45.0;
    }
    return degrees;
}

qreal rotationFromDrag(const QPointF &local, const QPointF &centre,
                       qreal rotateStartRotation, qreal rotateStartAngle,
                       bool snap15, bool snap45)
{
    const QPointF v = local - centre;
    const qreal angle = qRadiansToDegrees(qAtan2(v.y(), v.x()));
    const qreal raw = normalizeRotationDeg(
        rotateStartRotation + (angle - rotateStartAngle));
    return snapRotationDeg(raw, snap15, snap45);
}

QRectF rubberBandRect(const QPointF &origin, const QPointF &local,
                      bool forceSquare, bool fromCenter)
{
    if (fromCenter) {
        const QPointF c = origin;
        const qreal halfW = qMax(2.0, qAbs(local.x() - c.x()));
        const qreal halfH = qMax(2.0, qAbs(local.y() - c.y()));
        if (forceSquare) {
            const qreal half = qMax(halfW, halfH);
            return QRectF(c - QPointF(half, half), QSizeF(2 * half, 2 * half));
        }
        return QRectF(c - QPointF(halfW, halfH), QSizeF(2 * halfW, 2 * halfH));
    }
    if (forceSquare) {
        const qreal side = qMax(qAbs(local.x() - origin.x()), qAbs(local.y() - origin.y()));
        const qreal dx = (local.x() >= origin.x()) ? side : -side;
        const qreal dy = (local.y() >= origin.y()) ? side : -side;
        return QRectF(origin, origin + QPointF(dx, dy)).normalized();
    }
    return QRectF(origin, local).normalized();
}

QRect integerCropFromLocal(const QRectF &local, const QPointF &offset)
{
    const int dx = qRound(local.left() - offset.x());
    const int dy = qRound(local.top() - offset.y());
    const QSize sz = ContentXform::roundedSizeAtLeast1(local.width(), local.height());
    return QRect(dx, dy, sz.width(), sz.height());
}

QRect flipAwareSourceCrop(const QRect &disp, int imageW, int imageH,
                          bool hFlip, bool vFlip)
{
    int dx = disp.x();
    int dy = disp.y();
    const int dw = disp.width();
    const int dh = disp.height();
    if (hFlip) {
        dx = imageW - dx - dw;
    }
    if (vFlip) {
        dy = imageH - dy - dh;
    }
    return QRect(dx, dy, dw, dh);
}

void paintDimOutside(QPainter &painter, const QRect &viewportRect,
                     const QPolygonF &cropViewPoly)
{
    QPainterPath outer;
    outer.addRect(QRectF(viewportRect));
    QPainterPath hole;
    hole.addPolygon(cropViewPoly);
    hole.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawPath(outer.subtracted(hole));
}

void paintFrame(QPainter &painter, const QPolygonF &cropViewPoly)
{
    painter.setBrush(Qt::NoBrush);
    QPen frame(QColor(255, 190, 40, 240), 0);
    frame.setCosmetic(true);
    frame.setWidthF(1.75);
    painter.setPen(frame);
    painter.drawPolygon(cropViewPoly);
    QPen dash(QColor(40, 30, 10, 180), 0, Qt::DashLine);
    dash.setCosmetic(true);
    dash.setWidthF(1.0);
    painter.setPen(dash);
    painter.drawPolygon(cropViewPoly);
}

void paintTextButton(QPainter &painter, const QRect &btn, bool hover,
                     const QString &label, CropBtnRole role, bool toggled)
{
    if (!btn.isValid()) {
        return;
    }
    const qreal radius = (role == CropBtnRole::Toggle) ? 6.0 : 11.0;
    QColor fill(50, 50, 50, 230);
    QColor border(255, 190, 40);
    QColor text(240, 240, 240);
    qreal borderW = 1.15;
    switch (role) {
    case CropBtnRole::Toggle:
        if (toggled) {
            fill = hover ? QColor(100, 210, 230, 255) : QColor(60, 175, 200, 245);
            border = QColor(255, 255, 255);
            text = QColor(10, 35, 45);
            borderW = 2.0;
        } else {
            fill = hover ? QColor(30, 70, 85, 230) : QColor(40, 40, 40, 220);
            border = hover ? QColor(255, 255, 255) : QColor(70, 170, 195);
            borderW = hover ? 1.75 : 1.25;
        }
        break;
    case CropBtnRole::Action:
        fill = hover ? QColor(80, 60, 20, 240) : QColor(50, 50, 50, 230);
        border = hover ? QColor(255, 255, 255) : QColor(255, 190, 40);
        borderW = hover ? 1.75 : 1.15;
        break;
    case CropBtnRole::Neutral:
        fill = hover ? QColor(70, 70, 70, 240) : QColor(45, 45, 45, 220);
        border = hover ? QColor(200, 200, 200) : QColor(120, 120, 120);
        text = QColor(220, 220, 220);
        borderW = hover ? 1.5 : 1.0;
        break;
    case CropBtnRole::Commit:
        fill = hover ? QColor(255, 210, 70, 255) : QColor(240, 175, 40, 245);
        border = hover ? QColor(255, 255, 255) : QColor(120, 80, 10);
        text = QColor(40, 25, 5);
        borderW = hover ? 1.75 : 1.25;
        break;
    }
    QPen pen(border);
    pen.setWidthF(borderW);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRoundedRect(btn, radius, radius);
    if (role == CropBtnRole::Toggle && toggled) {
        QPen ring(QColor(255, 255, 255, 200));
        ring.setWidthF(1.1);
        ring.setCosmetic(true);
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(btn.adjusted(3, 3, -3, -3), radius * 0.7, radius * 0.7);
    }
    painter.setPen(text);
    QFont f = painter.font();
    f.setPointSize(clampButtonPointSize(f.pointSize()));
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(btn, Qt::AlignCenter, label);
}


void paintRotateKnobs(QPainter &painter, const QPolygonF &cropViewPoly, bool hot)
{
    const CropFrameViewAnchors a = frameViewAnchors(cropViewPoly);
    if (!a.valid) {
        return;
    }
    auto drawRotateKnob = [&](const QPointF &mid, const QPointF &knob) {
        QPen stem(hot ? QColor(255, 255, 255) : QColor(255, 190, 40), 0);
        stem.setCosmetic(true);
        stem.setWidthF(hot ? 1.8 : 1.3);
        painter.setPen(stem);
        painter.drawLine(mid, knob);
        painter.setBrush(hot ? QColor(255, 220, 80) : QColor(255, 190, 40));
        painter.drawEllipse(knob, hot ? 6.0 : 5.0, hot ? 6.0 : 5.0);
        painter.setBrush(Qt::NoBrush);
    };
    drawRotateKnob(a.tm, a.rotTop);
    drawRotateKnob(a.rm, a.rotRight);
    drawRotateKnob(a.bm, a.rotBottom);
    drawRotateKnob(a.lm, a.rotLeft);
}

void paintMoveGrip(QPainter &painter, const QPolygonF &cropViewPoly, bool hot)
{
    const CropFrameViewAnchors a = frameViewAnchors(cropViewPoly);
    if (!a.valid) {
        return;
    }
    const QPointF centre = a.centre;
    const qreal s = hot ? 10.0 : 9.0;
    painter.setPen(QPen(hot ? QColor(255, 255, 255) : QColor(40, 30, 10), hot ? 1.8 : 1.35));
    painter.setBrush(hot ? QColor(255, 220, 80, 255) : QColor(255, 190, 40, 240));
    painter.drawRoundedRect(QRectF(centre.x() - s, centre.y() - s, 2 * s, 2 * s), 3.0, 3.0);
    painter.setPen(QPen(QColor(40, 30, 10), 1.35));
    painter.drawLine(QPointF(centre.x() - s + 3, centre.y()),
                     QPointF(centre.x() + s - 3, centre.y()));
    painter.drawLine(QPointF(centre.x(), centre.y() - s + 3),
                     QPointF(centre.x(), centre.y() + s - 3));
    painter.setBrush(Qt::NoBrush);
}

} // namespace CropGeometry
