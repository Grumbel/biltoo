// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"
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
QString tooltipForHandle(ImageItem::Handle h)
{
    switch (h) {
    case ImageItem::Handle::FlipH:
        return QObject::tr("Toggle horizontal flip");
    case ImageItem::Handle::FlipV:
        return QObject::tr("Toggle vertical flip");
    case ImageItem::Handle::Raise:
        return QObject::tr("Raise (bring forward)");
    case ImageItem::Handle::Lower:
        return QObject::tr("Lower (send backward)");
    case ImageItem::Handle::ResetScale:
        return QObject::tr("Reset scale to 100%");
    case ImageItem::Handle::ResetRotation:
        return QObject::tr("Reset rotation to 0°");
    case ImageItem::Handle::OpacitySlider:
        return QObject::tr("Opacity");
    default:
        return QString();
    }
}
} // namespace

namespace {
constexpr qreal kHandleScreenPx = 21.0;      // scale/rotate markers in *viewport* px
constexpr qreal kRotateOffsetPx = 36.0;      // rotate handle distance from edge (viewport px)
constexpr qreal kChromeBtnScreenPx = 22.0;   // flip / raise / lower diameter (viewport px)
constexpr qreal kChromeHitScreenPx = 21.0;   // hit radius in viewport px
constexpr qreal kChromeInsetPx = 10.0;       // inset from pixmap edge into the image
constexpr qreal kChromeBtnGapPx = 6.0;       // gap between stacked chrome buttons
constexpr qreal kSliderWidthPx = 90.0;
constexpr qreal kSliderHeightPx = 9.0;
constexpr qreal kSliderBottomInsetPx = 10.0; // opacity track above bottom edge
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
    QRectF r = QGraphicsPixmapItem::boundingRect();
    if (isSelected() && m_interactive) {
        const qreal content = qMax(r.width(), r.height());
        const qreal sMin = qMax(deviceScaleMin(), 1e-4);
        const qreal ideal = (kHandleScreenPx * 1.75 + kRotateOffsetPx + 8.0) / sMin;
        // Never pad more than ~half the content diagonal in local units; at very
        // small scales the view still finds handles via selected-item queries.
        const qreal pad = qMin(ideal, qMax(40.0, content * 0.75));
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
    path.addRect(QGraphicsPixmapItem::boundingRect());
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
        || h == Handle::Raise || h == Handle::Lower
        || h == Handle::ResetScale || h == Handle::ResetRotation
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
    // Ctrl or Shift → scale from the opposite corner/edge (drawing-program style).
    // Default → uniform/anisotropic scale about the item centre.
    //
    // Edge stretch uses scene-space projections onto the *press-time* image axes
    // so the ratio stays stable when scale approaches the clamp (HANDLES.md).
    // Local ratios after each setItemScale amplify noise near zero.
    const bool fromCenter = !(mods & (Qt::ControlModifier | Qt::ShiftModifier));
    const QPointF itemCentre = this->scenePos();
    const QRectF localR = QGraphicsPixmapItem::boundingRect();
    const qreal halfW = localR.width() * 0.5;
    const qreal halfH = localR.height() * 0.5;
    constexpr qreal kMinDist = 1.0; // scene px; below this ignore the sample

    if (isCornerScaleHandle(m_activeHandle)) {
        if (fromCenter) {
            const qreal d0 = QLineF(itemCentre, m_pressScenePos).length();
            const qreal d1 = QLineF(itemCentre, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
            }
        } else {
            const QPointF anchor = m_pressAnchorScene;
            const qreal d0 = QLineF(anchor, m_pressScenePos).length();
            const qreal d1 = QLineF(anchor, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
                const QPointF now = mapToScene(m_pressAnchorLocal);
                setPos(pos() + (anchor - now));
            }
        }
        return;
    }

    if (isEdgeScaleHandle(m_activeHandle)) {
        // Unit image axes in *scene* at press (rotation fixed during this drag).
        // Built from press scale + rotation so they do not change as we update scale.
        const qreal rot = qDegreesToRadians(m_pressRotation);
        const qreal c = qCos(rot);
        const qreal s = qSin(rot);
        // Local +X maps to scene direction (c, s) * scaleX; +Y to (-s, c) * scaleY
        // after the usual Qt transform order (scale then rotate around centre).
        // Transform is rotate then scale only; flips are baked into the pixmap
        // so the geometric frame axes are unaffected.
        QPointF axisX(c * m_pressScaleX, s * m_pressScaleX);
        QPointF axisY(-s * m_pressScaleY, c * m_pressScaleY);

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
        if (!fromCenter) {
            const QPointF now = mapToScene(m_pressAnchorLocal);
            setPos(pos() + (m_pressAnchorScene - now));
        }
    }
}

qreal ImageItem::chromeButtonSize() const
{
    return kChromeBtnScreenPx / screenScale();
}

QRectF ImageItem::opacitySliderRect() const
{
    // Horizontal track along the bottom interior of the pixmap (rotates with image).
    // Clamp local sizes so near-zero item scale does not produce a track larger
    // than the pixmap itself (HANDLES.md).
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    auto axis = [this](const QPointF &localAxis) -> qreal {
        return qMax(1e-6, QLineF(localToViewPx(QPointF(0, 0)), localToViewPx(localAxis)).length());
    };
    const qreal sx = axis(QPointF(1, 0));
    const qreal sy = axis(QPointF(0, 1));
    const qreal content = qMax(r.width(), r.height());
    const qreal maxOff = qMax(20.0, content * 0.4);
    const qreal insetX = qMin(kChromeInsetPx / sx, maxOff);
    const qreal insetY = qMin(kChromeInsetPx / sy, maxOff);
    const qreal h = qMin(kSliderHeightPx / sy, qMax(1.0, r.height() * 0.2));
    const qreal maxW = qMin(kSliderWidthPx / sx, r.width());
    const qreal avail = qMax(0.0, r.width() - 2.0 * insetX);
    const qreal w = qMin(maxW, avail);
    const qreal x = r.left() + insetX + (avail - w) / 2.0;
    const qreal y = r.bottom() - insetY - h;
    return QRectF(x, y, w, h);
}

void ImageItem::setOpacityFromSliderPos(const QPointF &scenePos)
{
    const QRectF track = opacitySliderRect();
    if (track.width() <= 0) {
        return;
    }
    // Project the pointer onto the track in view space so a rotated slider still
    // responds along its visible axis (not raw local X).
    const QPointF a = localToViewPx(QPointF(track.left(), track.center().y()));
    const QPointF b = localToViewPx(QPointF(track.right(), track.center().y()));
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
                << Handle::ScaleBottom << Handle::ScaleLeft;
    }
    handles << Handle::RotateTop << Handle::RotateRight
            << Handle::RotateBottom << Handle::RotateLeft
            << Handle::FlipH << Handle::FlipV
            << Handle::Raise << Handle::Lower
            << Handle::ResetScale << Handle::ResetRotation
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
    const qreal insetX = qMin(kChromeInsetPx / sx, maxOff / 4.0);

    const qreal stackX = r.right() + insetX + qMin((kChromeBtnScreenPx / sx) / 2.0, maxOff / 4.0);
    const int nChrome = 6;
    const qreal stackH = nChrome * btn + (nChrome - 1) * gap;
    const qreal stackTop = cy - stackH / 2.0;
    auto chromeBtnCenter = [&](int index) {
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
    case Handle::Raise:
        return chromeBtnCenter(2);
    case Handle::Lower:
        return chromeBtnCenter(3);
    case Handle::ResetScale:
        return chromeBtnCenter(4);
    case Handle::ResetRotation:
        return chromeBtnCenter(5);
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

    // Degenerate frame: only opacity / chrome near centre are reachable.
    if (QLineF(tl, br).length() < 4.0) {
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

    // Opacity track (mapped ends)
    {
        const QRectF slider = opacitySliderRect();
        const QPointF a = toView(QPointF(slider.left(), slider.center().y()));
        const QPointF b = toView(QPointF(slider.right(), slider.center().y()));
        const QPointF ab = b - a;
        const qreal ab2 = QPointF::dotProduct(ab, ab);
        qreal tt = 0.0;
        if (ab2 > 1e-6) {
            tt = qBound(0.0, QPointF::dotProduct(p - a, ab) / ab2, 1.0);
        }
        if (QLineF(p, a + ab * tt).length() <= kChromeHitScreenPx) {
            return Handle::OpacitySlider;
        }
    }

    // Chrome buttons — same view-space stack as paint
    {
        const qreal btnR = kChromeBtnScreenPx / 2.0;
        const qreal inset = kChromeInsetPx + btnR;
        const QPointF stackOrigin = midRight + outRight * inset;
        const QPointF along = dirRight;
        const int nChrome = 6;
        const qreal step = kChromeBtnScreenPx + kChromeBtnGapPx;
        const qreal stackH = nChrome * kChromeBtnScreenPx + (nChrome - 1) * kChromeBtnGapPx;
        const QPointF stackStart = stackOrigin - along * (stackH / 2.0)
                                   + along * (kChromeBtnScreenPx / 2.0);
        const Handle chromeHandles[] = {
            Handle::FlipH, Handle::FlipV, Handle::Raise, Handle::Lower,
            Handle::ResetScale, Handle::ResetRotation
        };
        Handle best = Handle::None;
        qreal bestDist = 1e300;
        for (int i = 0; i < nChrome; ++i) {
            const QPointF c = stackStart + along * (i * step);
            const qreal d = QLineF(p, c).length();
            const qreal limit = (chromeHandles[i] == Handle::FlipH
                                 || chromeHandles[i] == Handle::FlipV)
                                    ? (kChromeBtnScreenPx * 0.62 + 4.0)
                                    : kChromeHitScreenPx;
            if (d <= limit && d <= bestDist) {
                bestDist = d;
                best = chromeHandles[i];
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
        toggleHFlip();
        break;
    case Handle::FlipV:
        toggleVFlip();
        break;
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
        break;
    case Handle::ResetRotation:
        setItemRotation(0.0);
        break;
    default:
        break;
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
        QGraphicsPixmapItem::paint(painter, &opt, widget);
        painter->restore();
    }

    const QRectF r = cropped ? crop : QGraphicsPixmapItem::boundingRect();

    // Gallery: selection frame only (classic multi-select). Hover is for HUD
    // filename, not a full-tile wash — near-fullscreen packs stay usable.
    if (!m_interactive) {
        const bool selected = option->state & QStyle::State_Selected;
        if (selected) {
            painter->save();
            painter->setOpacity(1.0);
            QPen pen(QColor(0, 180, 255), 0);
            pen.setCosmetic(true);
            pen.setWidthF(2.5);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
            painter->restore();
        }
        return;
    }

    // Selection transform chrome is painted by ImageView::paintEvent (viewport
    // device space) so handles sit above every image and stay scale-invariant.
    Q_UNUSED(r);
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
    const qreal frameDiag = QLineF(tl, br).length();
    const bool frameOk = frameDiag >= 4.0;

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
        const qreal rad = hs * (hot ? 0.42 : 0.36);
        painter->setBrush(hot ? QColor(255, 230, 80) : QColor(255, 200, 40));
        QPen hp(hot ? QColor(255, 255, 255) : QColor(40, 40, 40), 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.75 : 1.25);
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
            const bool hot = (m_hoverHandle == co.h || m_activeHandle == co.h);
            const QPointF c = co.corner;
            const QPointF d1 = norm(co.alongA);
            const QPointF d2 = norm(co.alongB);
            const qreal arm = hs * (hot ? 1.25 : 1.05);
            QPen hp(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 2.25 : 1.75);
            hp.setCapStyle(Qt::SquareCap);
            painter->setPen(hp);
            painter->drawLine(c, c + d1 * arm);
            painter->drawLine(c, c + d2 * arm);
            const qreal half = hs * (hot ? 0.28 : 0.24);
            // Grip square aligned to local axes mapped into view.
            const QPointF lx = norm(toView(QPointF(1, 0)) - toView(QPointF(0, 0)));
            const QPointF ly = norm(toView(QPointF(0, 1)) - toView(QPointF(0, 0)));
            QPolygonF sq;
            sq << c + (-lx - ly) * half << c + (lx - ly) * half
               << c + (lx + ly) * half << c + (-lx + ly) * half;
            painter->setBrush(hot ? QColor(255, 255, 255) : QColor(0, 160, 255, 230));
            painter->drawPolygon(sq);
            painter->setBrush(Qt::NoBrush);
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
            const qreal len = hs * (hot ? 2.2 : 1.9);
            const qreal thick = hs * (hot ? 0.42 : 0.34);
            QPen hp(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 1.5 : 1.15);
            painter->setPen(hp);
            painter->setBrush(hot ? QColor(80, 200, 255) : QColor(0, 160, 255, 230));
            QPolygonF bar;
            bar << ed.mid + along * (len / 2) + perp * (thick / 2)
                << ed.mid - along * (len / 2) + perp * (thick / 2)
                << ed.mid - along * (len / 2) - perp * (thick / 2)
                << ed.mid + along * (len / 2) - perp * (thick / 2);
            painter->drawPolygon(bar);
            painter->setBrush(Qt::NoBrush);
        }
    }

    // Chrome buttons: constant viewport-pixel size and distance outside the
    // visual right edge (HANDLES.md). Stack runs along the edge direction in
    // view space so rotation does not change spacing or push buttons to infinity.
    {
        const qreal btnR = kChromeBtnScreenPx / 2.0;
        const qreal flipR = kChromeBtnScreenPx * 0.62;
        const qreal inset = kChromeInsetPx + btnR;
        // Stack along the local "right" edge mapped to view (dirRight / outRight).
        const QPointF stackOrigin = midRight + outRight * inset;
        const QPointF along = dirRight; // unit, parallel to the edge
        const int nChrome = 6;
        const qreal step = kChromeBtnScreenPx + kChromeBtnGapPx;
        const qreal stackH = nChrome * kChromeBtnScreenPx + (nChrome - 1) * kChromeBtnGapPx;
        // Centre the stack on the edge mid-point.
        const QPointF stackStart = stackOrigin - along * (stackH / 2.0) + along * (kChromeBtnScreenPx / 2.0);

        auto chromeCenterView = [&](int index) -> QPointF {
            return stackStart + along * (index * step);
        };

        auto drawFlipToggle = [&](Handle h, int index, bool on, const QString &glyph) {
            const QPointF c = chromeCenterView(index);
            const bool hovered = (m_hoverHandle == h);
            const qreal rad = flipR * (hovered ? 1.08 : 1.0);
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
            painter->drawEllipse(c, rad, rad);
            if (on) {
                QPen ring(QColor(255, 255, 255, 200));
                ring.setWidthF(1.25);
                ring.setCosmetic(true);
                painter->setPen(ring);
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(c, rad * 0.72, rad * 0.72);
            }
            painter->setPen(on ? QColor(255, 255, 255) : QColor(230, 230, 230));
            QFont f = painter->font();
            f.setPointSizeF(qMax(8.0, rad * 0.58));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(QRectF(c.x() - rad, c.y() - rad, rad * 2, rad * 2),
                              Qt::AlignCenter, glyph);
        };

        auto drawBtn = [&](Handle h, int index, const QString &glyph) {
            const QPointF c = chromeCenterView(index);
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

        drawFlipToggle(Handle::FlipH, 0, m_hFlip, QStringLiteral("↔"));
        drawFlipToggle(Handle::FlipV, 1, m_vFlip, QStringLiteral("↕"));
        drawBtn(Handle::Raise, 2, QStringLiteral("↑"));
        drawBtn(Handle::Lower, 3, QStringLiteral("↓"));
        drawBtn(Handle::ResetScale, 4, QStringLiteral("1:1"));
        drawBtn(Handle::ResetRotation, 5, QStringLiteral("0°"));
    }

    // Opacity slider along the bottom edge of the frame (view-space).
    {
        const QRectF track = opacitySliderRect();
        const QPointF a = toView(QPointF(track.left(), track.center().y()));
        const QPointF b = toView(QPointF(track.right(), track.center().y()));
        const QPointF ab = b - a;
        const qreal abLen = qHypot(ab.x(), ab.y());
        const QPointF along = abLen > 1e-6 ? ab / abLen : QPointF(1, 0);
        const QPointF perp(-along.y(), along.x());
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
        || h == Handle::Raise || h == Handle::Lower
        || h == Handle::ResetScale || h == Handle::ResetRotation) {
        activateChromeHandle(h);
        return true;
    }
    m_activeHandle = h;
    m_pressScenePos = scenePos;
    m_pressScaleX = m_scaleX;
    m_pressScaleY = m_scaleY;
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
        if (mods & Qt::ControlModifier) {
            angle = qRound(angle / 90.0) * 90.0;
        } else if (mods & Qt::ShiftModifier) {
            angle = qRound(angle / 45.0) * 45.0;
        }
        setItemRotation(angle);
    } else if (isScaleHandle(m_activeHandle)) {
        applyScaleHandleDrag(scenePos, mods);
    }
    notifyViewStatus();
}

void ImageItem::endHandleInteraction()
{
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
