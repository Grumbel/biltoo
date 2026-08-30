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
constexpr qreal kHandleScreenPx = 28.0;      // scale/rotate markers in *viewport* px
constexpr qreal kRotateOffsetPx = 48.0;      // rotate handle distance from edge (viewport px)
constexpr qreal kChromeBtnScreenPx = 30.0;   // flip / raise / lower diameter (viewport px)
constexpr qreal kChromeHitScreenPx = 28.0;   // hit radius in viewport px
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
        const qreal hitLocal = (kHandleScreenPx * 1.75) / sMin;
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
        const qreal r = (kHandleScreenPx * 2.0) / sMin;
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
    bestDist = kHandleScreenPx * 1.75;
    // Corners and rotate handles: disc around centre.
    QList<Handle> pointHandles = {
        Handle::RotateTop, Handle::RotateRight, Handle::RotateBottom, Handle::RotateLeft
    };
    if (m_scaleHandlesEnabled) {
        pointHandles = QList<Handle>{Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                     Handle::ScaleBottomLeft, Handle::ScaleBottomRight}
                       + pointHandles;
    }
    for (Handle h : pointHandles) {
        const qreal d = handleDistanceScreenPx(h, itemPos);
        if (d <= bestDist) {
            bestDist = d;
            best = h;
        }
    }

    // Edge stretch: distance to the edge segment in view space (matches the
    // thick bar drawn along the edge), not only the midpoint disc.
    if (m_scaleHandlesEnabled) {
        const QRectF r = QGraphicsPixmapItem::boundingRect();
        struct EdgeSeg {
            Handle h;
            QPointF aLocal;
            QPointF bLocal;
        };
        const qreal half = (kHandleScreenPx * 2.4) / screenScale() * 0.5;
        const EdgeSeg edges[] = {
            {Handle::ScaleTop,    QPointF(r.center().x() - half, r.top()),
                                  QPointF(r.center().x() + half, r.top())},
            {Handle::ScaleBottom, QPointF(r.center().x() - half, r.bottom()),
                                  QPointF(r.center().x() + half, r.bottom())},
            {Handle::ScaleLeft,   QPointF(r.left(), r.center().y() - half),
                                  QPointF(r.left(), r.center().y() + half)},
            {Handle::ScaleRight,  QPointF(r.right(), r.center().y() - half),
                                  QPointF(r.right(), r.center().y() + half)},
        };
        const QPointF p = localToViewPx(itemPos);
        const qreal edgeHit = kHandleScreenPx * 0.85; // half thickness + pad
        for (const EdgeSeg &ed : edges) {
            const QPointF a = localToViewPx(ed.aLocal);
            const QPointF b = localToViewPx(ed.bLocal);
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
    // Caller is ImageView::paintEvent with QPainter(viewport()) — do not reset
    // world transform in a way that fights HiDPI; map centres via the view.
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
    // Unit direction of a local-space vector, in viewport pixels (rotates/scales
    // with the item so brackets and edge bars follow the frame).
    auto dirView = [this, view](const QPointF &localDelta) -> QPointF {
        const QPointF a = QPointF(view->mapFromScene(mapToScene(QPointF(0, 0))));
        const QPointF b = QPointF(view->mapFromScene(mapToScene(localDelta)));
        QPointF d = b - a;
        const qreal len = qHypot(d.x(), d.y());
        if (len > 1e-6) {
            d /= len;
        } else {
            d = QPointF(1, 0);
        }
        return d;
    };

    painter->save();
    painter->setOpacity(1.0);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Fixed on-screen sizes — never multiply by item or view scale.
    const qreal hs = kHandleScreenPx;

    QPen framePen(QColor(0, 160, 255), 0);
    framePen.setCosmetic(true);
    framePen.setWidthF(2.0);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    {
        const QPolygonF poly({
            toView(localRect.topLeft()),
            toView(localRect.topRight()),
            toView(localRect.bottomRight()),
            toView(localRect.bottomLeft()),
        });
        painter->drawPolygon(poly);
    }

    // Rotate stems
    painter->drawLine(toView(QPointF(localRect.center().x(), localRect.top())),
                      toView(handleCenter(Handle::RotateTop)));
    painter->drawLine(toView(QPointF(localRect.right(), localRect.center().y())),
                      toView(handleCenter(Handle::RotateRight)));
    painter->drawLine(toView(QPointF(localRect.center().x(), localRect.bottom())),
                      toView(handleCenter(Handle::RotateBottom)));
    painter->drawLine(toView(QPointF(localRect.left(), localRect.center().y())),
                      toView(handleCenter(Handle::RotateLeft)));

    if (m_scaleHandlesEnabled) {
        // Corner L-brackets: arms along the *image* edges (rotate with frame).
        struct Corner {
            Handle h;
            QPointF alongA; // local direction of first arm
            QPointF alongB;
        };
        const Corner corners[] = {
            {Handle::ScaleTopLeft, QPointF(1, 0), QPointF(0, 1)},
            {Handle::ScaleTopRight, QPointF(-1, 0), QPointF(0, 1)},
            {Handle::ScaleBottomLeft, QPointF(1, 0), QPointF(0, -1)},
            {Handle::ScaleBottomRight, QPointF(-1, 0), QPointF(0, -1)},
        };
        for (const Corner &co : corners) {
            const bool hot = (m_hoverHandle == co.h || m_activeHandle == co.h);
            const QPointF c = toView(handleCenter(co.h));
            const QPointF d1 = dirView(co.alongA);
            const QPointF d2 = dirView(co.alongB);
            const qreal arm = hs * (hot ? 1.7 : 1.45);
            QPen hp(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 3.0 : 2.5);
            hp.setCapStyle(Qt::SquareCap);
            painter->setPen(hp);
            painter->drawLine(c, c + d1 * arm);
            painter->drawLine(c, c + d2 * arm);
            // Solid grip square, oriented with the frame.
            const qreal half = hs * (hot ? 0.38 : 0.32);
            const QPointF px = dirView(QPointF(1, 0));
            const QPointF py = dirView(QPointF(0, 1));
            QPolygonF sq;
            sq << c + (-px - py) * half << c + (px - py) * half
               << c + (px + py) * half << c + (-px + py) * half;
            painter->setBrush(hot ? QColor(255, 255, 255) : QColor(0, 160, 255, 230));
            painter->drawPolygon(sq);
            painter->setBrush(Qt::NoBrush);
        }

        // Edge stretch bars along the image edge (rotate with frame).
        struct Edge {
            Handle h;
            QPointF localAlong;
        };
        const Edge edges[] = {
            {Handle::ScaleTop, QPointF(1, 0)},
            {Handle::ScaleBottom, QPointF(1, 0)},
            {Handle::ScaleLeft, QPointF(0, 1)},
            {Handle::ScaleRight, QPointF(0, 1)},
        };
        for (const Edge &ed : edges) {
            const bool hot = (m_hoverHandle == ed.h || m_activeHandle == ed.h);
            const QPointF c = toView(handleCenter(ed.h));
            const QPointF along = dirView(ed.localAlong);
            const QPointF perp(-along.y(), along.x());
            const qreal len = hs * (hot ? 3.0 : 2.6);
            const qreal thick = hs * (hot ? 0.75 : 0.60);
            QPen hp(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 2.0 : 1.5);
            painter->setPen(hp);
            painter->setBrush(hot ? QColor(80, 200, 255) : QColor(0, 160, 255, 230));
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
        const QPointF c = toView(handleCenter(h));
        const bool hot = (m_hoverHandle == h || m_activeHandle == h);
        const qreal s = hs * (hot ? 1.2 : 1.0);
        painter->setBrush(hot ? QColor(255, 230, 80) : QColor(255, 200, 40));
        QPen hp(hot ? QColor(255, 255, 255) : QColor(40, 40, 40), 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 2.0 : 1.5);
        painter->setPen(hp);
        painter->drawEllipse(c, s / 2.0, s / 2.0);
    }

    {
        const qreal btnR = kChromeBtnScreenPx / 2.0;
        auto drawBtn = [&](Handle h, const QString &glyph) {
            const QPointF c = toView(handleCenter(h));
            const bool hovered = (m_hoverHandle == h);
            const bool active = (m_activeHandle == h);
            const qreal rad = btnR * (hovered || active ? 1.12 : 1.0);
            QColor fill = hovered || active ? QColor(0, 140, 255, 240) : QColor(50, 50, 50, 230);
            QPen border(hovered || active ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(hovered || active ? 2.0 : 1.0);
            border.setCosmetic(true);
            painter->setPen(border);
            painter->setBrush(fill);
            painter->drawEllipse(c, rad, rad);
            painter->setPen(QColor(240, 240, 240));
            QFont f = painter->font();
            f.setPointSizeF(qMax(8.0, rad * 0.65));
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
        painter->drawEllipse(mid, 7.0, 7.0);
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
