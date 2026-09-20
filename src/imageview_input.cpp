// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
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

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    if (!isImageMode()) {
        return EdgeZone::None;
    }
    // Tool modes own the canvas: no Up-to-Gallery / prev-next edge chrome
    // (same as crop). Esc or the toolbar toggle leaves the mode.
    if (m_cropCtrl.session().active() || m_attentionCtrl.session().active()) {
        return EdgeZone::None;
    }
    const EdgeNavPolicy::Zone z = EdgeNavPolicy::zoneAt(
        viewPos, width(), height(), m_sessionNav.isGalleryReturnAvailable(),
        m_sessionNav.isImageModeNavEnabled());
    switch (z) {
    case EdgeNavPolicy::Zone::Previous:
        return EdgeZone::Previous;
    case EdgeNavPolicy::Zone::Next:
        return EdgeZone::Next;
    case EdgeNavPolicy::Zone::GalleryReturn:
        return EdgeZone::GalleryReturn;
    case EdgeNavPolicy::Zone::None:
    default:
        return EdgeZone::None;
    }
}
void ImageView::updateMouseInfo(const QPoint &viewPos)

{
    ImageMouseInfo info;
    const QPointF scenePos = mapToScene(viewPos);

    // Prefer the topmost item under the cursor
    ImageItem *hit = nullptr;
    const QList<QGraphicsItem *> hits = m_scene->items(scenePos);
    for (QGraphicsItem *gi : hits) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = item;
            break;
        }
    }

    if (hit) {
        const QPoint pixel = hit->pixelAtScenePos(scenePos);
        if (pixel.x() >= 0) {
            info.valid = true;
            info.imagePos = pixel;
            info.pixelColor = hit->colorAtPixel(pixel);
            info.path = hit->path();
        }
    }

    if (m_chrome.setMouseInfo(info)) {
        emit mouseInfoChanged(m_chrome.currentMouseInfo());
    }
}
void ImageView::wheelZoomViewAboutCursor(QWheelEvent *event)
{
    const qreal factor = ViewTransform::wheelZoomFactor(event->angleDelta().y());
    // Image mode and free-form Workspace: zoom the view about the cursor.
    // Do not touch selected-item geometry here — prepareGeometryChange on
    // handle pads was expanding AABBs and fighting the user's pan/zoom.
    m_slideshow.cancelSlideshowMotion();
    releaseStickyZoom();
    m_framing.releaseFit();
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    // Soft / PreferCache / tile LOD: coalesce continuous wheel notches.
    // Per-notch climb+tick was heavy on the GUI thread (set_viewport, cancel,
    // issue_requests). Paint uses the last plan + soft until the debounce fires.
    scheduleTileLodAfterInteraction(50);
    viewport()->update(); // refresh viewport-space chrome at the new scale
    emit statusChanged();
    event->accept();
}
void ImageView::wheelEvent(QWheelEvent *event)
{
    if (tryWheelGalleryZoom(event) || tryWheelGalleryScroll(event)) {
        return;
    }
    wheelZoomViewAboutCursor(event);
}
void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_layoutApply.active()) {
        return;
    }
    if (isImageMode() && !m_slideshow.hud().isProgressActive()) {
        maybeClimbImageModePixelsForView();
    } else if (isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    }
    // Gallery: never repack from resize (session delete looked like auto-layout).
    // Still refresh the decode window: open often packs at 0×0, soft arrives
    // into ImageCache, and without this pulse cells stay blank until F5/relayout.
    if (isGalleryMode()) {
        if (viewport() && viewport()->width() > 1 && viewport()->height() > 1) {
            scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowRearmMs);
        }
        return;
    }
    // Dwell cover owns framing — never refit the underlay over it.
    // Invalidate atlas viewport keys so the next tick rebuilds at new size.
    if (m_slideshow.dwell().isMotionActive()) {
        m_slideshow.dwell().invalidateAtlasViewport();
        m_slideshow.zoomBlur().clearUnderlays();
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    if (m_framing.isFitMode() && m_items.size() == 1) {
        fitItem(m_items.first(), currentFitAspectMode());
    }
}
bool ImageView::tryMousePressZoomRegion(QMouseEvent *event)
{
    if (!(m_zoomRegion.isArmed() || (isWorkspaceMode() && m_tool == Tool::Zoom))
        || event->button() != Qt::LeftButton) {
        return false;
    }
    m_zoomRegion.tryBeginPress(event->pos(), viewport());
    event->accept();
    return true;
}
bool ImageView::tryMousePressImageLink(QMouseEvent *event)
{
    if (!isImageMode() || m_cropCtrl.session().active() || m_attentionCtrl.session().active()
        || event->button() != Qt::LeftButton
        || event->modifiers() != Qt::NoModifier
        || !PagePath::isPageRef(classicPath())) {
        return false;
    }
    if (!m_textLayer.hasLayerRegions() || m_textLayer.layerPathRef() != classicPath()) {
        const bool hadShow = m_textLayer.showsRegions();
        m_textLayer.setShowRegions(true);
        refreshTextLayer();
        m_textLayer.setShowRegions(hadShow);
    }
    int page = 0;
    QString uri;
    if (!hitTextLinkAt(event->pos(), &page, &uri)) {
        return false;
    }
    emit linkActivated(page, uri);
    event->accept();
    return true;
}
bool ImageView::tryMousePressPan(QMouseEvent *event)
{
    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (!m_slideshow.dwell().isMotionActive()
        && (event->button() == Qt::MiddleButton
            || (event->button() == Qt::LeftButton
                && ((isImageMode() && m_chrome.isImageModeLeftDragPan())
                    || (isWorkspaceMode() && m_tool == Tool::Pan)
                    || (isGalleryMode() && (event->modifiers() & Qt::AltModifier))
                    || (event->modifiers() & Qt::AltModifier))))) {
        if (!(isWorkspaceMode() && (event->modifiers() & Qt::ShiftModifier)
              && event->button() == Qt::LeftButton)) {
            m_chrome.beginPan(event->pos());
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return true;
        }
    }
    if (event->button() == Qt::MiddleButton && !m_slideshow.dwell().isMotionActive()) {
        m_chrome.beginPan(event->pos());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return true;
    }
    return false;
}
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
    tickPrimaryTileLod(4);
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
    if (!item || !m_undoStack) {
        return;
    }
    if (after.pos == before.pos
        && qFuzzyCompare(after.scale, before.scale)
        && qFuzzyCompare(after.scaleY > 0 ? after.scaleY : 1.0,
                         before.scaleY > 0 ? before.scaleY : 1.0)
        && qFuzzyCompare(after.shear + 1.0, before.shear + 1.0)
        && qFuzzyCompare(after.rotation, before.rotation)
        && after.opacity == before.opacity) {
        return;
    }
    class TransformCommand : public QUndoCommand {
    public:
        TransformCommand(ImageView *view, ImageItem *item,
                         const WorkspaceItemState &before,
                         const WorkspaceItemState &after,
                         const QString &text)
            : m_view(view), m_item(item), m_before(before), m_after(after)
        {
            setText(text);
        }
        void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
        void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
    private:
        ImageView *m_view;
        ImageItem *m_item;
        WorkspaceItemState m_before, m_after;
    };
    m_undoStack->push(new TransformCommand(this, item, before, after, text));
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
    tickPrimaryTileLod(8);
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
