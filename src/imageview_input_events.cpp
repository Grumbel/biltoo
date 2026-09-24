// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include <cstdlib>
#include <QEvent>
#include "item/itemcomponents.h"
#include "gallery/gallerydecodesm.h"
#include "image/toolpolicy.h"
#include "workspace/workspacenavgeometry.h"
#include "workspace/grouptransformgeometry.h"
#include "item/selectiongeometry.h"
#include "view/viewtransform.h"
#include "item/placementlinear.h"
#include "attention/attentiongeometry.h"
#include "image/edgenavpolicy.h"
#include "host/pagepath.h"
#include "gallery/gallerylayout.h"
#include "imageitem.h"
#include "host/imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QSet>
#include <QThreadPool>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QWheelEvent>
#include <QRubberBand>
#include <QtMath>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <QGraphicsItem>
#include "host/thumtoocache.h"

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (m_shell.handleMousePress(event)) {
        return;
    }
    QGraphicsView::mousePressEvent(event);
}
void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseMove(event)) {
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}
void ImageView::restoreToolCursor()
{
    setCursor(ToolPolicy::cursorFor(m_workspace.currentTool()));
}
void ImageView::pushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                                      const ItemComponents::Placement &after, const QString &text)
{
    if (!item) {
        return;
    }
    if (ItemComponents::placementNearlyEqual(before, after)) {
        return;
    }
    // Single geometry undo path (persist + Placement command).
    pushItemGeometryCommand(text, item, before, after);
    emit statusChanged();
}

void ImageView::hostPushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                                          const ItemComponents::Placement &after, const QString &text)
{
    pushItemTransformUndo(item, before, after, text);
}
void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseRelease(event)) {
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}
void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_shell.handleKeyPress(event)) {
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

// --- Shell events (double-click / leave) ---

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseDoubleClick(event)) {
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    m_shell.handleLeave();
    QGraphicsView::leaveEvent(event);
}

// --- Drag / drop ---

bool ImageView::viewportEvent(QEvent *event)
{
    // Viewport is a QOpenGLWidget; it receives drag/drop when acceptDrops is
    // set on it. Forward to the view so scene mapping runs here.
    switch (event->type()) {
    case QEvent::DragEnter:
        dragEnterEvent(static_cast<QDragEnterEvent *>(event));
        return event->isAccepted();
    case QEvent::DragMove:
        dragMoveEvent(static_cast<QDragMoveEvent *>(event));
        return event->isAccepted();
    case QEvent::Drop:
        dropEvent(static_cast<QDropEvent *>(event));
        return event->isAccepted();
    default:
        break;
    }
    return QGraphicsView::viewportEvent(event);
}

void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    m_shell.dragEnterEvent(event);
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    m_shell.dragMoveEvent(event);
}

void ImageView::dropEvent(QDropEvent *event)
{
    m_shell.dropEvent(event);
}
