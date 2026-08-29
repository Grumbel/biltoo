// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

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
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
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

QVariant ImageItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    return QGraphicsPixmapItem::itemChange(change, value);
}

void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget)
{
    QGraphicsPixmapItem::paint(painter, option, widget);

    if (option->state & QStyle::State_Selected) {
        painter->save();
        QPen pen(QColor(0, 160, 255), 2 / m_scale);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(1, 1, -1, -1));
        painter->restore();
    }
}
