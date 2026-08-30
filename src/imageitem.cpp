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
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {
constexpr qreal kHandleScreenPx = 9.0;
constexpr qreal kRotateOffsetPx = 28.0;
constexpr qreal kChromeOffsetPx = 26.0; // below content, screen pixels
constexpr qreal kChromeSpacingPx = 22.0;
} // namespace

ImageItem::ImageItem(const QString &path, const QImage &image, QGraphicsItem *parent)
    : QGraphicsPixmapItem(parent)
    , m_path(path)
    , m_source(image)
{
    setPixmap(QPixmap::fromImage(image));
    setTransformationMode(Qt::SmoothTransformation);
    // Classic viewer by default: not selectable/movable until workspace mode
    setFlags(ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setOffset(-image.width() / 2.0, -image.height() / 2.0); // origin at centre
    applyLocalTransform();
}

void ImageItem::setItemScale(qreal scale)
{
    m_scale = qMax(0.01, scale);
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::setItemRotation(qreal degrees)
{
    m_rotation = degrees;
    while (m_rotation >= 360.0) {
        m_rotation -= 360.0;
    }
    while (m_rotation < 0.0) {
        m_rotation += 360.0;
    }
    applyLocalTransform();
    prepareGeometryChange();
}

void ImageItem::zoomBy(qreal factor)
{
    setItemScale(m_scale * factor);
}

void ImageItem::rotateBy(qreal degrees)
{
    setItemRotation(m_rotation + degrees);
}

void ImageItem::setItemOpacity(qreal opacity)
{
    m_opacity = qBound(0.05, opacity, 1.0);
    setOpacity(m_opacity);
}

void ImageItem::setInteractive(bool on)
{
    m_interactive = on;
    if (on) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges
                 | ItemIsFocusable);
    } else {
        setSelected(false);
        setFlags(ItemSendsGeometryChanges);
    }
}

void ImageItem::setScaleHandlesEnabled(bool on)
{
    if (m_scaleHandlesEnabled == on) {
        return;
    }
    m_scaleHandlesEnabled = on;
    prepareGeometryChange();
    update();
}

void ImageItem::applyLocalTransform()
{
    QTransform t;
    t.rotate(m_rotation);
    t.scale(m_scale, m_scale);
    setTransform(t);
}

QPoint ImageItem::pixelAtScenePos(const QPointF &scenePos) const
{
    const QPointF local = mapFromScene(scenePos) - offset();
    const int x = static_cast<int>(local.x());
    const int y = static_cast<int>(local.y());
    if (x < 0 || y < 0 || x >= m_source.width() || y >= m_source.height()) {
        return QPoint(-1, -1);
    }
    return QPoint(x, y);
}

QColor ImageItem::colorAtPixel(const QPoint &pixel) const
{
    if (pixel.x() < 0 || pixel.y() < 0
        || pixel.x() >= m_source.width() || pixel.y() >= m_source.height()) {
        return QColor();
    }
    return m_source.pixelColor(pixel);
}

QRectF ImageItem::contentRect() const
{
    return boundingRect(); // overridden below; use pixmap rect
}

QRectF ImageItem::boundingRect() const
{
    // Expand for handles when selected so they are not clipped
    QRectF r = QGraphicsPixmapItem::boundingRect();
    if (isSelected() && m_interactive) {
        const qreal ss = screenScale();
        const qreal topPad = handleHitRadius() + kRotateOffsetPx / ss + 4.0;
        const qreal bottomPad = handleHitRadius() + kChromeOffsetPx / ss + 4.0;
        const qreal sidePad = handleHitRadius() + 4.0;
        r.adjust(-sidePad, -topPad, sidePad, bottomPad);
    }
    return r;
}

QPainterPath ImageItem::shape() const
{
    // Hit-test the content plus handles when selected
    QPainterPath path;
    path.addRect(QGraphicsPixmapItem::boundingRect());
    if (isSelected() && m_interactive) {
        const qreal r = handleHitRadius();
        QList<Handle> handles = {Handle::Rotate, Handle::Raise, Handle::Lower,
                                  Handle::OpacityDown, Handle::OpacityUp};
        if (m_scaleHandlesEnabled) {
            handles = QList<Handle>{Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                    Handle::ScaleBottomLeft, Handle::ScaleBottomRight}
                      + handles;
        }
        for (Handle h : handles) {
            const QPointF c = handleCenter(h);
            path.addEllipse(c, r, r);
        }
        const QRectF cr = QGraphicsPixmapItem::boundingRect();
        path.moveTo(cr.center().x(), cr.top());
        path.lineTo(handleCenter(Handle::Rotate));
    }
    return path;
}

qreal ImageItem::screenScale() const
{
    // Item local scale × view transform scale → pixels per local unit
    qreal s = qMax(m_scale, 0.01);
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            const QTransform t = views.first()->transform();
            s *= std::hypot(t.m11(), t.m12());
        }
    }
    return qMax(s, 0.01);
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
    return h == Handle::Raise || h == Handle::Lower
           || h == Handle::OpacityDown || h == Handle::OpacityUp;
}

QPointF ImageItem::handleCenter(Handle h) const
{
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal ss = screenScale();
    const qreal rotOff = kRotateOffsetPx / ss;
    const qreal chromeY = r.bottom() + kChromeOffsetPx / ss;
    const qreal spacing = kChromeSpacingPx / ss;
    const qreal cx = r.center().x();
    switch (h) {
    case Handle::ScaleTopLeft:
        return r.topLeft();
    case Handle::ScaleTopRight:
        return r.topRight();
    case Handle::ScaleBottomLeft:
        return r.bottomLeft();
    case Handle::ScaleBottomRight:
        return r.bottomRight();
    case Handle::Rotate:
        return QPointF(cx, r.top() - rotOff);
    case Handle::Raise:
        return QPointF(cx - 1.5 * spacing, chromeY);
    case Handle::Lower:
        return QPointF(cx - 0.5 * spacing, chromeY);
    case Handle::OpacityDown:
        return QPointF(cx + 0.5 * spacing, chromeY);
    case Handle::OpacityUp:
        return QPointF(cx + 1.5 * spacing, chromeY);
    default:
        return QPointF();
    }
}

ImageItem::Handle ImageItem::handleAt(const QPointF &itemPos) const
{
    if (!isSelected() || !m_interactive) {
        return Handle::None;
    }
    const qreal r = handleHitRadius();
    const qreal r2 = r * r;
    QList<Handle> handles = {Handle::Rotate, Handle::Raise, Handle::Lower,
                              Handle::OpacityDown, Handle::OpacityUp};
    if (m_scaleHandlesEnabled) {
        handles = QList<Handle>{Handle::ScaleTopLeft, Handle::ScaleTopRight,
                                Handle::ScaleBottomLeft, Handle::ScaleBottomRight}
                  + handles;
    }
    for (Handle h : handles) {
        const QPointF c = handleCenter(h);
        const QPointF d = itemPos - c;
        if (QPointF::dotProduct(d, d) <= r2) {
            return h;
        }
    }
    return Handle::None;
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
    case Handle::Raise:
        setZValue(zValue() + 1.0);
        break;
    case Handle::Lower:
        setZValue(zValue() - 1.0);
        break;
    case Handle::OpacityUp:
        setItemOpacity(m_opacity + 0.1);
        break;
    case Handle::OpacityDown:
        setItemOpacity(m_opacity - 0.1);
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
    }
    return QGraphicsPixmapItem::itemChange(change, value);
}

void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget)
{
    QGraphicsPixmapItem::paint(painter, option, widget);

    if (!(option->state & QStyle::State_Selected) || !m_interactive) {
        return;
    }

    painter->save();

    const QRectF r = QGraphicsPixmapItem::boundingRect();
    QPen pen(QColor(0, 160, 255), 0); // cosmetic width via setCosmetic
    pen.setCosmetic(true);
    pen.setWidthF(1.5);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));

    // Line to rotate handle
    const QPointF rot = handleCenter(Handle::Rotate);
    const QPointF topMid(r.center().x(), r.top());
    painter->drawLine(topMid, rot);

    const qreal hs = handleDrawSize();
    pen.setWidthF(1.0);
    painter->setPen(pen);

    // Scale handles (squares) — only in free-form layouts
    if (m_scaleHandlesEnabled) {
        painter->setBrush(QColor(0, 160, 255));
        for (Handle h : {Handle::ScaleTopLeft, Handle::ScaleTopRight,
                         Handle::ScaleBottomLeft, Handle::ScaleBottomRight}) {
            const QPointF c = handleCenter(h);
            painter->drawRect(QRectF(c.x() - hs / 2, c.y() - hs / 2, hs, hs));
        }
    }

    // Rotate handle (circle)
    painter->setBrush(QColor(255, 200, 40));
    painter->drawEllipse(rot, hs / 2, hs / 2);

    // Chrome buttons under the image: raise, lower, opacity − / +
    auto drawChrome = [&](Handle h, const QString &glyph, const QColor &fill) {
        const QPointF c = handleCenter(h);
        painter->setBrush(fill);
        painter->setPen(pen);
        painter->drawEllipse(c, hs / 2 + 1, hs / 2 + 1);
        painter->setPen(QPen(Qt::white));
        // Counter-scale text so glyph size stays readable on screen
        painter->save();
        painter->translate(c);
        const qreal ss = screenScale();
        painter->scale(1.0 / ss, 1.0 / ss);
        QFont f = painter->font();
        f.setBold(true);
        f.setPixelSize(11);
        painter->setFont(f);
        const QRectF tr(-12, -12, 24, 24);
        painter->drawText(tr, Qt::AlignCenter, glyph);
        painter->restore();
    };
    drawChrome(Handle::Raise, QStringLiteral("↑"), QColor(60, 60, 60, 220));
    drawChrome(Handle::Lower, QStringLiteral("↓"), QColor(60, 60, 60, 220));
    drawChrome(Handle::OpacityDown, QStringLiteral("−"), QColor(80, 50, 120, 220));
    drawChrome(Handle::OpacityUp, QStringLiteral("+"), QColor(80, 50, 120, 220));

    painter->restore();
}

void ImageItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_interactive && event->button() == Qt::LeftButton) {
        const Handle h = handleAt(event->pos());
        if (h != Handle::None) {
            if (isChromeHandle(h)) {
                activateChromeHandle(h);
                event->accept();
                return;
            }
            m_activeHandle = h;
            m_pressScenePos = event->scenePos();
            m_pressScale = m_scale;
            m_pressRotation = m_rotation;
            m_pressItemPos = event->pos();
            event->accept();
            return;
        }
    }
    QGraphicsPixmapItem::mousePressEvent(event);
}

void ImageItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_activeHandle != Handle::None) {
        if (m_activeHandle == Handle::Rotate) {
            const QPointF centre = scenePos(); // item origin is image centre
            const QPointF v0 = m_pressScenePos - centre;
            const QPointF v1 = event->scenePos() - centre;
            const qreal a0 = qAtan2(v0.y(), v0.x());
            const qreal a1 = qAtan2(v1.y(), v1.x());
            const qreal deltaDeg = qRadiansToDegrees(a1 - a0);
            setItemRotation(m_pressRotation + deltaDeg);
        } else {
            // Uniform scale from item centre based on distance ratio
            const QPointF centre = scenePos();
            const qreal d0 = QLineF(centre, m_pressScenePos).length();
            const qreal d1 = QLineF(centre, event->scenePos()).length();
            if (d0 > 1.0) {
                setItemScale(m_pressScale * (d1 / d0));
            }
        }
        notifyViewStatus();
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseMoveEvent(event);
}

void ImageItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_activeHandle != Handle::None && event->button() == Qt::LeftButton) {
        m_activeHandle = Handle::None;
        event->accept();
        return;
    }
    QGraphicsPixmapItem::mouseReleaseEvent(event);
}

void ImageItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (m_interactive && isSelected()) {
        const Handle h = handleAt(event->pos());
        switch (h) {
        case Handle::Rotate:
            setCursor(Qt::CrossCursor);
            break;
        case Handle::ScaleTopLeft:
        case Handle::ScaleBottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Handle::ScaleTopRight:
        case Handle::ScaleBottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Handle::Raise:
        case Handle::Lower:
        case Handle::OpacityDown:
        case Handle::OpacityUp:
            setCursor(Qt::PointingHandCursor);
            break;
        default:
            unsetCursor();
            break;
        }
    } else {
        unsetCursor();
    }
    QGraphicsPixmapItem::hoverMoveEvent(event);
}
