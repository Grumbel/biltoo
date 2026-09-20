// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "itemcomponents.h"
#include "gallerysoftsm.h"
#include "toolpolicy.h"
#include "workspacenavgeometry.h"
#include "grouptransformgeometry.h"
#include "selectiongeometry.h"
#include "viewtransform.h"
#include "placementlinear.h"
#include "attentiongeometry.h"
#include "edgenavpolicy.h"
#include "pagepath.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"

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
#include "thumtoocache.h"

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (tryMousePressSlideshowSeek(event)
        || tryMousePressAttention(event)
        || tryMousePressCrop(event)
        || tryMousePressZoomRegion(event)
        || tryMousePressWorkspaceChrome(event)
        || tryMousePressImageLink(event)
        || tryMousePressTextRubber(event)
        || tryMousePressImageEdges(event)
        || tryMousePressPan(event)
        || tryMousePressWorkspaceRotate(event)
        || tryMousePressGalleryRight(event)
        || tryMousePressGalleryLeft(event)
        || tryMousePressWorkspaceSelect(event)) {
        return;
    }

    QGraphicsView::mousePressEvent(event);
}
void ImageView::updateMouseMoveLinkHover(QMouseEvent *event)
{
    // Link hover: pointing hand + status tip (Image mode page docs).
    if (isImageMode() && !m_cropCtrl.session().active() && !m_attentionCtrl.session().active() && !m_textLayer.isRubberbanding()
        && !m_chrome.isPanning() && event->buttons() == Qt::NoButton
        && PagePath::isPageRef(classicPath())) {
        if (!m_textLayer.hasLayerRegions() || m_textLayer.layerPathRef() != classicPath()) {
            const ThumtooCache::PageTextLayer cached =
                ThumtooCache::cachedPageTextLayer(classicPath());
            if (!cached.regions.isEmpty()) {
                m_textLayer.setLayerContent(cached, classicPath());
            }
        }
        int page = 0;
        QString uri;
        QString tip;
        if (hitTextLinkAt(event->pos(), &page, &uri)) {
            setCursor(Qt::PointingHandCursor);
            if (page > 0) {
                tip = tr("Link → page %1").arg(page);
            }
            if (!uri.isEmpty()) {
                tip = tip.isEmpty() ? uri : (tip + QStringLiteral(" · ") + uri);
            }
            if (tip.isEmpty()) {
                tip = tr("Link");
            }
        } else if (m_hoverEdge == EdgeZone::None) {
            setCursor(m_chrome.isImageModeLeftDragPan() ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
        if (m_textLayer.setLinkHoverTip(tip)) {
            emit statusChanged();
        }
    } else if (m_textLayer.hasLinkHoverTip() && event->buttons() == Qt::NoButton) {
        m_textLayer.clearLinkHoverTip();
        emit statusChanged();
    }
}
bool ImageView::tryMouseMovePan(QMouseEvent *event)
{
    if (!m_chrome.isPanning()) {
        return false;
    }
    // Dwell camera owns the view transform — do not fight it with hand pan.
    if (m_slideshow.dwell().isMotionActive()) {
        m_chrome.endPan();
        event->accept();
        return true;
    }
    const QPoint delta = m_chrome.panDeltaFrom(event->pos());
    m_chrome.updatePanPos(event->pos());
    // Grow the free-form sceneRect with the view so middle-drag is never
    // clamped against a stale zero-range scrollbar.
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    // Tile LOD timer stops once the viewport is covered. Panning changes the
    // visible set without a zoom/climb event — keep issuing requests.
    m_displayPipeline.tickPrimaryTileLod(4);
    event->accept();
    return true;
}
bool ImageView::tryMouseMoveZoomRegion(QMouseEvent *event)
{
    if (!m_zoomRegion.tryUpdateMove(event->pos())) {
        return false;
    }
    event->accept();
    return true;
}
void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (tryMouseMoveTextRubber(event)) {
        return;
    }
    updateMouseMoveLinkHover(event);
    if (tryMouseMoveAttention(event)
        || tryMouseMoveCropDrag(event)
        || tryMouseMovePan(event)
        || tryMouseMoveCropHover(event)
        || tryMouseMoveZoomRegion(event)) {
        return;
    }
    updateMouseInfo(event->pos());
    if (tryMouseMovePageGuide(event)
        || tryMouseMoveGroupAndHandleDrag(event)
        || tryMouseMoveWorkspaceRotate(event)) {
        return;
    }

    if (isImageMode()) {
        updateHoverEdge(event->pos());
    }

    m_chrome.setHoverViewPos(event->pos());
    updateMouseMoveSlideshowSeek(event);
    updateGalleryHoverAt(m_chrome.hoverViewPos());
    updateMouseMoveWorkspaceChromeHover(event);

    QGraphicsView::mouseMoveEvent(event);
}
void ImageView::restoreToolCursor()
{
    setCursor(ToolPolicy::cursorFor(m_tool));
}
void ImageView::pushItemTransformUndo(ImageItem *item, const WorkspaceItemState &before,
                                      const WorkspaceItemState &after, const QString &text)
{
    if (!item) {
        return;
    }
    if (ItemComponents::placementNearlyEqual(
            ItemComponents::placementFromState(before),
            ItemComponents::placementFromState(after))) {
        return;
    }
    // Single geometry undo path (persist + TransformCommand).
    pushItemGeometryCommand(text, item, before, after);
    emit statusChanged();
}
bool ImageView::tryMouseReleaseZoomRegion(QMouseEvent *event)
{
    if (!m_zoomRegion.isDragging()) {
        return false;
    }
    QRect viewRect;
    // Significant rubber → fit; tiny click cancels without zooming.
    if (m_zoomRegion.tryEndRelease(event->pos(), &viewRect)) {
        const QRectF sceneRect = mapToScene(viewRect).boundingRect();
        if (sceneRect.isValid() && !sceneRect.isEmpty()) {
            releaseStickyZoom();
            m_framing.clearFitFill();
            fitInView(sceneRect, Qt::KeepAspectRatio);
            emit statusChanged();
        }
    }
    cancelZoomRegion();
    event->accept();
    return true;
}
bool ImageView::tryMouseReleasePan(QMouseEvent *event)
{
    if (!m_chrome.isPanning()
        || (event->button() != Qt::MiddleButton && event->button() != Qt::LeftButton)) {
        return false;
    }
    m_chrome.endPan();
    restoreToolCursor();
    m_displayPipeline.tickPrimaryTileLod(8);
    event->accept();
    return true;
}
bool ImageView::tryMouseReleaseItemDrag(QMouseEvent *event)
{
    if (!m_itemInteract.currentDragItem() || event->button() != Qt::LeftButton) {
        return false;
    }
    pushItemTransformUndo(m_itemInteract.currentDragItem(), m_itemInteract.currentDragStartState(), captureState(m_itemInteract.currentDragItem()), tr("Move"));
    m_itemInteract.endMove();
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    return false; // fall through to base class
}
void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (tryMouseReleaseSlideshowSeek(event)
        || tryMouseReleaseTextRubber(event)
        || tryMouseReleaseAttention(event)
        || tryMouseReleaseCrop(event)
        || tryMouseReleaseZoomRegion(event)
        || tryMouseReleasePageGuide(event)
        || tryMouseReleaseGroupDrag(event)
        || tryMouseReleaseHandleDrag(event)
        || tryMouseReleaseWorkspaceRotate(event)
        || tryMouseReleasePan(event)) {
        return;
    }
    tryMouseReleaseItemDrag(event);
    QGraphicsView::mouseReleaseEvent(event);
}
bool ImageView::tryKeyPressZoomRegion(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Escape || !(m_zoomRegion.isActive())) {
        return false;
    }
    cancelZoomRegion();
    event->accept();
    return true;
}
bool ImageView::tryKeyPressSelectAll(QKeyEvent *event)
{
    // Gallery / Workspace: Ctrl+A selects every live tile (standard multi-select).
    if (!(isGalleryMode() || isWorkspaceMode())
        || event->key() != Qt::Key_A
        || !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
        || (event->modifiers() & (Qt::ShiftModifier | Qt::AltModifier))) {
        return false;
    }
    selectAllCanvasItems();
    event->accept();
    return true;
}

void ImageView::emitGalleryItemFocus(ImageItem *item)
{
    if (!item) {
        return;
    }
    if (item->sessionId() != kInvalidSessionImageId) {
        emit sessionImageFocused(item->sessionId());
    } else if (!item->path().isEmpty()) {
        emit galleryItemFocused(item->path());
    }
}


void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (tryKeyPressAttention(event)
        || tryKeyPressCrop(event)
        || tryKeyPressZoomRegion(event)
        || tryKeyPressSelectAll(event)
        || tryKeyPressImageNavigate(event)
        || tryKeyPressGallery(event)
        || tryKeyPressWorkspaceShear(event)
        || tryKeyPressDeleteSelection(event)) {
        return;
    }
    QGraphicsView::keyPressEvent(event);
}
