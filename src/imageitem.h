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
 */
class ImageItem : public QGraphicsPixmapItem
{
public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

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

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

private:
    void applyLocalTransform();

    QString m_path;
    QImage m_source;
    qreal m_scale = 1.0;
    qreal m_rotation = 0.0;
    qreal m_opacity = 1.0;
    bool m_interactive = false;
};

#endif // IMAGEITEM_H
