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

int ImageView::edgeZoneWidth() const
{
    return EdgeNavPolicy::zoneWidth(width());
}

int ImageView::edgeZoneHeight() const
{
    return EdgeNavPolicy::zoneHeight(height());
}

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    if (!isImageMode()) {
        return EdgeZone::None;
    }
    // Tool modes own the canvas: no Up-to-Gallery / prev-next edge chrome
    // (same as crop). Esc or the toolbar toggle leaves the mode.
    if (m_crop.mode || m_attention.mode) {
        return EdgeZone::None;
    }
    const EdgeNavPolicy::Zone z = EdgeNavPolicy::zoneAt(
        viewPos, width(), height(), m_sessionNav.galleryReturnAvailable,
        m_sessionNav.imageModeNavEnabled);
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

void ImageView::updateHoverEdge(const QPoint &viewPos)
{
    const EdgeZone zone = edgeZoneAt(viewPos);
    if (zone == m_hoverEdge) {
        return;
    }
    m_hoverEdge = zone;
    if (m_hoverEdge == EdgeZone::Previous || m_hoverEdge == EdgeZone::Next
        || m_hoverEdge == EdgeZone::GalleryReturn) {
        setCursor(Qt::PointingHandCursor);
    } else if (!m_chrome.panning && !m_itemInteract.rotating) {
        setCursor(ToolPolicy::cursorFor(m_tool));
    }
    viewport()->update();
}




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





QRectF ImageView::selectionSceneBounds(const QList<ImageItem *> &items) const
{
    QVector<QRectF> rects;
    rects.reserve(items.size());
    for (ImageItem *item : items) {
        if (item) {
            rects.append(item->contentSceneRect());
        }
    }
    return SelectionGeometry::unionContentAabbs(rects);
}







void ImageView::updateGalleryHoverAt(const QPoint &viewPos)
{
    if (!isGalleryMode() || !m_scene) {
        if (!m_gallery.hoverPath().isEmpty()) {
            m_gallery.clearHoverPath();
            viewport()->update();
        }
        return;
    }
    QString path;
    const QPointF scenePos = mapToScene(viewPos);
    for (QGraphicsItem *gi : m_scene->items(scenePos)) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            path = ii->path();
            break;
        }
    }
    if (path != m_gallery.hoverPath()) {
        m_gallery.setHoverPath(path);
        viewport()->update();
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

    if (info.valid != m_chrome.mouseInfo.valid
        || info.imagePos != m_chrome.mouseInfo.imagePos
        || info.pixelColor != m_chrome.mouseInfo.pixelColor
        || info.path != m_chrome.mouseInfo.path) {
        m_chrome.setMouseInfo(info);
        emit mouseInfoChanged(m_chrome.mouseInfo);
    }
}

bool ImageView::tryWheelGalleryZoom(QWheelEvent *event)
{
    // Gallery: Ctrl+wheel zooms the view (inspection) and refreshes the soft
    // ladder for the new on-screen cell size.
    if (!isGalleryMode() || !(event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    const qreal factor = ViewTransform::wheelZoomFactor(event->angleDelta().y());
    releaseStickyZoom();
    m_framing.fitMode = false;
    m_framing.fillMode = false;
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Do not run updateGalleryDecodeWindow or FullViewportUpdate here —
    // each wheel notch used to rescan all tiles + setInterest + repaint
    // every high-res soft, freezing the UI while zooming out.
    scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowScrollMs);
    refreshStatus();
    event->accept();
    return true;
}

bool ImageView::tryWheelGalleryScroll(QWheelEvent *event)
{
    if (!isGalleryMode()) {
        return false;
    }
    QScrollBar *hBar = horizontalScrollBar();
    QScrollBar *vBar = verticalScrollBar();
    const bool canH = hBar && hBar->maximum() > hBar->minimum();
    const bool canV = vBar && vBar->maximum() > vBar->minimum();

    int dx = 0;
    int dy = 0;
    if (!event->pixelDelta().isNull()) {
        dx = event->pixelDelta().x();
        dy = event->pixelDelta().y();
    } else {
        // angleDelta is in eighths of a degree; 120 ≈ one notch.
        dx = event->angleDelta().x();
        dy = event->angleDelta().y();
    }

    // Shift+wheel → prefer horizontal (common UI convention).
    if (event->modifiers() & Qt::ShiftModifier) {
        if (dx == 0 && dy != 0) {
            dx = dy;
            dy = 0;
        }
    }

    // Horizontal strip layouts: vertical wheel pans sideways.
    const bool preferHorizontalScroll =
        m_layout.mode == LayoutMode::SideBySide
        || m_layout.mode == LayoutMode::MasonryRows
        || m_layout.mode == LayoutMode::MasonryRowsFill;

    if (preferHorizontalScroll && dx == 0 && dy != 0) {
        dx = dy;
        dy = 0;
    } else if (dx == 0 && dy != 0 && !canV && canH) {
        dx = dy;
        dy = 0;
    } else if (dy == 0 && dx != 0 && !canH && canV) {
        dy = dx;
        dx = 0;
    }

    if (canH && dx != 0) {
        hBar->setValue(hBar->value() - dx);
    }
    if (canV && dy != 0) {
        vBar->setValue(vBar->value() - dy);
    }
    // Accept even at scroll ends so the event does not fall through to zoom.
    event->accept();
    return true;
}

void ImageView::wheelZoomViewAboutCursor(QWheelEvent *event)
{
    const qreal factor = ViewTransform::wheelZoomFactor(event->angleDelta().y());
    // Image mode and free-form Workspace: zoom the view about the cursor.
    // Do not touch selected-item geometry here — prepareGeometryChange on
    // handle pads was expanding AABBs and fighting the user's pan/zoom.
    cancelSlideshowMotion();
    releaseStickyZoom();
    m_framing.fitMode = false;
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
    if (isImageMode() && !m_ssHud.progressActive) {
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
    if (m_ssDwell.motionActive) {
        m_ssDwell.atlasVw = 0;
        m_ssZoomBlur.underlay[0] = QPixmap();
        m_ssZoomBlur.underlay[1] = QPixmap();
        m_ssZoomBlur.sourceKey[0] = 0;
        m_ssZoomBlur.sourceKey[1] = 0;
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    if (m_framing.fitMode && m_items.size() == 1) {
        fitItem(m_items.first(), currentFitAspectMode());
    }
}

bool ImageView::tryMousePressSlideshowSeek(QMouseEvent *event)
{
    // mpv-style seekbar: drag along bottom edge during slideshow.
    if (event->button() != Qt::LeftButton || !viewport()) {
        return false;
    }
    if (!m_ssHud.isSeekHit(event->pos().y(), viewport()->height())) {
        return false;
    }
    m_ssHud.seekDragging = true;
    m_ssHud.seekbarVisible = true;
    const qreal f = ViewTransform::unitFraction(event->pos().x(), viewport()->width());
    emit slideshowSeekRequested(f);
    event->accept();
    return true;
}

bool ImageView::tryMousePressAttention(QMouseEvent *event)
{
    if (!m_attention.mode || event->button() != Qt::LeftButton || !isImageMode()
        || edgeZoneAt(event->pos()) != EdgeZone::None) {
        return false;
    }
    ImageItem *item = targetItem();
    if (!item || item->contentRect().isEmpty()) {
        return false;
    }
    const int hit = attentionHandleIndexAt(event->pos());
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    // Handle hit: standard selection (Shift/Ctrl toggle; plain exclusive unless
    // already selected so multi-drag keeps the set).
    if (hit >= 0) {
        if (shift || ctrl) {
            m_attention.setSelected(
                AttentionGeometry::toggleSelectionIndex(m_attention.selected, hit));
        } else if (!m_attention.selected.contains(hit)) {
            m_attention.setSelected({hit});
        }
        const QVector<QPointF> startPts = attentionPointsForTarget();
        m_attention.beginPointDrag(event->pos(), startPts, startPts);
        viewport()->update();
        event->accept();
        return true;
    }
    // Ctrl+click empty content: insert a point (then drag to place).
    const QPointF scene = mapToScene(event->pos());
    const QPointF local = item->mapFromScene(scene);
    const QRectF cr = item->contentRect();
    if (ctrl && cr.contains(local)) {
        const QPointF n = AttentionGeometry::normFromLocal(local, cr);
        const QVector<QPointF> before = attentionPointsForTarget();
        QVector<QPointF> pts = before;
        pts.append(n);
        setAttentionPointsForTarget(pts);
        m_attention.setSelected({int(pts.size() - 1)});
        const QVector<QPointF> startPts = attentionPointsForTarget();
        m_attention.beginPointDrag(event->pos(), startPts, before);
        event->accept();
        return true;
    }
    // Plain / Shift click on empty: rubber-band select (additive with Shift).
    m_attention.beginRubber(event->pos(), !shift && !ctrl);
    viewport()->update();
    event->accept();
    return true;
}

bool ImageView::tryMousePressCrop(QMouseEvent *event)
{
    if (!m_crop.mode || event->button() != Qt::LeftButton) {
        return false;
    }
    const CropHandle h = cropHandleAt(event->pos());
    if (h == CropHandle::ExpandToggle) {
        m_crop.allowExpand = !m_crop.allowExpand;
        if (!m_crop.allowExpand) {
            ensureCropRectValid(); // clamp back into the image
        }
        viewport()->update();
        event->accept();
        return true;
    }
    if (h == CropHandle::Auto) {
        applyAutoCrop();
        event->accept();
        return true;
    }
    if (h == CropHandle::Reset) {
        // Expand draft to the full image; Apply commits a cleared session crop.
        if (ImageItem *item = cropTargetItem()) {
            m_crop.setRect(item->contentRect());
            m_crop.setRotation(0.0);
            ensureCropRectValid();
            viewport()->update();
        }
        event->accept();
        return true;
    }
    if (h == CropHandle::Cancel) {
        cancelCrop();
        event->accept();
        return true;
    }
    if (h == CropHandle::Close) {
        applyCrop();
        event->accept();
        return true;
    }
    if (h != CropHandle::None) {
        beginCropHandleDrag(h, event->pos());
        event->accept();
        return true;
    }
    // Middle/Alt still pan; plain left on the image body → new rubber-band crop.
    if (!(event->modifiers()
          & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier))) {
        beginCropRubberBand(event->pos());
        if (m_crop.rubberBanding) {
            event->accept();
            return true;
        }
    }
    return false;
}

bool ImageView::tryMousePressZoomRegion(QMouseEvent *event)
{
    if (!(m_zoomRegion.armed || (isWorkspaceMode() && m_tool == Tool::Zoom))
        || event->button() != Qt::LeftButton) {
        return false;
    }
    m_zoomRegion.beginDrag(event->pos());
    if (!m_zoomRegion.rubberBand) {
        m_zoomRegion.rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
    }
    m_zoomRegion.rubberBand->setGeometry(QRect(m_zoomRegion.origin, QSize()));
    m_zoomRegion.rubberBand->show();
    event->accept();
    return true;
}

bool ImageView::tryMousePressWorkspaceChrome(QMouseEvent *event)
{
    if (!isWorkspaceMode() || event->button() != Qt::LeftButton
        || m_tool != Tool::Select) {
        return false;
    }
    // Workspace chrome hit-testing is view-owned (DOMAIN: free-object transforms).
    // Chrome is painted above all tiles in viewport space; hit-testing must
    // similarly ignore scene z-order of other images under the pointer.
    const QPointF scenePos = mapToScene(event->pos());
    QList<ImageItem *> selected;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (ii->isInteractive() && m_items.contains(ii)) {
                selected.append(ii);
            }
        }
    }
    if (selected.size() > 1) {
        // Multi-select: group frame only (no per-item handles).
        const int gh = groupHandleAt(event->pos(), selected);
        if (gh >= 0 && beginGroupScale(gh, selected)) {
            m_groupXform.setPressScenePos(mapToScene(event->pos()));
            event->accept();
            return true;
        }
    } else if (selected.size() == 1) {
        // Single selection: test that item's handles first, even when the
        // pointer is over another tile's pixmap (handles are drawn on top).
        ImageItem *item = selected.first();
        if (item->beginHandleInteraction(scenePos, event->modifiers())) {
            m_itemInteract.beginHandleDrag(item, captureState(item));
            setPageGuideSelected(false);
            event->accept();
            return true;
        }
    }
    // Page guide scale grips when the guide is selected.
    if (m_pageGuide.visible && m_pageGuide.selected) {
        const int ph = pageGuideHandleAt(event->pos());
        if (ph >= 0 && beginPageGuideResize(ph)) {
            event->accept();
            return true;
        }
    }
    // No handle hit — fall through to move/select / clear.
    return false;
}

bool ImageView::tryMousePressImageLink(QMouseEvent *event)
{
    if (!isImageMode() || m_crop.mode || m_attention.mode
        || event->button() != Qt::LeftButton
        || event->modifiers() != Qt::NoModifier
        || !PagePath::isPageRef(classicPath())) {
        return false;
    }
    if (m_textLayer.layer.regions.isEmpty() || m_textLayer.layerPath != classicPath()) {
        const bool hadShow = m_textLayer.showRegions;
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

bool ImageView::tryMousePressTextRubber(QMouseEvent *event)
{
    if (!isImageMode() || m_crop.mode || m_attention.mode
        || event->button() != Qt::LeftButton
        || !(event->modifiers() & Qt::ShiftModifier)
        || (event->modifiers() & (Qt::AltModifier | Qt::ControlModifier))
        || !PagePath::isPageRef(classicPath())) {
        return false;
    }
    m_textLayer.beginRubber(event->pos());
    m_textLayer.selectedRegions.clear();
    setCursor(Qt::CrossCursor);
    viewport()->update();
    event->accept();
    return true;
}

bool ImageView::tryMousePressImageEdges(QMouseEvent *event)
{
    if (!isImageMode() || event->button() != Qt::LeftButton
        || (event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        return false;
    }
    const EdgeZone zone = edgeZoneAt(event->pos());
    if (zone == EdgeZone::GalleryReturn) {
        emit galleryReturnRequested();
        event->accept();
        return true;
    }
    if (zone == EdgeZone::Previous) {
        emit navigatePreviousRequested();
        event->accept();
        return true;
    }
    if (zone == EdgeZone::Next) {
        emit navigateNextRequested();
        event->accept();
        return true;
    }
    // Slideshow: centre click pauses / resumes. Edges stay navigation above.
    // Ignore the second press of a double-click so we do not toggle twice.
    if ((m_ssHud.progressActive || m_ssHud.pausedHud)
        && zone == EdgeZone::None) {
        if (m_lastSlideshowCenterClick.isValid()
            && m_lastSlideshowCenterClick.elapsed()
                < QApplication::doubleClickInterval()) {
            event->accept();
            return true;
        }
        m_lastSlideshowCenterClick.start();
        emit slideshowTogglePauseRequested();
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryMousePressPan(QMouseEvent *event)
{
    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (!m_ssDwell.motionActive
        && (event->button() == Qt::MiddleButton
            || (event->button() == Qt::LeftButton
                && ((isImageMode() && m_chrome.imageModeLeftDragPan)
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
    if (event->button() == Qt::MiddleButton && !m_ssDwell.motionActive) {
        m_chrome.beginPan(event->pos());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryMousePressWorkspaceRotate(QMouseEvent *event)
{
    // Workspace only: Shift + left button free-rotates (unless the press is on
    // a selected item's scale/chrome handle — those use Shift for opposite-edge scale).
    if (!isWorkspaceMode() || event->button() != Qt::LeftButton
        || !(event->modifiers() & Qt::ShiftModifier)) {
        return false;
    }
    ImageItem *hit = nullptr;
    const QPointF scenePos = mapToScene(event->pos());
    for (QGraphicsItem *gi : m_scene->items(scenePos)) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = ii;
            break;
        }
    }
    if (!hit) {
        hit = targetItem();
    }
    if (hit && hit->isSelected() && hit->hasHandleAt(hit->mapFromScene(scenePos))) {
        // Fall through to QGraphicsView → ImageItem handle interaction.
        return false;
    }
    if (!hit) {
        return false;
    }
    m_itemInteract.beginRotate(hit, angleAt(scenePos, hit), hit->itemRotation(),
                               captureState(hit));
    m_scene->clearSelection();
    hit->setSelected(true);
    setCursor(Qt::CrossCursor);
    event->accept();
    return true;
}

bool ImageView::tryMousePressGalleryRight(QMouseEvent *event)
{
    // Gallery right-click: do not let QGraphicsView alter selection (that
    // cancels multi-select before the context menu opens). If the click is on
    // an unselected tile, select only that tile; if it is already selected,
    // keep the current multi-select for bulk rotate/flip/delete.
    if (!isGalleryMode() || event->button() != Qt::RightButton) {
        return false;
    }
    const QPointF scenePos = mapToScene(event->pos());
    ImageItem *hit = nullptr;
    for (QGraphicsItem *gi : m_scene->items(scenePos)) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = ii;
            break;
        }
    }
    if (hit && !hit->isSelected()) {
        m_scene->clearSelection();
        hit->setSelected(true);
        m_gallery.setSelectionAnchor(hit);
        if (hit->sessionId() != kInvalidSessionImageId) {
            emit sessionImageFocused(hit->sessionId());
        } else if (!hit->path().isEmpty()) {
            emit galleryItemFocused(hit->path());
        }
        emit statusChanged();
    }
    event->accept();
    return true;
}

bool ImageView::tryMousePressGalleryLeft(QMouseEvent *event)
{
    // Gallery: classic multi-select (click / Ctrl / Shift); open is double-click.
    if (!isGalleryMode() || event->button() != Qt::LeftButton
        || (event->modifiers() & Qt::AltModifier)) {
        return false;
    }
        const QPointF scenePos = mapToScene(event->pos());
        ImageItem *hit = nullptr;
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = ii;
                break;
            }
        }
        // Ctrl (and Meta on platforms where that is the multi-select modifier)
        // toggles membership without clearing the rest of the selection.
        const bool ctrl = event->modifiers()
                          & (Qt::ControlModifier | Qt::MetaModifier);
        const bool shift = event->modifiers() & Qt::ShiftModifier;

        if (hit && shift && m_gallery.selectionAnchor()) {
            // Session-order range from anchor to hit (inclusive).
            int i0 = m_items.indexOf(m_gallery.selectionAnchor());
            int i1 = m_items.indexOf(hit);
            if (i0 < 0) {
                i0 = i1;
            }
            if (i1 < 0) {
                i1 = i0;
            }
            if (i0 > i1) {
                std::swap(i0, i1);
            }
            m_scene->blockSignals(true);
            m_scene->clearSelection();
            for (int i = i0; i <= i1 && i < m_items.size(); ++i) {
                m_items.at(i)->setSelected(true);
            }
            m_scene->blockSignals(false);
            // Selection overlay only — no item cache rebuild.
            if (viewport()) {
                viewport()->update();
            }
            emit canvasSelectionChanged();
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit galleryItemFocused(hit->path());
            }
            event->accept();
            if (m_hudPrefs.visible || m_hudFlash.visible) {
                emit statusChanged();
            }
            return true;
        }

        if (hit && ctrl) {
            // Defensive: soft/late tiles may have lost ItemIsSelectable.
            if (!(hit->flags() & QGraphicsItem::ItemIsSelectable)) {
                hit->setGallerySelectable(true);
            }
            hit->setSelected(!hit->isSelected());
            if (hit->isSelected()) {
                m_gallery.setSelectionAnchor(hit);
            }
            hit->invalidateDeviceCache();
            emit canvasSelectionChanged();
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit galleryItemFocused(hit->path());
            }
            event->accept();
            if (m_hudPrefs.visible || m_hudFlash.visible) {
                emit statusChanged();
            }
            return true;
        }

        if (hit) {
            // One selectionChanged: clear+select under blocked signals so
            // MainWindow does not run navigation/layout work twice per click.
            if (!(hit->flags() & QGraphicsItem::ItemIsSelectable)) {
                hit->setGallerySelectable(true);
            }
            const bool already = hit->isSelected()
                && m_scene->selectedItems().size() == 1;
            if (already) {
                m_gallery.setSelectionAnchor(hit);
                event->accept();
                return true;
            }
            m_scene->blockSignals(true);
            m_scene->clearSelection();
            hit->setSelected(true);
            m_scene->blockSignals(false);
            // Selection overlay in drawForeground; viewport update is enough.
            if (viewport()) {
                viewport()->update();
            }
            emit canvasSelectionChanged();
            m_gallery.setSelectionAnchor(hit);
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit galleryItemFocused(hit->path());
            }
            event->accept();
            return true;
        }

        // Empty space: clear selection (keep Ctrl-additive empty no-ops).
        if (!ctrl) {
            m_scene->clearSelection();
            emit canvasSelectionChanged();
            emit statusChanged();
        }
        // Allow rubber-band start via base class when drag mode is RubberBandDrag.
        QGraphicsView::mousePressEvent(event);
        return true;
    return true;
}

bool ImageView::tryMousePressWorkspaceSelect(QMouseEvent *event)
{
    // Workspace Select tool: item move/select, or click the page guide sheet.
    if (!isWorkspaceMode() || event->button() != Qt::LeftButton) {
        return false;
    }
        const QPointF scenePos = mapToScene(event->pos());
        ImageItem *itemHit = nullptr;
        if (m_scene) {
            for (QGraphicsItem *gi : m_scene->items(scenePos)) {
                if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                    if (ii->isInteractive() && m_items.contains(ii)) {
                        itemHit = ii;
                        break;
                    }
                }
            }
        }
        if (!itemHit && m_pageGuide.visible
            && pageGuideSceneRect().contains(scenePos)) {
            if (m_scene) {
                m_scene->clearSelection();
            }
            setPageGuideSelected(true);
            event->accept();
            emit statusChanged();
            return true;
        }
        setPageGuideSelected(false);
        QGraphicsView::mousePressEvent(event);
        if (ImageItem *hit = targetItem()) {
            m_itemInteract.beginMove(hit, captureState(hit));
        }
        emit statusChanged();
        return true;
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

bool ImageView::tryMouseMoveTextRubber(QMouseEvent *event)
{
    if (!m_textLayer.rubberbanding || !(event->buttons() & Qt::LeftButton)) {
        return false;
    }
    m_textLayer.updateRubber(event->pos());
    viewport()->update();
    event->accept();
    return true;
}

void ImageView::updateMouseMoveLinkHover(QMouseEvent *event)
{
    // Link hover: pointing hand + status tip (Image mode page docs).
    if (isImageMode() && !m_crop.mode && !m_attention.mode && !m_textLayer.rubberbanding
        && !m_chrome.panning && event->buttons() == Qt::NoButton
        && PagePath::isPageRef(classicPath())) {
        if (m_textLayer.layer.regions.isEmpty() || m_textLayer.layerPath != classicPath()) {
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
            setCursor(m_chrome.imageModeLeftDragPan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
        if (tip != m_textLayer.linkHoverTip) {
            m_textLayer.setLinkHoverTip(tip);
            emit statusChanged();
        }
    } else if (!m_textLayer.linkHoverTip.isEmpty() && event->buttons() == Qt::NoButton) {
        m_textLayer.clearLinkHoverTip();
        emit statusChanged();
    }
}

bool ImageView::tryMouseMoveAttention(QMouseEvent *event)
{
    if (!m_attention.mode || !isImageMode()) {
        return false;
    }
    if (m_attention.rubberbanding) {
        m_attention.updateRubber(event->pos());
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_attention.dragging) {
        ImageItem *item = targetItem();
        if (item && !item->contentRect().isEmpty()
            && !m_attention.selected.isEmpty()
            && m_attention.dragStartPts.size() == attentionPointsForTarget().size()) {
            // Translate selected points by view-delta mapped through content.
            const QPointF scene0 = mapToScene(m_attention.dragOriginView);
            const QPointF scene1 = mapToScene(event->pos());
            const QPointF local0 = item->mapFromScene(scene0);
            const QPointF local1 = item->mapFromScene(scene1);
            const QRectF cr = item->contentRect();
            const QPointF dNorm = AttentionGeometry::normDeltaFromLocalDelta(
                local1 - local0, cr);
            const QVector<QPointF> pts = AttentionGeometry::translateSelectedNorms(
                m_attention.dragStartPts, m_attention.selected, dNorm);
            setAttentionPointsForTarget(pts);
        }
        event->accept();
        return true;
    }
    viewport()->setCursor(attentionHandleAt(event->pos()) ? Qt::SizeAllCursor
                                                          : Qt::CrossCursor);
    return false;
}

bool ImageView::tryMouseMoveCropDrag(QMouseEvent *event)
{
    if (!m_crop.mode) {
        return false;
    }
    if (m_crop.activeHandle != CropHandle::None) {
        updateCropHandleDrag(event->pos());
        event->accept();
        return true;
    }
    if (m_crop.rubberBanding) {
        updateCropRubberBand(event->pos());
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryMouseMovePan(QMouseEvent *event)
{
    if (!m_chrome.panning) {
        return false;
    }
    // Dwell camera owns the view transform — do not fight it with hand pan.
    if (m_ssDwell.motionActive) {
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

bool ImageView::tryMouseMoveCropHover(QMouseEvent *event)
{
    if (!m_crop.mode) {
        return false;
    }
    const CropHandle h = cropHandleAt(event->pos());
    const bool cropHoverChanged = m_crop.setHoverHandle(h);
    if (cropHoverChanged) {
        viewport()->update();
    }
    switch (h) {
    case CropHandle::Move:
        viewport()->setCursor(Qt::SizeAllCursor);
        break;
    case CropHandle::Rotate:
        viewport()->setCursor(Qt::ClosedHandCursor);
        break;
    case CropHandle::Left:
    case CropHandle::Right:
        viewport()->setCursor(Qt::SizeHorCursor);
        break;
    case CropHandle::Top:
    case CropHandle::Bottom:
        viewport()->setCursor(Qt::SizeVerCursor);
        break;
    case CropHandle::TopLeft:
    case CropHandle::BottomRight:
        viewport()->setCursor(Qt::SizeFDiagCursor);
        break;
    case CropHandle::TopRight:
    case CropHandle::BottomLeft:
        viewport()->setCursor(Qt::SizeBDiagCursor);
        break;
    case CropHandle::ExpandToggle:
    case CropHandle::Auto:
    case CropHandle::Reset:
    case CropHandle::Cancel:
    case CropHandle::Close:
        viewport()->setCursor(Qt::PointingHandCursor);
        break;
    case CropHandle::None:
        viewport()->setCursor(Qt::CrossCursor);
        break;
    }
    if (cropHoverChanged) {
        QString tip;
        switch (h) {
        case CropHandle::Move:
            tip = tr("Move crop");
            break;
        case CropHandle::Rotate:
            tip = tr("Rotate crop");
            break;
        case CropHandle::Left:
        case CropHandle::Right:
        case CropHandle::Top:
        case CropHandle::Bottom:
        case CropHandle::TopLeft:
        case CropHandle::TopRight:
        case CropHandle::BottomLeft:
        case CropHandle::BottomRight:
            tip = tr("Resize crop");
            break;
        case CropHandle::Auto:
        case CropHandle::ExpandToggle:
            tip = tr("Allow crop outside image (pad on apply)");
            break;
        case CropHandle::Reset:
            tip = tr("Reset crop to full image");
            break;
        case CropHandle::Cancel:
            tip = tr("Cancel crop (Esc)");
            break;
        case CropHandle::Close:
            tip = tr("Apply crop (Enter)");
            break;
        case CropHandle::None:
            break;
        }
        if (!tip.isEmpty()) {
            QToolTip::showText(viewport()->mapToGlobal(event->pos()), tip, viewport());
        } else {
            QToolTip::hideText();
        }
    }
    updateMouseInfo(event->pos());
    event->accept();
    return true;
}

bool ImageView::tryMouseMoveZoomRegion(QMouseEvent *event)
{
    if (!m_zoomRegion.dragging || !m_zoomRegion.rubberBand) {
        return false;
    }
    m_zoomRegion.rubberBand->setGeometry(ViewTransform::rubberRect(m_zoomRegion.origin, event->pos()));
    event->accept();
    return true;
}

bool ImageView::tryMouseMovePageGuide(QMouseEvent *event)
{
    if (m_pageGuide.dragHandle >= 0) {
        updatePageGuideResize(mapToScene(event->pos()), event->modifiers());
        event->accept();
        return true;
    }
    if (isWorkspaceMode() && m_pageGuide.visible && m_pageGuide.selected
        && !(event->buttons() & Qt::LeftButton)) {
        const int ph = pageGuideHandleAt(event->pos());
        if (m_pageGuide.setHoverHandle(ph)) {
            viewport()->update();
        }
        if (ph >= 0) {
            switch (ph) {
            case 0: case 4:
                viewport()->setCursor(Qt::SizeFDiagCursor);
                break;
            case 2: case 6:
                viewport()->setCursor(Qt::SizeBDiagCursor);
                break;
            case 1: case 5:
                viewport()->setCursor(Qt::SizeVerCursor);
                break;
            case 3: case 7:
                viewport()->setCursor(Qt::SizeHorCursor);
                break;
            default:
                break;
            }
            event->accept();
            return true;
        }
    }
    return false;
}

bool ImageView::tryMouseMoveGroupAndHandleDrag(QMouseEvent *event)
{
    if (m_groupXform.scaleDrag) {
        updateGroupScale(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_groupXform.rotateDrag) {
        updateGroupRotate(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_itemInteract.handleDragItem && m_itemInteract.handleDragItem->hasActiveHandle()) {
        m_itemInteract.handleDragItem->updateHandleInteraction(mapToScene(event->pos()),
                                                    event->modifiers());
        viewport()->update(); // live chrome while scaling/rotating
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryMouseMoveWorkspaceRotate(QMouseEvent *event)
{
    if (!m_itemInteract.rotating || !m_itemInteract.rotateItem) {
        return false;
    }
    const QPointF scenePos = mapToScene(event->pos());
    const qreal angle = angleAt(scenePos, m_itemInteract.rotateItem);
    // Shift is held to start free-rotate; Ctrl snaps 90°, Shift alone 45°.
    const qreal rot = PlacementLinear::placementRotationFromDrag(
        m_itemInteract.rotateItemStart, m_itemInteract.rotateStartAngle, angle,
        event->modifiers() & Qt::ControlModifier,
        event->modifiers() & Qt::ShiftModifier);
    m_itemInteract.rotateItem->setItemRotation(rot);
    m_framing.fitMode = false;
    emit statusChanged();
    event->accept();
    return true;
}

void ImageView::updateMouseMoveSlideshowSeek(QMouseEvent *event)
{
    if (!m_ssHud.progressActive || !viewport()) {
        return;
    }
    const int y = event->pos().y();
    const int h = viewport()->height();
    const bool nearBottom = h > 0 && y >= h - 48;
    if (nearBottom != m_ssHud.seekbarVisible && !m_ssHud.seekDragging) {
        m_ssHud.seekbarVisible = nearBottom;
        viewport()->update();
    }
    if (m_ssHud.seekDragging && h > 0 && viewport()->width() > 0) {
        const qreal f = ViewTransform::unitFraction(event->pos().x(), viewport()->width());
        emit slideshowSeekRequested(f);
    }
}

void ImageView::updateMouseMoveWorkspaceChromeHover(QMouseEvent *event)
{
    // Workspace: drive handle hover from the view so highlight matches the
    // view-owned hit path (rotated / covered items included).
    if (isWorkspaceMode() && m_tool == Tool::Select && !m_itemInteract.handleDragItem
        && !m_groupXform.scaleDrag && !m_groupXform.rotateDrag && !m_chrome.panning) {
        const QPointF scenePos = mapToScene(event->pos());
        QList<ImageItem *> candidates;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    candidates.append(ii);
                }
            }
        }

        // Multi-select: only group handles (individual chrome is hidden).
        if (candidates.size() > 1) {
            for (ImageItem *item : candidates) {
                if (item->hoverHandle() != ImageItem::Handle::None) {
                    item->setHoverHandle(ImageItem::Handle::None);
                }
            }
            const int gh = groupHandleAt(event->pos(), candidates);
            if (m_groupXform.setHoverHandle(gh)) {
                viewport()->update();
            }
            if (gh >= 0) {
                // 0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L; 8–11 rotate
                switch (gh) {
                case 0: case 4: // TL, BR — NW–SE diagonal
                    viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case 2: case 6: // TR, BL — NE–SW diagonal
                    viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case 1: case 5:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case 3: case 7:
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case 8: case 9: case 10: case 11:
                    viewport()->setCursor(Qt::CrossCursor);
                    break;
                default:
                    viewport()->setCursor(Qt::ArrowCursor);
                    break;
                }
                if (groupHoverChanged) {
                    const QString tip = GroupTransformGeometry::isRotateHandle(gh)
                        ? tr("Rotate selection")
                        : tr("Scale selection");
                    QToolTip::showText(viewport()->mapToGlobal(event->pos()), tip, viewport());
                }
            } else if (!m_chrome.panning) {
                viewport()->unsetCursor();
                if (groupHoverChanged) {
                    QToolTip::hideText();
                }
            }
        } else {
            if (m_groupXform.hoverHandle != -1) {
                m_groupXform.clearHover();
                viewport()->update();
            }
            ImageItem *hoverOwner = nullptr;
            ImageItem::Handle hoverH = ImageItem::Handle::None;
            std::sort(candidates.begin(), candidates.end(),
                      [](ImageItem *a, ImageItem *b) { return a->stackZ() > b->stackZ(); });
            for (ImageItem *item : candidates) {
                const ImageItem::Handle h = item->handleAt(item->mapFromScene(scenePos));
                if (h != ImageItem::Handle::None) {
                    hoverOwner = item;
                    hoverH = h;
                    break;
                }
            }
            bool hoverChanged = false;
            for (ImageItem *item : candidates) {
                const ImageItem::Handle next =
                    (item == hoverOwner) ? hoverH : ImageItem::Handle::None;
                if (item->hoverHandle() != next) {
                    hoverChanged = true;
                }
                item->setHoverHandle(next);
            }
            if (hoverChanged) {
                viewport()->update();
            }
            if (hoverOwner && hoverH != ImageItem::Handle::None) {
                using H = ImageItem::Handle;
                switch (hoverH) {
                case H::RotateTop: case H::RotateRight:
                case H::RotateBottom: case H::RotateLeft:
                    viewport()->setCursor(Qt::CrossCursor);
                    break;
                case H::ScaleTopLeft: case H::ScaleBottomRight:
                    // NW–SE diagonal
                    viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case H::ScaleTopRight: case H::ScaleBottomLeft:
                    // NE–SW diagonal
                    viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case H::ScaleTop: case H::ScaleBottom:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case H::ScaleLeft: case H::ScaleRight:
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case H::ShearTop: case H::ShearBottom:
                    // Horizontal shear: drag along local X (↔ when upright).
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case H::ShearLeft: case H::ShearRight:
                    // Vertical local shear: drag along local Y (↕ when upright).
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case H::OpacitySlider:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                default:
                    viewport()->setCursor(Qt::PointingHandCursor);
                    break;
                }
                if (hoverChanged) {
                    const QString tip = ImageItem::handleToolTip(hoverH);
                    if (!tip.isEmpty()) {
                        QToolTip::showText(viewport()->mapToGlobal(event->pos()), tip, viewport());
                    } else {
                        QToolTip::hideText();
                    }
                }
            } else if (!m_chrome.panning && !m_itemInteract.handleDragItem) {
                viewport()->unsetCursor();
                if (hoverChanged) {
                    QToolTip::hideText();
                }
            }
        }
    }

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
    updateGalleryHoverAt(m_chrome.lastHoverViewPos);
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

bool ImageView::tryMouseReleaseSlideshowSeek(QMouseEvent *event)
{
    if (!m_ssHud.seekDragging || event->button() != Qt::LeftButton) {
        return false;
    }
    m_ssHud.seekDragging = false;
    event->accept();
    if (viewport()) {
        viewport()->update();
    }
    return true;
}

bool ImageView::tryMouseReleaseTextRubber(QMouseEvent *event)
{
    if (!m_textLayer.rubberbanding || event->button() != Qt::LeftButton) {
        return false;
    }
    m_textLayer.updateRubber(event->pos());
    finishTextRubberBand();
    unsetCursor();
    event->accept();
    return true;
}

bool ImageView::tryMouseReleaseAttention(QMouseEvent *event)
{
    if (!m_attention.mode || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_attention.rubberbanding) {
        ImageItem *item = targetItem();
        if (item && !item->contentRect().isEmpty()) {
            const QVector<QPointF> pts = attentionPointsForTarget();
            QVector<QPointF> viewPts;
            viewPts.reserve(pts.size());
            for (const QPointF &n : pts) {
                viewPts.append(attentionViewPos(item, n));
            }
            const QVector<int> hit = AttentionGeometry::indicesInViewRect(
                viewPts, m_attention.rubberRect);
            const bool shift = event->modifiers() & Qt::ShiftModifier;
            m_attention.setSelected(AttentionGeometry::mergeSelection(
                m_attention.selected, hit, shift));
        }
        m_attention.endRubber();
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_attention.dragging) {
        m_attention.endPointDrag();
        attentionCommitSelectionMove();
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryMouseReleaseCrop(QMouseEvent *event)
{
    if (!m_crop.mode || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_crop.activeHandle != CropHandle::None) {
        endCropHandleDrag();
        event->accept();
        return true;
    }
    if (m_crop.rubberBanding) {
        endCropRubberBand();
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryMouseReleaseZoomRegion(QMouseEvent *event)
{
    if (!m_zoomRegion.dragging) {
        return false;
    }
    const QRect viewRect = ViewTransform::rubberRect(m_zoomRegion.origin, event->pos());
    m_zoomRegion.endDrag();
    if (m_zoomRegion.rubberBand) {
        m_zoomRegion.rubberBand->hide();
    }
    // Ignore tiny clicks — treat as cancel rather than extreme zoom.
    if (m_zoomRegion.rubberSignificant(viewRect)) {
        const QRectF sceneRect = mapToScene(viewRect).boundingRect();
        if (sceneRect.isValid() && !sceneRect.isEmpty()) {
            releaseStickyZoom();
            m_framing.fitMode = false;
            m_framing.fillMode = false;
            fitInView(sceneRect, Qt::KeepAspectRatio);
            emit statusChanged();
        }
    }
    cancelZoomRegion();
    event->accept();
    return true;
}

bool ImageView::tryMouseReleasePageGuide(QMouseEvent *event)
{
    if (m_pageGuide.dragHandle < 0 || event->button() != Qt::LeftButton) {
        return false;
    }
    endPageGuideResize();
    event->accept();
    return true;
}

bool ImageView::tryMouseReleaseGroupDrag(QMouseEvent *event)
{
    if (!(m_groupXform.scaleDrag || m_groupXform.rotateDrag) || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_undoStack && !m_groupXform.dragItems.isEmpty()) {
        m_undoStack->beginMacro(m_groupXform.rotateDrag ? tr("Rotate selection")
                                                  : tr("Scale selection"));
        for (int i = 0; i < m_groupXform.dragItems.size(); ++i) {
            ImageItem *item = m_groupXform.dragItems.at(i);
            if (!item || i >= m_groupXform.dragStartStates.size()) {
                continue;
            }
            pushItemTransformUndo(item, m_groupXform.dragStartStates.at(i), captureState(item),
                                  tr("Transform"));
        }
        m_undoStack->endMacro();
    }
    endGroupScale();
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    event->accept();
    return true;
}

bool ImageView::tryMouseReleaseHandleDrag(QMouseEvent *event)
{
    if (!m_itemInteract.handleDragItem || event->button() != Qt::LeftButton) {
        return false;
    }
    ImageItem *handleItem = m_itemInteract.handleDragItem;
    handleItem->endHandleInteraction();
    pushItemTransformUndo(handleItem, m_itemInteract.dragStartState,
                          captureState(handleItem), tr("Transform"));
    m_itemInteract.endHandleDrag();
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    event->accept();
    return true;
}

bool ImageView::tryMouseReleaseWorkspaceRotate(QMouseEvent *event)
{
    if (!m_itemInteract.rotating || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_itemInteract.rotateItem) {
        pushItemTransformUndo(m_itemInteract.rotateItem, m_itemInteract.dragStartState,
                              captureState(m_itemInteract.rotateItem), tr("Rotate"));
    }
    m_itemInteract.endRotate();
    restoreToolCursor();
    event->accept();
    return true;
}

bool ImageView::tryMouseReleasePan(QMouseEvent *event)
{
    if (!m_chrome.panning
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
    if (!m_itemInteract.dragItem || event->button() != Qt::LeftButton) {
        return false;
    }
    pushItemTransformUndo(m_itemInteract.dragItem, m_itemInteract.dragStartState, captureState(m_itemInteract.dragItem), tr("Move"));
    m_itemInteract.dragItem = nullptr;
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

bool ImageView::tryKeyPressAttention(QKeyEvent *event)
{
    if (!m_attention.mode) {
        return false;
    }
    if (event->key() == Qt::Key_Escape) {
        setAttentionMode(false);
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        attentionDeleteSelected();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) {
        const int n = attentionPointsForTarget().size();
        m_attention.selected.clear();
        for (int i = 0; i < n; ++i) {
            m_attention.selected.append(i);
        }
        if (viewport()) {
            viewport()->update();
        }
        event->accept();
        return true;
    }
    // Detect is toolbar-only — Space is reserved for slideshow.
    return false;
}

bool ImageView::tryKeyPressCrop(QKeyEvent *event)
{
    if (!m_crop.mode) {
        return false;
    }
    if (event->key() == Qt::Key_Escape) {
        cancelCrop();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        applyCrop();
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryKeyPressZoomRegion(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Escape || !(m_zoomRegion.armed || m_zoomRegion.dragging)) {
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

bool ImageView::tryKeyPressImageNavigate(QKeyEvent *event)
{
    // Image mode: Left/Right (and friends) navigate the session. QGraphicsView
    // would otherwise scroll the viewport when the image is zoomed or the view
    // has focus (typical in fullscreen), swallowing the QAction shortcuts.
    if (!isImageMode()
        || (event->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        emit navigatePreviousRequested();
        event->accept();
        return true;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
        emit navigateNextRequested();
        event->accept();
        return true;
    default:
        return false;
    }
}

ImageItem *ImageView::selectedOrFirstGalleryItem() const
{
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            return ii;
        }
    }
    return m_items.isEmpty() ? nullptr : m_items.first();
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

bool ImageView::tryKeyPressGallery(QKeyEvent *event)
{
    // Gallery: arrow keys move among tiles by scene position; Enter opens.
    if (!isGalleryMode()
        || (event->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        || m_items.isEmpty()) {
        return false;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (ImageItem *item = selectedOrFirstGalleryItem()) {
            if (item->sessionId() != kInvalidSessionImageId) {
                emit sessionImageOpenRequested(item->sessionId());
            } else if (item->sessionIndex() >= 0) {
                emit sessionSlotOpenRequested(item->sessionIndex());
            } else if (!item->path().isEmpty()) {
                emit galleryItemOpenRequested(item->path());
            }
            event->accept();
            return true;
        }
        return false;
    }

    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        ImageItem *item = (event->key() == Qt::Key_Home)
                              ? m_items.first()
                              : m_items.last();
        focusSessionPath(item->path());
        emitGalleryItemFocus(item);
        event->accept();
        return true;
    }

    // Spatial neighbour: prefer candidates in the arrow direction, score by
    // primary-axis distance with a cross-axis penalty (grid-friendly).
    const int key = event->key();
    if (key != Qt::Key_Left && key != Qt::Key_Right
        && key != Qt::Key_Up && key != Qt::Key_Down) {
        return false;
    }
    ImageItem *from = selectedOrFirstGalleryItem();
    if (!from) {
        from = m_items.first();
    }
    const QPointF origin = from->sceneBoundingRect().center();
    ImageItem *best = nullptr;
    qreal bestScore = 1e300;
    for (ImageItem *cand : m_items) {
        if (!cand || cand == from) {
            continue;
        }
        const QPointF c = cand->sceneBoundingRect().center();
        const auto scored = WorkspaceNavGeometry::scoreRelative(
            static_cast<Qt::Key>(key), origin, c);
        if (!scored.inDirection) {
            continue;
        }
        if (scored.score < bestScore) {
            bestScore = scored.score;
            best = cand;
        }
    }
    if (!best) {
        return false;
    }
    focusSessionPath(best->path());
    emitGalleryItemFocus(best);
    event->accept();
    return true;
}

bool ImageView::tryKeyPressWorkspaceShear(QKeyEvent *event)
{
    // Workspace: Alt+[ / Alt+] nudge horizontal shear; Alt+0 resets shear.
    if (!isWorkspaceMode()
        || !(event->modifiers() & Qt::AltModifier)
        || (event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    const int key = event->key();
    if (key != Qt::Key_BracketLeft && key != Qt::Key_BracketRight
        && key != Qt::Key_0) {
        return false;
    }
    const QList<QGraphicsItem *> selected =
        m_scene ? m_scene->selectedItems() : QList<QGraphicsItem *>();
    QList<ImageItem *> targets;
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (m_items.contains(item)) {
                targets.append(item);
            }
        }
    }
    if (targets.isEmpty()) {
        return false;
    }
    const qreal step = PlacementLinear::shearStepFromModifiers(
        event->modifiers() & Qt::ShiftModifier);
    for (ImageItem *item : targets) {
        if (key == Qt::Key_0) {
            item->setItemShear(0.0);
        } else {
            item->setItemShear(PlacementLinear::shearAfterKey(
                item->itemShear(), step, key == Qt::Key_BracketRight));
        }
        commitItemSessionEdit(item);
    }
    emit statusChanged();
    event->accept();
    return true;
}

bool ImageView::tryKeyPressDeleteSelection(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Delete
        && !(event->key() == Qt::Key_Backspace && isMultiItemMode())) {
        return false;
    }
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    QVector<SessionImageId> removeIds;
    QStringList removePaths;
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (!m_items.contains(item)) {
                continue;
            }
            if (item->sessionId() != kInvalidSessionImageId) {
                removeIds.append(item->sessionId());
            } else if (!item->path().isEmpty()) {
                removePaths.append(item->path());
            }
        }
    }
    if (removeIds.isEmpty() && removePaths.isEmpty()) {
        return false;
    }
    if (isGalleryMode()) {
        // Gallery tiles are the session — remove by id when bound.
        if (!removeIds.isEmpty()) {
            emit sessionRemoveIdsRequested(removeIds);
        }
        if (!removePaths.isEmpty()) {
            emit sessionRemovePathsRequested(removePaths);
        }
        event->accept();
        return true;
    }
    if (isWorkspaceMode()) {
        // Workspace: hide from canvas only; session membership stays.
        // Destroy by item pointer (same path may exist twice after Duplicate).
        QList<ImageItem *> toRemove;
        for (QGraphicsItem *gi : selected) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (m_items.contains(item)) {
                    toRemove.append(item);
                }
            }
        }
        setUpdatesEnabled(false);
        if (m_scene) {
            m_scene->blockSignals(true);
        }
        for (ImageItem *item : toRemove) {
            destroyCanvasItem(item);
        }
        if (m_scene) {
            m_scene->blockSignals(false);
        }
        setUpdatesEnabled(true);
        viewport()->update();
        emit statusChanged();
        emit workspacePathsChanged();
        event->accept();
        return true;
    }
    return false;
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
