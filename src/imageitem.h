// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEITEM_H
#define IMAGEITEM_H

#include <QGraphicsPixmapItem>
#include <QImage>
#include <QString>

/**
 * A single image on the workspace. Owns its pixmap, source pixels (for colour
 * sampling), and local scale/rotation applied around the item centre.
 *
 * When selected and interactive, draws scale and rotate handles and supports
 * direct manipulation of those handles with the mouse.
 */
class ImageItem : public QGraphicsPixmapItem
{
public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    enum class Handle {
        None,
        ScaleTopLeft,
        ScaleTopRight,
        ScaleBottomLeft,
        ScaleBottomRight,
        Rotate
    };

    explicit ImageItem(const QString &path, const QImage &image,
                       QGraphicsItem *parent = nullptr);

    QString path() const { return m_path; }
    QSize imageSize() const { return m_source.size(); }
    const QImage &sourceImage() const { return m_source; }

    qreal itemScale() const { return m_scale; }
    qreal itemRotation() const { return m_rotation; }
    qreal itemOpacity() const { return m_opacity; }

    void setItemScale(qreal scale);
    void setItemRotation(qreal degrees);
    void setItemOpacity(qreal opacity);
    void zoomBy(qreal factor);
    void rotateBy(qreal degrees);

    /** When false, the item cannot be selected or dragged (classic viewer). */
    void setInteractive(bool on);

    /** Map a scene position to integer pixel coordinates, or (-1,-1) if outside. */
    QPoint pixelAtScenePos(const QPointF &scenePos) const;
    QColor colorAtPixel(const QPoint &pixel) const;

    /** Which handle (if any) is under the given item-local position. */
    Handle handleAt(const QPointF &itemPos) const;

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    void applyLocalTransform();
    QRectF contentRect() const;
    /** Handle centres in item coordinates (pre-transform / local). */
    QPointF handleCenter(Handle h) const;
    qreal handleHitRadius() const;
    qreal handleDrawSize() const;

    QString m_path;
    QImage m_source;
    qreal m_scale = 1.0;
    qreal m_rotation = 0.0;
    qreal m_opacity = 1.0;
    bool m_interactive = false;

    Handle m_activeHandle = Handle::None;
    QPointF m_pressScenePos;
    qreal m_pressScale = 1.0;
    qreal m_pressRotation = 0.0;
    QPointF m_pressItemPos;
};

#endif // IMAGEITEM_H
