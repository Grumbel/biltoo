// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"

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
        return QObject::tr("Flip horizontal");
    case ImageItem::Handle::FlipV:
        return QObject::tr("Flip vertical");
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
constexpr qreal kHandleScreenPx = 13.0;
constexpr qreal kRotateOffsetPx = 32.0;
constexpr qreal kChromeBtnScreenPx = 28.0;   // flip / raise / lower diameter (screen px)
constexpr qreal kChromeHitScreenPx = 18.0;   // hit radius in screen px (generous)
constexpr qreal kChromeInsetPx = 12.0;       // inset from pixmap edge into the image
constexpr qreal kChromeBtnGapPx = 8.0;       // gap between stacked chrome buttons
constexpr qreal kSliderWidthPx = 100.0;
constexpr qreal kSliderHeightPx = 12.0;
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
    // Expand for external handles (scale/rotate) when selected so they are not clipped.
    // Flip / raise / lower / opacity sit inside the pixmap and need no extra pad.
    QRectF r = QGraphicsPixmapItem::boundingRect();
    if (isSelected() && m_interactive) {
        // Pads must cover screen-pixel handle targets after rotation; use min
        // device scale so the local AABB still contains the hit discs.
        const qreal sMin = deviceScaleMin();
        const qreal hitLocal = (kHandleScreenPx * 1.35) / sMin;
        const qreal rotLocal = kRotateOffsetPx / sMin;
        const qreal pad = hitLocal + rotLocal + 4.0 / sMin;
        r.adjust(-pad, -pad, pad, pad);
    }
    return r;
}

QPainterPath ImageItem::shape() const
{
    // Hit-test the content plus handles when selected. Radii are in item-local
    // units; use generous chrome ellipses so rotated items still receive events.
    QPainterPath path;
    if (!m_galleryCellSize.isEmpty()) {
        const QRectF clip = galleryClipLocal();
        if (!clip.isEmpty()) {
            path.addRect(clip);
            return path;
        }
    }
    path.addRect(QGraphicsPixmapItem::boundingRect());
    if (isSelected() && m_interactive) {
        // Local radii from min device scale so a screen-pixel disc around each
        // handle stays inside shape() after rotation / anisotropic scale.
        const qreal sMin = deviceScaleMin();
        const qreal r = (kHandleScreenPx * 1.5) / sMin;
        QList<Handle> handles = {
            Handle::RotateTop, Handle::RotateRight, Handle::RotateBottom, Handle::RotateLeft
        };
        if (m_scaleHandlesEnabled) {
            handles = QList<Handle>{Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                    Handle::ScaleBottomLeft, Handle::ScaleBottomRight,
                                    Handle::ScaleTop, Handle::ScaleRight,
                                    Handle::ScaleBottom, Handle::ScaleLeft}
                      + handles;
        }
        for (Handle h : handles) {
            const QPointF c = handleCenter(h);
            path.addEllipse(c, r, r);
        }
        const qreal cr = (kChromeHitScreenPx * 1.25) / sMin;
        for (Handle h : {Handle::FlipH, Handle::FlipV, Handle::Raise, Handle::Lower,
                          Handle::ResetScale, Handle::ResetRotation}) {
            const QPointF c = handleCenter(h);
            path.addEllipse(c, cr, cr);
        }
        const qreal pad = 10.0 / sMin;
        path.addRect(opacitySliderRect().adjusted(-pad, -pad, pad, pad));
        const QRectF content = QGraphicsPixmapItem::boundingRect();
        path.moveTo(content.center().x(), content.top());
        path.lineTo(handleCenter(Handle::RotateTop));
        path.moveTo(content.right(), content.center().y());
        path.lineTo(handleCenter(Handle::RotateRight));
        path.moveTo(content.center().x(), content.bottom());
        path.lineTo(handleCenter(Handle::RotateBottom));
        path.moveTo(content.left(), content.center().y());
        path.lineTo(handleCenter(Handle::RotateLeft));
    }
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
    const bool fromCenter = !(mods & (Qt::ControlModifier | Qt::ShiftModifier));
    const QPointF centre = scenePos; // unused name clash — use item centre
    Q_UNUSED(centre);
    const QPointF itemCentre = this->scenePos();

    if (isCornerScaleHandle(m_activeHandle)) {
        if (fromCenter) {
            const qreal d0 = QLineF(itemCentre, m_pressScenePos).length();
            const qreal d1 = QLineF(itemCentre, scenePos).length();
            if (d0 > 1.0) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
            }
        } else {
            // Keep opposite corner fixed in scene; scale so the dragged corner
            // tracks the pointer along the ray from the anchor.
            const QPointF anchor = m_pressAnchorScene;
            const qreal d0 = QLineF(anchor, m_pressScenePos).length();
            const qreal d1 = QLineF(anchor, scenePos).length();
            if (d0 > 1.0) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
                // Reposition so the anchor local point stays at anchor scene.
                const QPointF now = mapToScene(m_pressAnchorLocal);
                setPos(pos() + (anchor - now));
            }
        }
        return;
    }

    if (isEdgeScaleHandle(m_activeHandle)) {
        // mapFromScene undoes rotation+scale, so local X/Y are the image axes
        // even when the frame is rotated on screen — stretch those axes only.
        const QPointF local = mapFromScene(scenePos);
        const QPointF pressLocal = m_pressItemPos;

        qreal sx = m_pressScaleX;
        qreal sy = m_pressScaleY;
        if (m_activeHandle == Handle::ScaleLeft || m_activeHandle == Handle::ScaleRight) {
            if (fromCenter) {
                const qreal x0 = qAbs(pressLocal.x());
                const qreal x1 = qAbs(local.x());
                if (x0 > 1.0) {
                    sx = m_pressScaleX * (x1 / x0);
                }
            } else {
                const qreal ax = m_pressAnchorLocal.x();
                const qreal x0 = qAbs(pressLocal.x() - ax);
                const qreal x1 = qAbs(local.x() - ax);
                if (x0 > 1.0) {
                    sx = m_pressScaleX * (x1 / x0);
                }
            }
        } else {
            if (fromCenter) {
                const qreal y0 = qAbs(pressLocal.y());
                const qreal y1 = qAbs(local.y());
                if (y0 > 1.0) {
                    sy = m_pressScaleY * (y1 / y0);
                }
            } else {
                const qreal ay = m_pressAnchorLocal.y();
                const qreal y0 = qAbs(pressLocal.y() - ay);
                const qreal y1 = qAbs(local.y() - ay);
                if (y0 > 1.0) {
                    sy = m_pressScaleY * (y1 / y0);
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
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal ss = screenScale();
    const qreal inset = kChromeInsetPx / ss;
    const qreal h = kSliderHeightPx / ss;
    const qreal maxW = kSliderWidthPx / ss;
    const qreal avail = qMax(0.0, r.width() - 2.0 * inset);
    const qreal w = qMin(maxW, avail);
    const qreal x = r.left() + inset + (avail - w) / 2.0;
    const qreal y = r.bottom() - inset - h;
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
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal ss = screenScale();
    const qreal rotOff = kRotateOffsetPx / ss;
    const qreal btn = kChromeBtnScreenPx / ss;
    const qreal gap = kChromeBtnGapPx / ss;
    const qreal inset = kChromeInsetPx / ss;
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();

    // Vertical stack on the right interior: FlipH/V, Raise/Lower, ResetScale/Rot.
    const qreal stackX = r.right() - inset - btn / 2.0;
    const int nChrome = 6;
    const qreal stackTop = cy - ((nChrome * btn + (nChrome - 1) * gap) / 2.0);
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
        return QPointF(cx, r.top() - rotOff);
    case Handle::RotateRight:
        return QPointF(r.right() + rotOff, cy);
    case Handle::RotateBottom:
        return QPointF(cx, r.bottom() + rotOff);
    case Handle::RotateLeft:
        return QPointF(r.left() - rotOff, cy);
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

    // Prefer chrome and opacity using view-pixel distances (rotation-safe).
    {
        const QRectF slider = opacitySliderRect();
        const QPointF a = localToViewPx(QPointF(slider.left(), slider.center().y()));
        const QPointF b = localToViewPx(QPointF(slider.right(), slider.center().y()));
        const QPointF p = localToViewPx(itemPos);
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

    Handle best = Handle::None;
    qreal bestDist = kChromeHitScreenPx;
    for (Handle h : {Handle::FlipH, Handle::FlipV, Handle::Raise, Handle::Lower,
                      Handle::ResetScale, Handle::ResetRotation}) {
        const qreal d = handleDistanceScreenPx(h, itemPos);
        if (d <= bestDist) {
            bestDist = d;
            best = h;
        }
    }
    if (best != Handle::None) {
        return best;
    }

    best = Handle::None;
    bestDist = kHandleScreenPx * 1.35;
    QList<Handle> handles = {
        Handle::RotateTop, Handle::RotateRight, Handle::RotateBottom, Handle::RotateLeft
    };
    if (m_scaleHandlesEnabled) {
        handles = QList<Handle>{Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                Handle::ScaleBottomLeft, Handle::ScaleBottomRight,
                                Handle::ScaleTop, Handle::ScaleRight,
                                Handle::ScaleBottom, Handle::ScaleLeft}
                  + handles;
    }
    for (Handle h : handles) {
        const qreal d = handleDistanceScreenPx(h, itemPos);
        if (d <= bestDist) {
            bestDist = d;
            best = h;
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
        setStackZ(m_stackZ + 1.0);
        break;
    case Handle::Lower:
        setStackZ(m_stackZ - 1.0);
        break;
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

    // Gallery: soft hover / selection frame, never transform chrome.
    if (!m_interactive) {
        const bool selected = option->state & QStyle::State_Selected;
        if (m_galleryHovered || selected) {
            painter->save();
            painter->setOpacity(1.0);
            QPen pen(selected ? QColor(0, 180, 255) : QColor(220, 220, 220, 180), 0);
            pen.setCosmetic(true);
            pen.setWidthF(selected ? 2.5 : 1.5);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
            if (m_galleryHovered && !selected) {
                painter->fillRect(r, QColor(255, 255, 255, 28));
            }
            painter->restore();
        }
        return;
    }

    if (!(option->state & QStyle::State_Selected)) {
        return;
    }

    paintInteractionChrome(painter, r);
}

void ImageItem::paintInteractionChrome(QPainter *painter, const QRectF &localRect) const
{
    // Chrome is drawn in *device* (viewport) pixels with an identity world
    // transform. Only the logical handle centres are mapped through the item
    // transform — so scaleX/scaleY never stretch the controls.
    const QTransform itemToDevice = painter->worldTransform();
    auto toDev = [&](const QPointF &local) {
        return itemToDevice.map(local);
    };
    auto mapDir = [&](const QPointF &localDelta) {
        const QPointF a = itemToDevice.map(QPointF(0, 0));
        const QPointF b = itemToDevice.map(localDelta);
        QPointF d = b - a;
        const qreal len = qHypot(d.x(), d.y());
        if (len > 1e-6) {
            d /= len;
        }
        return d;
    };

    painter->save();
    painter->setWorldTransform(QTransform());
    painter->setOpacity(1.0);
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(0, 160, 255), 0);
    pen.setCosmetic(true);
    pen.setWidthF(1.5);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    {
        const QPolygonF poly({
            toDev(localRect.topLeft()),
            toDev(localRect.topRight()),
            toDev(localRect.bottomRight()),
            toDev(localRect.bottomLeft()),
        });
        painter->drawPolygon(poly);
    }

    painter->drawLine(toDev(QPointF(localRect.center().x(), localRect.top())),
                      toDev(handleCenter(Handle::RotateTop)));
    painter->drawLine(toDev(QPointF(localRect.right(), localRect.center().y())),
                      toDev(handleCenter(Handle::RotateRight)));
    painter->drawLine(toDev(QPointF(localRect.center().x(), localRect.bottom())),
                      toDev(handleCenter(Handle::RotateBottom)));
    painter->drawLine(toDev(QPointF(localRect.left(), localRect.center().y())),
                      toDev(handleCenter(Handle::RotateLeft)));

    const qreal hs = kHandleScreenPx;

    if (m_scaleHandlesEnabled) {
        struct Corner { Handle h; QPointF localA; QPointF localB; };
        const Corner corners[] = {
            {Handle::ScaleTopLeft, QPointF(1, 0), QPointF(0, 1)},
            {Handle::ScaleTopRight, QPointF(-1, 0), QPointF(0, 1)},
            {Handle::ScaleBottomLeft, QPointF(1, 0), QPointF(0, -1)},
            {Handle::ScaleBottomRight, QPointF(-1, 0), QPointF(0, -1)},
        };
        for (const Corner &co : corners) {
            const bool hot = (m_hoverHandle == co.h || m_activeHandle == co.h);
            const QPointF c = toDev(handleCenter(co.h));
            const QPointF d1 = mapDir(co.localA);
            const QPointF d2 = mapDir(co.localB);
            const qreal arm = hs * (hot ? 1.6 : 1.35);
            QPen hp(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 2.5 : 2.0);
            hp.setCapStyle(Qt::SquareCap);
            painter->setPen(hp);
            painter->drawLine(c, c + d1 * arm);
            painter->drawLine(c, c + d2 * arm);
        }

        struct Edge { Handle h; QPointF localAlong; };
        const Edge edges[] = {
            {Handle::ScaleTop, QPointF(1, 0)},
            {Handle::ScaleBottom, QPointF(1, 0)},
            {Handle::ScaleLeft, QPointF(0, 1)},
            {Handle::ScaleRight, QPointF(0, 1)},
        };
        for (const Edge &ed : edges) {
            const bool hot = (m_hoverHandle == ed.h || m_activeHandle == ed.h);
            const QPointF c = toDev(handleCenter(ed.h));
            const QPointF along = mapDir(ed.localAlong);
            const QPointF perp(-along.y(), along.x());
            const qreal len = hs * (hot ? 2.4 : 2.0);
            const qreal thick = hs * (hot ? 0.45 : 0.35);
            QPen hp(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 2.0 : 1.5);
            painter->setPen(hp);
            painter->setBrush(hot ? QColor(80, 200, 255) : QColor(0, 160, 255, 220));
            QPolygonF bar;
            bar << c + along * (len / 2) + perp * (thick / 2)
                << c - along * (len / 2) + perp * (thick / 2)
                << c - along * (len / 2) - perp * (thick / 2)
                << c + along * (len / 2) - perp * (thick / 2);
            painter->drawPolygon(bar);
        }
    }

    for (Handle h : {Handle::RotateTop, Handle::RotateRight,
                     Handle::RotateBottom, Handle::RotateLeft}) {
        const QPointF c = toDev(handleCenter(h));
        const bool hot = (m_hoverHandle == h || m_activeHandle == h);
        const qreal s = hs * (hot ? 1.25 : 1.0);
        painter->setBrush(hot ? QColor(255, 230, 80) : QColor(255, 200, 40));
        QPen hp = pen;
        if (hot) {
            hp.setColor(Qt::white);
            hp.setWidthF(2.0);
        }
        painter->setPen(hp);
        painter->drawEllipse(c, s / 2.0, s / 2.0);
    }

    {
        const qreal btnR = kChromeBtnScreenPx / 2.0;
        auto drawBtn = [&](Handle h, const QString &glyph) {
            const QPointF c = toDev(handleCenter(h));
            const bool hovered = (m_hoverHandle == h);
            const bool active = (m_activeHandle == h);
            const qreal rad = btnR * (hovered || active ? 1.12 : 1.0);
            QColor fill = hovered || active ? QColor(0, 140, 255, 240) : QColor(50, 50, 50, 230);
            QPen border = pen;
            border.setColor(hovered || active ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(hovered || active ? 2.0 : 1.0);
            painter->setPen(border);
            painter->setBrush(fill);
            painter->drawEllipse(c, rad, rad);
            painter->setPen(QColor(240, 240, 240));
            QFont f = painter->font();
            f.setPointSizeF(qMax(7.0, rad * 0.7));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(QRectF(c.x() - rad, c.y() - rad, rad * 2, rad * 2),
                              Qt::AlignCenter, glyph);
        };
        drawBtn(Handle::FlipH, QStringLiteral("↔"));
        drawBtn(Handle::FlipV, QStringLiteral("↕"));
        drawBtn(Handle::Raise, QStringLiteral("↑"));
        drawBtn(Handle::Lower, QStringLiteral("↓"));
        drawBtn(Handle::ResetScale, QStringLiteral("1:1"));
        drawBtn(Handle::ResetRotation, QStringLiteral("0°"));
    }

    {
        const QRectF track = opacitySliderRect();
        const QPointF a = toDev(QPointF(track.left(), track.center().y()));
        const QPointF b = toDev(QPointF(track.right(), track.center().y()));
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
        trackPen.setWidthF(1.5);
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
        painter->drawEllipse(mid, 6.0, 6.0);
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
    // Fallback only: ImageView resolves chrome hits first (device-space). This
    // path runs when the scene delivers the event to the item body/handle.
    if (m_interactive && event->button() == Qt::LeftButton
        && beginHandleInteraction(event->scenePos(), event->modifiers())) {
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mousePressEvent(event);
}

void ImageItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_activeHandle != Handle::None) {
        updateHandleInteraction(event->scenePos(), event->modifiers());
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseMoveEvent(event);
}

void ImageItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_activeHandle != Handle::None && event->button() == Qt::LeftButton) {
        endHandleInteraction();
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseReleaseEvent(event);
}

void ImageItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (!m_interactive && (flags() & ItemIsSelectable)) {
        if (!m_galleryHovered) {
            m_galleryHovered = true;
            update();
        }
        setCursor(Qt::PointingHandCursor);
        QGraphicsPixmapItem::hoverMoveEvent(event);
        return;
    }

    if (m_interactive && isSelected()) {
        const Handle h = handleAt(event->pos());
        if (h != m_hoverHandle) {
            m_hoverHandle = h;
            setToolTip(tooltipForHandle(h));
            update();
        }
        switch (h) {
        case Handle::RotateTop:
        case Handle::RotateRight:
        case Handle::RotateBottom:
        case Handle::RotateLeft:
            setCursor(Qt::CrossCursor);
            break;
        case Handle::ScaleTopLeft:
        case Handle::ScaleBottomRight: {
            const qreal a = std::fmod(std::fabs(m_rotation), 180.0);
            const bool swap = (a > 45.0 && a < 135.0);
            setCursor(swap ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor);
            break;
        }
        case Handle::ScaleTopRight:
        case Handle::ScaleBottomLeft: {
            const qreal a = std::fmod(std::fabs(m_rotation), 180.0);
            const bool swap = (a > 45.0 && a < 135.0);
            setCursor(swap ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
            break;
        }
        case Handle::ScaleTop:
        case Handle::ScaleBottom: {
            const qreal a = std::fmod(std::fabs(m_rotation), 180.0);
            setCursor((a > 45.0 && a < 135.0) ? Qt::SizeHorCursor : Qt::SizeVerCursor);
            break;
        }
        case Handle::ScaleLeft:
        case Handle::ScaleRight: {
            const qreal a = std::fmod(std::fabs(m_rotation), 180.0);
            setCursor((a > 45.0 && a < 135.0) ? Qt::SizeVerCursor : Qt::SizeHorCursor);
            break;
        }
        case Handle::FlipH:
        case Handle::FlipV:
        case Handle::Raise:
        case Handle::Lower:
        case Handle::ResetScale:
        case Handle::ResetRotation:
            setCursor(Qt::PointingHandCursor);
            break;
        case Handle::OpacitySlider: {
            const qreal a = std::fmod(std::fabs(m_rotation), 180.0);
            setCursor((a > 45.0 && a < 135.0) ? Qt::SizeVerCursor : Qt::SizeHorCursor);
            break;
        }
        default:
            unsetCursor();
            break;
        }
    } else {
        unsetCursor();
    }
    QGraphicsPixmapItem::hoverMoveEvent(event);
}

void ImageItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    if (m_galleryHovered) {
        m_galleryHovered = false;
        update();
    }
    if (m_hoverHandle != Handle::None) {
        m_hoverHandle = Handle::None;
        setToolTip(QString());
        update();
    }
    unsetCursor();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}
