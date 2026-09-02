// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
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
#include <cmath>

int ImageView::edgeZoneWidth() const
{
    return qMax(48, static_cast<int>(width() * 0.12));
}

int ImageView::edgeZoneHeight() const
{
    return qMax(40, static_cast<int>(height() * 0.10));
}

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    if (!isImageMode()) {
        return EdgeZone::None;
    }
    // Top strip: back to Gallery or Workspace (when Image was opened from there).
    // Takes priority over left/right so the upper corners still return.
    if (m_galleryReturnAvailable && viewPos.y() < edgeZoneHeight()) {
        return EdgeZone::GalleryReturn;
    }
    if (!m_imageModeNavEnabled) {
        return EdgeZone::None;
    }
    const int zone = edgeZoneWidth();
    if (viewPos.x() < zone) {
        return EdgeZone::Previous;
    }
    if (viewPos.x() > width() - zone) {
        return EdgeZone::Next;
    }
    return EdgeZone::None;
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
    } else if (!m_panning && !m_rotating) {
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    viewport()->update();
}



void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    const QPointF scenePos = mapToScene(event->position().toPoint());
    emit filesDropped(event->mimeData()->urls(), event->modifiers(), scenePos);
    event->acceptProposedAction();
}





QRectF ImageView::selectionSceneBounds(const QList<ImageItem *> &items) const
{
    QRectF bounds;
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        const QRectF r = item->contentSceneRect();
        if (!r.isValid() || r.isEmpty()) {
            continue;
        }
        bounds = bounds.isValid() ? bounds.united(r) : r;
    }
    return bounds;
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

    if (info.valid != m_mouseInfo.valid
        || info.imagePos != m_mouseInfo.imagePos
        || info.pixelColor != m_mouseInfo.pixelColor
        || info.path != m_mouseInfo.path) {
        m_mouseInfo = info;
        emit mouseInfoChanged(m_mouseInfo);
    }
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    // Gallery: wheel only scrolls the packed scene. Zoom belongs to Image mode
    // (and Workspace). Never scale the view from the wheel here — that felt
    // random when overflow was small or Ctrl was held accidentally.
    if (isGalleryMode()) {
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
            m_layoutMode == LayoutMode::SideBySide
            || m_layoutMode == LayoutMode::MasonryRows;

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
        return;
    }

    const qreal factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);

    // Image mode and free-form Workspace: zoom the view about the cursor.
    // Do not touch selected-item geometry here — prepareGeometryChange on
    // handle pads was expanding AABBs and fighting the user's pan/zoom.
    m_fitMode = false;
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    viewport()->update(); // refresh viewport-space chrome at the new scale
    emit statusChanged();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_applyingLayout) {
        return;
    }
    // Gallery: never repack from resize. Thumb-strip setFiles, scrollbar
    // policy, and splitter drags all resize this view; packing here made
    // session delete look like an automatic layout. Pack only on explicit
    // layout actions / F5 (applyLayout callers).
    if (isGalleryMode()) {
        return;
    }
    if (m_fitMode && m_items.size() == 1) {
        fitItem(m_items.first(), currentFitAspectMode());
    }
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    // Crop mode: handles adjust the draft rect; drag on image starts rubber-band.
    if (m_cropMode && event->button() == Qt::LeftButton) {
        const CropHandle h = cropHandleAt(event->pos());
        if (h == CropHandle::ExpandToggle) {
            m_cropAllowExpand = !m_cropAllowExpand;
            if (!m_cropAllowExpand) {
                ensureCropRectValid(); // clamp back into the image
            }
            viewport()->update();
            event->accept();
            return;
        }
        if (h == CropHandle::Reset) {
            // Expand draft to the full image; Enter/Apply commits a cleared session crop.
            if (ImageItem *item = cropTargetItem()) {
                m_cropRect = item->contentRect();
                ensureCropRectValid();
                viewport()->update();
            }
            event->accept();
            return;
        }
        if (h == CropHandle::Apply) {
            applyCrop();
            event->accept();
            return;
        }
        if (h != CropHandle::None) {
            beginCropHandleDrag(h, event->pos());
            event->accept();
            return;
        }
        // Middle/Alt still pan; plain left on the image body → new rubber-band crop.
        if (!(event->modifiers()
              & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier))) {
            beginCropRubberBand(event->pos());
            if (m_cropRubberBanding) {
                event->accept();
                return;
            }
        }
    }

    // One-shot rubber-band zoom (Z): capture the region before other tools.
    if (m_zoomRegionArmed && event->button() == Qt::LeftButton) {
        m_zoomRegionDragging = true;
        m_zoomRegionOrigin = event->pos();
        if (!m_zoomRubberBand) {
            m_zoomRubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
        }
        m_zoomRubberBand->setGeometry(QRect(m_zoomRegionOrigin, QSize()));
        m_zoomRubberBand->show();
        event->accept();
        return;
    }

    // Workspace chrome hit-testing is view-owned (DOMAIN: free-object transforms).
    // Chrome is painted above all tiles in viewport space; hit-testing must
    // similarly ignore scene z-order of other images under the pointer.
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && m_tool == Tool::Select) {
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
                m_groupPressScenePos = mapToScene(event->pos());
                event->accept();
                return;
            }
        } else if (selected.size() == 1) {
            // Single selection: test that item's handles first, even when the
            // pointer is over another tile's pixmap (handles are drawn on top).
            ImageItem *item = selected.first();
            if (item->beginHandleInteraction(scenePos, event->modifiers())) {
                m_handleDragItem = item;
                m_dragItem = item;
                m_dragStartState = captureState(item);
                event->accept();
                return;
            }
        }
        // No handle hit — fall through to move/select / clear.
    }

    // Image mode: edge clicks — top returns to Gallery/Workspace; left/right navigate
    if (isImageMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        const EdgeZone zone = edgeZoneAt(event->pos());
        if (zone == EdgeZone::GalleryReturn) {
            emit galleryReturnRequested();
            event->accept();
            return;
        }
        if (zone == EdgeZone::Previous) {
            emit navigatePreviousRequested();
            event->accept();
            return;
        }
        if (zone == EdgeZone::Next) {
            emit navigateNextRequested();
            event->accept();
            return;
        }
    }

    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton
            && ((isImageMode() && m_imageModeLeftDragPan)
                || (isWorkspaceMode() && m_tool == Tool::Pan)
                || (isGalleryMode() && (event->modifiers() & Qt::AltModifier))
                || (event->modifiers() & Qt::AltModifier)))) {
        if (!(isWorkspaceMode() && (event->modifiers() & Qt::ShiftModifier)
              && event->button() == Qt::LeftButton)) {
            m_panning = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Workspace only: Shift + left button free-rotates (unless the press is on
    // a selected item's scale/chrome handle — those use Shift for opposite-edge scale).
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ShiftModifier)) {
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
        } else if (hit) {
            m_rotating = true;
            m_rotateItem = hit;
            m_rotateStartAngle = angleAt(scenePos, hit);
            m_rotateItemStart = hit->itemRotation();
            m_dragStartState = captureState(hit);
            m_scene->clearSelection();
            hit->setSelected(true);
            setCursor(Qt::CrossCursor);
            event->accept();
            return;
        }
    }

    // Gallery right-click: do not let QGraphicsView alter selection (that
    // cancels multi-select before the context menu opens). If the click is on
    // an unselected tile, select only that tile; if it is already selected,
    // keep the current multi-select for bulk rotate/flip/delete.
    if (isGalleryMode() && event->button() == Qt::RightButton) {
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
        return;
    }

    // Gallery: classic multi-select (click / Ctrl / Shift); open is double-click.
    if (isGalleryMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & Qt::AltModifier)) {
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
            m_scene->clearSelection();
            for (int i = i0; i <= i1 && i < m_items.size(); ++i) {
                m_items.at(i)->setSelected(true);
            }
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit galleryItemFocused(hit->path());
            }
            event->accept();
            emit statusChanged();
            return;
        }

        if (hit && ctrl) {
            hit->setSelected(!hit->isSelected());
            if (hit->isSelected()) {
                m_gallery.setSelectionAnchor(hit);
            }
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit galleryItemFocused(hit->path());
            }
            event->accept();
            emit statusChanged();
            return;
        }

        if (hit) {
            m_scene->clearSelection();
            hit->setSelected(true);
            m_gallery.setSelectionAnchor(hit);
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit galleryItemFocused(hit->path());
            }
            event->accept();
            emit statusChanged();
            return;
        }

        // Empty space: clear selection (keep Ctrl-additive empty no-ops).
        if (!ctrl) {
            m_scene->clearSelection();
            emit statusChanged();
        }
        // Allow rubber-band start via base class when drag mode is RubberBandDrag.
        QGraphicsView::mousePressEvent(event);
        return;
    }

    // Workspace Select tool: let QGraphicsView handle selection / move
    if (isWorkspaceMode() && event->button() == Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        // Capture drag start for undo when an item is selected under the cursor
        if (ImageItem *hit = targetItem()) {
            m_dragItem = hit;
            m_dragStartState = captureState(hit);
        }
        emit statusChanged();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_cropMode && m_cropActiveHandle != CropHandle::None) {
        updateCropHandleDrag(event->pos());
        event->accept();
        return;
    }
    if (m_cropMode && m_cropRubberBanding) {
        updateCropRubberBand(event->pos());
        event->accept();
        return;
    }
    // Middle-button (or Alt) pan must work in crop mode — handle before crop hover.
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (m_cropMode) {
        const CropHandle h = cropHandleAt(event->pos());
        if (h != m_cropHoverHandle) {
            m_cropHoverHandle = h;
            viewport()->update();
        }
        switch (h) {
        case CropHandle::Move:
            viewport()->setCursor(Qt::SizeAllCursor);
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
        case CropHandle::Reset:
        case CropHandle::Apply:
            viewport()->setCursor(Qt::PointingHandCursor);
            break;
        case CropHandle::None:
            viewport()->setCursor(Qt::CrossCursor);
            break;
        }
        updateMouseInfo(event->pos());
        event->accept();
        return;
    }

    if (m_zoomRegionDragging && m_zoomRubberBand) {
        m_zoomRubberBand->setGeometry(QRect(m_zoomRegionOrigin, event->pos()).normalized());
        event->accept();
        return;
    }

    updateMouseInfo(event->pos());

    if (m_groupScaleDrag) {
        updateGroupScale(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return;
    }
    if (m_groupRotateDrag) {
        updateGroupRotate(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return;
    }

    if (m_handleDragItem && m_handleDragItem->hasActiveHandle()) {
        m_handleDragItem->updateHandleInteraction(mapToScene(event->pos()),
                                                    event->modifiers());
        viewport()->update(); // live chrome while scaling/rotating
        event->accept();
        return;
    }

    if (m_rotating && m_rotateItem) {
        const QPointF scenePos = mapToScene(event->pos());
        const qreal angle = angleAt(scenePos, m_rotateItem);
        const qreal delta = angle - m_rotateStartAngle;
        qreal rot = m_rotateItemStart + delta;
        if (event->modifiers() & Qt::ControlModifier) {
            rot = qRound(rot / 90.0) * 90.0;
        } else if (event->modifiers() & Qt::ShiftModifier) {
            // Shift is held to start free-rotate; add Ctrl for 90°, alone keep smooth
            // unless also... User asked Ctrl/Shift snap. Shift starts rotate so
            // during shift-drag, snap to 45 when Shift still held without wanting smooth.
            // Use Ctrl=90 always; for shift-drag path Shift is always down — snap 45.
            rot = qRound(rot / 45.0) * 45.0;
        }
        m_rotateItem->setItemRotation(rot);
        m_fitMode = false;
        emit statusChanged();
        event->accept();
        return;
    }

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        // Grow the free-form sceneRect with the view so middle-drag is never
        // clamped against a stale zero-range scrollbar.
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (isImageMode()) {
        updateHoverEdge(event->pos());
    }

    m_lastHoverViewPos = event->pos();
    updateGalleryHoverAt(m_lastHoverViewPos);

    // Workspace: drive handle hover from the view so highlight matches the
    // view-owned hit path (rotated / covered items included).
    if (isWorkspaceMode() && m_tool == Tool::Select && !m_handleDragItem
        && !m_groupScaleDrag && !m_groupRotateDrag && !m_panning) {
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
            if (gh != m_groupHoverHandle) {
                m_groupHoverHandle = gh;
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
            } else if (!m_panning) {
                viewport()->unsetCursor();
            }
        } else {
            if (m_groupHoverHandle != -1) {
                m_groupHoverHandle = -1;
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
                case H::OpacitySlider:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                default:
                    viewport()->setCursor(Qt::PointingHandCursor);
                    break;
                }
            } else if (!m_panning && !m_handleDragItem) {
                viewport()->unsetCursor();
            }
        }
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_cropMode && m_cropActiveHandle != CropHandle::None
        && event->button() == Qt::LeftButton) {
        endCropHandleDrag();
        event->accept();
        return;
    }
    if (m_cropMode && m_cropRubberBanding && event->button() == Qt::LeftButton) {
        endCropRubberBand();
        event->accept();
        return;
    }

    if (m_zoomRegionDragging) {
        const QRect viewRect = QRect(m_zoomRegionOrigin, event->pos()).normalized();
        m_zoomRegionDragging = false;
        if (m_zoomRubberBand) {
            m_zoomRubberBand->hide();
        }
        // Ignore tiny clicks — treat as cancel rather than extreme zoom.
        if (viewRect.width() >= 8 && viewRect.height() >= 8) {
            const QRectF sceneRect = mapToScene(viewRect).boundingRect();
            if (sceneRect.isValid() && !sceneRect.isEmpty()) {
                m_fitMode = false;
                m_fillMode = false;
                fitInView(sceneRect, Qt::KeepAspectRatio);
                emit statusChanged();
            }
        }
        cancelZoomRegion();
        event->accept();
        return;
    }
    if ((m_groupScaleDrag || m_groupRotateDrag) && event->button() == Qt::LeftButton) {
        if (m_undoStack && !m_groupDragItems.isEmpty()) {
            m_undoStack->beginMacro(m_groupRotateDrag ? tr("Rotate selection")
                                                      : tr("Scale selection"));
            for (int i = 0; i < m_groupDragItems.size(); ++i) {
                ImageItem *item = m_groupDragItems.at(i);
                if (!item || i >= m_groupDragStartStates.size()) {
                    continue;
                }
                const WorkspaceItemState after = captureState(item);
                const WorkspaceItemState &before = m_groupDragStartStates.at(i);
                if (after.pos == before.pos
                    && qFuzzyCompare(after.scale, before.scale)
                    && qFuzzyCompare(after.scaleY > 0 ? after.scaleY : 1.0,
                                     before.scaleY > 0 ? before.scaleY : 1.0)
                    && qFuzzyCompare(after.rotation, before.rotation)) {
                    continue;
                }
                // Inline undo entry matching other transform paths.
                class TransformCommand : public QUndoCommand {
                public:
                    TransformCommand(ImageView *view, ImageItem *item,
                                     const WorkspaceItemState &before,
                                     const WorkspaceItemState &after)
                        : m_view(view), m_item(item), m_before(before), m_after(after)
                    {
                        setText(QObject::tr("Transform"));
                    }
                    void undo() override { if (m_view && m_item) m_view->applyState(m_item, m_before); }
                    void redo() override { if (m_view && m_item) m_view->applyState(m_item, m_after); }
                private:
                    ImageView *m_view;
                    ImageItem *m_item;
                    WorkspaceItemState m_before, m_after;
                };
                m_undoStack->push(new TransformCommand(this, item, before, after));
            }
            m_undoStack->endMacro();
        }
        endGroupScale();
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        event->accept();
        return;
    }

    if (m_handleDragItem && event->button() == Qt::LeftButton) {
        m_handleDragItem->endHandleInteraction();
        const WorkspaceItemState after = captureState(m_handleDragItem);
        if (after.pos != m_dragStartState.pos
            || after.scale != m_dragStartState.scale
            || after.scaleY != m_dragStartState.scaleY
            || after.rotation != m_dragStartState.rotation
            || after.opacity != m_dragStartState.opacity) {
            class TransformCommand : public QUndoCommand {
            public:
                TransformCommand(ImageView *view, ImageItem *item,
                                 const WorkspaceItemState &before,
                                 const WorkspaceItemState &after)
                    : m_view(view), m_item(item), m_before(before), m_after(after)
                {
                    setText(QObject::tr("Transform"));
                }
                void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
            private:
                ImageView *m_view;
                ImageItem *m_item;
                WorkspaceItemState m_before, m_after;
            };
            if (m_undoStack) {
                m_undoStack->push(new TransformCommand(this, m_handleDragItem,
                                                       m_dragStartState, after));
            }
            emit statusChanged();
        }
        m_handleDragItem = nullptr;
        m_dragItem = nullptr;
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        event->accept();
        return;
    }

    if (m_rotating && event->button() == Qt::LeftButton) {
        if (m_rotateItem) {
            const WorkspaceItemState after = captureState(m_rotateItem);
            if (after.rotation != m_dragStartState.rotation
                || after.pos != m_dragStartState.pos) {
                // Lightweight: clear is avoided; push a simple undo via reset path
                // Store as single-step by re-applying start on undo through stack of states
                class TransformCommand : public QUndoCommand {
                public:
                    TransformCommand(ImageView *view, ImageItem *item,
                                     const WorkspaceItemState &before,
                                     const WorkspaceItemState &after)
                        : m_view(view), m_item(item), m_before(before), m_after(after)
                    {
                        setText(QObject::tr("Transform"));
                    }
                    void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                    void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
                private:
                    ImageView *m_view;
                    ImageItem *m_item;
                    WorkspaceItemState m_before, m_after;
                };
                m_undoStack->push(new TransformCommand(this, m_rotateItem,
                                                       m_dragStartState, after));
            }
        }
        m_rotating = false;
        m_rotateItem = nullptr;
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_dragItem && event->button() == Qt::LeftButton) {
        const WorkspaceItemState after = captureState(m_dragItem);
        if (after.pos != m_dragStartState.pos
            || after.scale != m_dragStartState.scale
            || after.scaleY != m_dragStartState.scaleY
            || after.rotation != m_dragStartState.rotation) {
            class TransformCommand : public QUndoCommand {
            public:
                TransformCommand(ImageView *view, ImageItem *item,
                                 const WorkspaceItemState &before,
                                 const WorkspaceItemState &after)
                    : m_view(view), m_item(item), m_before(before), m_after(after)
                {
                    setText(QObject::tr("Move"));
                }
                void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
            private:
                ImageView *m_view;
                ImageItem *m_item;
                WorkspaceItemState m_before, m_after;
            };
            m_undoStack->push(new TransformCommand(this, m_dragItem,
                                                   m_dragStartState, after));
            emit statusChanged();
        }
        m_dragItem = nullptr;
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_cropMode) {
        if (event->key() == Qt::Key_Escape) {
            cancelCrop();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            applyCrop();
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape && (m_zoomRegionArmed || m_zoomRegionDragging)) {
        cancelZoomRegion();
        event->accept();
        return;
    }

    // Image mode: Left/Right (and friends) navigate the session. QGraphicsView
    // would otherwise scroll the viewport when the image is zoomed or the view
    // has focus (typical in fullscreen), swallowing the QAction shortcuts.
    if (isImageMode()
        && !(event->modifiers()
             & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_PageUp:
        case Qt::Key_Backspace:
            emit navigatePreviousRequested();
            event->accept();
            return;
        case Qt::Key_Right:
        case Qt::Key_PageDown:
            emit navigateNextRequested();
            event->accept();
            return;
        default:
            break;
        }
    }

    // Gallery: arrow keys move among tiles by scene position; Enter opens.
    if (isGalleryMode()
        && !(event->modifiers()
             & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        && !m_items.isEmpty()) {
        auto selectedGalleryItem = [this]() -> ImageItem * {
            for (QGraphicsItem *gi : m_scene->selectedItems()) {
                if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                    return ii;
                }
            }
            return m_items.isEmpty() ? nullptr : m_items.first();
        };

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (ImageItem *item = selectedGalleryItem()) {
                if (item->sessionId() != kInvalidSessionImageId) {
                    emit sessionImageOpenRequested(item->sessionId());
                } else if (item->sessionIndex() >= 0) {
                    emit sessionSlotOpenRequested(item->sessionIndex());
                } else if (!item->path().isEmpty()) {
                    emit galleryItemOpenRequested(item->path());
                }
                event->accept();
                return;
            }
        }

        if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
            ImageItem *item = (event->key() == Qt::Key_Home)
                                  ? m_items.first()
                                  : m_items.last();
            focusSessionPath(item->path());
            if (item->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(item->sessionId());
            } else if (!item->path().isEmpty()) {
                emit galleryItemFocused(item->path());
            }
            event->accept();
            return;
        }

        // Spatial neighbour: prefer candidates in the arrow direction, score by
        // primary-axis distance with a cross-axis penalty (grid-friendly).
        const int key = event->key();
        if (key == Qt::Key_Left || key == Qt::Key_Right
            || key == Qt::Key_Up || key == Qt::Key_Down) {
            ImageItem *from = selectedGalleryItem();
            if (!from) {
                from = m_items.first();
            }
            const QPointF origin = from->sceneBoundingRect().center();
            ImageItem *best = nullptr;
            qreal bestScore = 1e300;
            constexpr qreal kEps = 1.0;
            constexpr qreal kCrossWeight = 2.5;
            for (ImageItem *cand : m_items) {
                if (cand == from) {
                    continue;
                }
                const QPointF d = cand->sceneBoundingRect().center() - origin;
                qreal primary = 0;
                qreal cross = 0;
                bool inDir = false;
                switch (key) {
                case Qt::Key_Left:
                    inDir = d.x() < -kEps;
                    primary = -d.x();
                    cross = qAbs(d.y());
                    break;
                case Qt::Key_Right:
                    inDir = d.x() > kEps;
                    primary = d.x();
                    cross = qAbs(d.y());
                    break;
                case Qt::Key_Up:
                    inDir = d.y() < -kEps;
                    primary = -d.y();
                    cross = qAbs(d.x());
                    break;
                case Qt::Key_Down:
                    inDir = d.y() > kEps;
                    primary = d.y();
                    cross = qAbs(d.x());
                    break;
                default:
                    break;
                }
                if (!inDir) {
                    continue;
                }
                const qreal score = primary + kCrossWeight * cross;
                if (score < bestScore) {
                    bestScore = score;
                    best = cand;
                }
            }
            if (best) {
                focusSessionPath(best->path());
                if (best->sessionId() != kInvalidSessionImageId) {
                emit sessionImageFocused(best->sessionId());
            } else if (!best->path().isEmpty()) {
                emit galleryItemFocused(best->path());
            }
                event->accept();
                return;
            }
        }
    }

    if (event->key() == Qt::Key_Delete
        || (event->key() == Qt::Key_Backspace && isMultiItemMode())) {
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
            // fall through
        } else if (isGalleryMode()) {
            // Gallery tiles are the session — remove by id when bound.
            if (!removeIds.isEmpty()) {
                emit sessionRemoveIdsRequested(removeIds);
            }
            if (!removePaths.isEmpty()) {
                emit sessionRemovePathsRequested(removePaths);
            }
            event->accept();
            return;
        } else if (isWorkspaceMode()) {
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
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}
