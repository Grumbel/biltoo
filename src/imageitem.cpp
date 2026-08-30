// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"

#include <QCursor>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {
constexpr qreal kHandleScreenPx = 9.0;
constexpr qreal kRotateOffsetPx = 28.0;
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
        const qreal pad = handleHitRadius() + kRotateOffsetPx / qMax(m_scale, 0.01) + 4.0;
        r.adjust(-pad, -pad, pad, pad);
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
        for (Handle h : {Handle::ScaleTopLeft, Handle::ScaleTopRight,
                         Handle::ScaleBottomLeft, Handle::ScaleBottomRight,
                         Handle::Rotate}) {
            const QPointF c = handleCenter(h);
            path.addEllipse(c, r, r);
        }
        // Thin corridor to the rotate handle
        const QPointF topMid = handleCenter(Handle::ScaleTopLeft) * 0.5
                               + handleCenter(Handle::ScaleTopRight) * 0.5;
        // Actually use content top centre
        const QRectF cr = QGraphicsPixmapItem::boundingRect();
        path.moveTo(cr.center().x(), cr.top());
        path.lineTo(handleCenter(Handle::Rotate));
    }
    return path;
}

qreal ImageItem::handleDrawSize() const
{
    // Keep handle roughly constant on screen
    return kHandleScreenPx / qMax(m_scale, 0.01);
}

qreal ImageItem::handleHitRadius() const
{
    return handleDrawSize() * 1.2;
}

QPointF ImageItem::handleCenter(Handle h) const
{
    const QRectF r = QGraphicsPixmapItem::boundingRect();
    const qreal rotOff = kRotateOffsetPx / qMax(m_scale, 0.01);
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
        return QPointF(r.center().x(), r.top() - rotOff);
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
    for (Handle h : {Handle::Rotate, Handle::ScaleTopLeft, Handle::ScaleTopRight,
                     Handle::ScaleBottomLeft, Handle::ScaleBottomRight}) {
        const QPointF c = handleCenter(h);
        const QPointF d = itemPos - c;
        if (QPointF::dotProduct(d, d) <= r2) {
            return h;
        }
    }
    return Handle::None;
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

    // Scale handles (squares)
    const qreal hs = handleDrawSize();
    painter->setBrush(QColor(0, 160, 255));
    pen.setWidthF(1.0);
    painter->setPen(pen);
    for (Handle h : {Handle::ScaleTopLeft, Handle::ScaleTopRight,
                     Handle::ScaleBottomLeft, Handle::ScaleBottomRight}) {
        const QPointF c = handleCenter(h);
        painter->drawRect(QRectF(c.x() - hs / 2, c.y() - hs / 2, hs, hs));
    }

    // Rotate handle (circle)
    painter->setBrush(QColor(255, 200, 40));
    painter->drawEllipse(rot, hs / 2, hs / 2);

    painter->restore();
}

void ImageItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_interactive && event->button() == Qt::LeftButton) {
        const Handle h = handleAt(event->pos());
        if (h != Handle::None) {
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
        default:
            unsetCursor();
            break;
        }
    } else {
        unsetCursor();
    }
    QGraphicsPixmapItem::hoverMoveEvent(event);
}
