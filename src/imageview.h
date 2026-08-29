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
class QUndoStack;

struct ImageMouseInfo {
    bool valid = false;
    QPoint imagePos;
    QColor pixelColor;
    QString path;
};

struct WorkspaceItemState {
    QString path;
    QPointF pos;
    qreal scale = 1.0;
    qreal rotation = 0.0;
    qreal opacity = 1.0;
    qreal z = 0.0;
};

/**
 * Image view with an optional multi-item workspace.
 * In classic mode the current image is shown centred and non-interactive.
 * Workspace state is snapshotted when the mode is turned off and restored
 * when it is turned back on.
 */
class ImageView : public QGraphicsView
{
    Q_OBJECT

public:
    enum class Tool {
        Select,
        Pan
    };

    explicit ImageView(QWidget *parent = nullptr);
    ~ImageView() override;

    bool loadImage(const QString &path);
    bool addImage(const QString &path);
    void clearWorkspace();
    void setWorkspaceMode(bool on);
    bool workspaceMode() const { return m_workspaceMode; }
    void clearExtras();

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    QUndoStack *undoStack() const { return m_undoStack; }

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

    enum class LayoutMode {
        FreeForm,
        SideBySide,
        Vertical,
        Grid,
        Stack
    };

    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layoutMode; }
    void applyLayout();

    WorkspaceItemState captureState(const ImageItem *item) const;
    void applyState(ImageItem *item, const WorkspaceItemState &state);

    QString statusText() const;
    ImageMouseInfo mouseInfo() const { return m_mouseInfo; }
    QString currentPath() const;
    QSize imageSize() const;
    int itemCount() const;
    QStringList itemPaths() const;

signals:
    void statusChanged();
    void mouseInfoChanged(const ImageMouseInfo &info);
    void toolChanged(ImageView::Tool tool);

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
    void snapshotWorkspace();
    void restoreWorkspace();

    QGraphicsScene *m_scene = nullptr;
    QList<ImageItem *> m_items;
    QList<WorkspaceItemState> m_savedWorkspace;
    QString m_classicPath;

    QUndoStack *m_undoStack = nullptr;

    bool m_fitMode = true;
    bool m_workspaceMode = false;
    Tool m_tool = Tool::Select;
    ImageMouseInfo m_mouseInfo;

    QPoint m_lastMousePos;
    bool m_panning = false;

    bool m_rotating = false;
    ImageItem *m_rotateItem = nullptr;
    qreal m_rotateStartAngle = 0.0;
    qreal m_rotateItemStart = 0.0;

    ImageItem *m_dragItem = nullptr;
    WorkspaceItemState m_dragStartState;
};

#endif // IMAGEVIEW_H
