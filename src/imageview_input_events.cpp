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
    if (m_slideshow.tryMousePressSlideshowSeek(event)
        || m_attentionCtrl.tryMousePressAttention(event)
        || m_cropCtrl.tryMousePressCrop(event)
        || tryMousePressZoomRegion(event)
        || m_workspace.tryMousePressWorkspaceChrome(event)
        || tryMousePressImageLink(event)
        || tryMousePressTextRubber(event)
        || m_image.tryMousePressEdges(event)
        || m_shell.tryMousePressPan(event)
        || m_workspace.tryMousePressWorkspaceRotate(event)
        || m_gallery.tryMousePressGalleryRight(event)
        || m_gallery.tryMousePressGalleryLeft(event)
        || m_workspace.tryMousePressSelect(event)) {
        return;
    }

    QGraphicsView::mousePressEvent(event);
}
bool ImageView::tryMouseMoveZoomRegion(QMouseEvent *event)
{
    return m_image.tryMouseMoveZoomRegion(event);
}
void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (tryMouseMoveTextRubber(event)) {
        return;
    }
    m_textCtrl.updateMouseMoveLinkHover(event);
    if (m_attentionCtrl.tryMouseMoveAttention(event)
        || m_cropCtrl.tryMouseMoveCropDrag(event)
        || m_shell.tryMouseMovePan(event)
        || m_cropCtrl.tryMouseMoveCropHover(event)
        || tryMouseMoveZoomRegion(event)
        || m_gallery.tryMouseMoveGalleryDrag(event)) {
        return;
    }
    updateMouseInfo(event->pos());
    if (tryMouseMovePageGuide(event)
        || tryMouseMoveGroupAndHandleDrag(event)
        || m_workspace.tryMouseMoveWorkspaceRotate(event)) {
        return;
    }

    if (isImageMode()) {
        updateHoverEdge(event->pos());
    }

    m_shell.viewport().setHoverViewPos(event->pos());
    m_slideshow.updateMouseMoveSlideshowSeek(event);
    m_gallery.updateGalleryHoverAt(m_shell.viewport().hoverViewPos());
    m_workspace.updateMouseMoveWorkspaceChromeHover(event);

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
bool ImageView::tryMouseReleaseZoomRegion(QMouseEvent *event)
{
    return m_image.tryMouseReleaseZoomRegion(event);
}


bool ImageView::tryMouseReleaseItemDrag(QMouseEvent *event)
{
    return m_workspace.tryMouseReleaseItemDrag(event);
}
void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_slideshow.tryMouseReleaseSlideshowSeek(event)
        || tryMouseReleaseTextRubber(event)
        || m_attentionCtrl.tryMouseReleaseAttention(event)
        || m_cropCtrl.tryMouseReleaseCrop(event)
        || tryMouseReleaseZoomRegion(event)
        || tryMouseReleasePageGuide(event)
        || tryMouseReleaseGroupDrag(event)
        || tryMouseReleaseHandleDrag(event)
        || m_workspace.tryMouseReleaseWorkspaceRotate(event)
        || m_shell.tryMouseReleasePan(event)) {
        return;
    }
    m_gallery.clearGalleryDragArm();
    tryMouseReleaseItemDrag(event);
    QGraphicsView::mouseReleaseEvent(event);
}
bool ImageView::tryKeyPressZoomRegion(QKeyEvent *event)
{
    return m_image.tryKeyPressZoomRegion(event);
}




void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_attentionCtrl.tryKeyPressAttention(event)
        || m_cropCtrl.tryKeyPressCrop(event)
        || tryKeyPressZoomRegion(event)
        || m_workspace.tryKeyPressSelectAll(event)
        || m_image.tryKeyPressNavigate(event)
        || m_gallery.tryKeyPressGallery(event)
        || m_workspace.tryKeyPressShear(event)
        || m_gallery.tryKeyPressDeleteSelection(event)
        || m_workspace.tryKeyPressDeleteSelection(event)) {
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

// --- Shell events (double-click / leave) ---

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_image.tryMouseDoubleClick(event)
        || m_gallery.tryMouseDoubleClick(event)
        || m_workspace.tryMouseDoubleClick(event)) {
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    if (m_shell.viewport().hasMouseInfo()) {
        m_shell.viewport().clearMouseInfo();
        emit mouseInfoChanged(m_shell.viewport().currentMouseInfo());
    }
    if (hostHoverEdge() != EdgeZone::None) {
        clearHoverEdge();
        viewport()->update();
    }
    if (!m_gallery.hoverPath().isEmpty()) {
        m_gallery.clearHoverPath();
        viewport()->update();
    }
    if (m_slideshow.hud().isSeekbarVisible() && !m_slideshow.hud().isSeekDragging()) {
        m_slideshow.hud().setSeekbarVisible(false);
        if (viewport()) {
            viewport()->update();
        }
    }
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
    if (event->mimeData()
        && (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat(QStringLiteral("application/x-biltoo-paths")))) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()
        && (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat(QStringLiteral("application/x-biltoo-paths")))) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()) {
        event->ignore();
        return;
    }
    const QByteArray pathBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-paths"));
    const bool hasInternal = !pathBytes.isEmpty();
    if (!event->mimeData()->hasUrls() && !hasInternal) {
        event->ignore();
        return;
    }
    // Prefer global→viewport→scene. Drop events may land on the view or the
    // OpenGL viewport child; widget-local position() is then wrong for mapToScene.
    // QDropEvent has no portable globalPosition() here — use the cursor.
    const QPoint viewPos = viewport()->mapFromGlobal(QCursor::pos());
    const QPointF scenePos = mapToScene(viewPos);
    QList<qint64> sessionIds;
    const QByteArray idBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-session-ids"));
    if (!idBytes.isEmpty()) {
        for (const QByteArray &tok : idBytes.split(',')) {
            bool ok = false;
            const qint64 v = tok.trimmed().toLongLong(&ok);
            sessionIds.append(ok ? v : 0);
        }
    }
    QStringList internalPaths;
    if (hasInternal) {
        internalPaths = QString::fromUtf8(pathBytes).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/drop: ImageView::dropEvent viewPos=(%d,%d) scene=(%.1f,%.1f) "
                "mode=W%d G%d\n",
                viewPos.x(), viewPos.y(), scenePos.x(), scenePos.y(),
                isWorkspaceMode() ? 1 : 0, isGalleryMode() ? 1 : 0);
    }
    emit filesDropped(event->mimeData()->urls(), event->modifiers(), scenePos,
                      /*hasScenePos=*/true, sessionIds, internalPaths);
    event->acceptProposedAction();
}
