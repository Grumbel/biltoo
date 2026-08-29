// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include <QGraphicsView>
#include <QString>

class QGraphicsPixmapItem;
class QGraphicsScene;

class ImageView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit ImageView(QWidget *parent = nullptr);
    ~ImageView() override;

    bool loadImage(const QString &path);
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    void rotateLeft();
    void rotateRight();

    QString statusText() const;

signals:
    void statusChanged();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void applyTransform();
    void updateFitIfNeeded();

    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;

    qreal m_scale = 1.0;
    qreal m_rotation = 0.0; // degrees
    bool m_fitMode = false;
    QString m_currentPath;
    QSize m_imageSize;

    QPoint m_lastMousePos;
    bool m_panning = false;
};

#endif // IMAGEVIEW_H
