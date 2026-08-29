// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include <QGraphicsView>
#include <QPoint>
#include <QString>
#include <QStringList>

class ImageItem;
class QGraphicsScene;

struct ImageMouseInfo {
    bool valid = false;
    QPoint imagePos;
    QColor pixelColor;
    QString path;
};

/**
 * Workspace view: one or more ImageItems on a QGraphicsScene.
 * loadImage() replaces the workspace with a single image.
 * addImage() places an additional image for comparison.
 */
class ImageView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit ImageView(QWidget *parent = nullptr);
    ~ImageView() override;

    bool loadImage(const QString &path);
    bool addImage(const QString &path);
    void clearWorkspace();
    void setWorkspaceMode(bool on);
    bool workspaceMode() const { return m_workspaceMode; }
    /** Remove all items except the primary (first) session image, if any. */
    void clearExtras();

    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    void rotateLeft();
    void rotateRight();

    void raiseSelected();
    void lowerSelected();
    void opacityUp();
    void opacityDown();
    void opacityReset();

    /** Arrange all items in a simple horizontal row. */
    void layoutSideBySide();

    QString statusText() const;
    ImageMouseInfo mouseInfo() const { return m_mouseInfo; }
    QString currentPath() const;
    QSize imageSize() const;
    int itemCount() const;
    QStringList itemPaths() const;

signals:
    void statusChanged();
    void mouseInfoChanged(const ImageMouseInfo &info);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    ImageItem *loadItem(const QString &path);
    ImageItem *primaryItem() const;
    ImageItem *targetItem() const;
    void updateMouseInfo(const QPoint &viewPos);
    void fitItem(ImageItem *item);
    void ensureVisibleItem(ImageItem *item);
    qreal angleAt(const QPointF &scenePos, ImageItem *item) const;

    QGraphicsScene *m_scene = nullptr;
    QList<ImageItem *> m_items;

    bool m_fitMode = true;
    bool m_workspaceMode = false;
    ImageMouseInfo m_mouseInfo;

    QPoint m_lastMousePos;
    bool m_panning = false;

    // Free rotation: Shift + left drag around item centre
    bool m_rotating = false;
    ImageItem *m_rotateItem = nullptr;
    qreal m_rotateStartAngle = 0.0;
    qreal m_rotateItemStart = 0.0;
};

#endif // IMAGEVIEW_H
