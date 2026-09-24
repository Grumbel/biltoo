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
        || tryMousePressPan(event)
        || m_workspace.tryMousePressWorkspaceRotate(event)
        || m_gallery.tryMousePressGalleryRight(event)
        || m_gallery.tryMousePressGalleryLeft(event)
        || m_workspace.tryMousePressSelect(event)) {
        return;
    }

    QGraphicsView::mousePressEvent(event);
}
void ImageView::updateMouseMoveLinkHover(QMouseEvent *event)
{
    // Link hover: pointing hand + status tip (Image mode page docs).
    if (isImageMode() && !m_cropCtrl.session().active() && !m_attentionCtrl.session().active() && !m_textCtrl.session().isRubberbanding()
        && !m_chrome.isPanning() && event->buttons() == Qt::NoButton
        && PagePath::isPageRef(m_image.classicPath())) {
        if (!m_textCtrl.session().hasLayerRegions() || m_textCtrl.session().layerPathRef() != m_image.classicPath()) {
            const ThumtooCache::PageTextLayer cached =
                ThumtooCache::cachedPageTextLayer(m_image.classicPath());
            if (!cached.regions.isEmpty()) {
                m_textCtrl.session().setLayerContent(cached, m_image.classicPath());
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
        } else if (hostHoverEdge() == EdgeZone::None) {
            setCursor(m_chrome.isImageModeLeftDragPan() ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
        if (m_textCtrl.session().setLinkHoverTip(tip)) {
            emit statusChanged();
        }
    } else if (m_textCtrl.session().hasLinkHoverTip() && event->buttons() == Qt::NoButton) {
        m_textCtrl.session().clearLinkHoverTip();
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
    // visible set without a zoom/climb event — coalesce issues (not per move).
    m_displayPipeline->scheduleTileLodAfterInteraction(32);
    event->accept();
    return true;
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
    updateMouseMoveLinkHover(event);
    if (m_attentionCtrl.tryMouseMoveAttention(event)
        || m_cropCtrl.tryMouseMoveCropDrag(event)
        || tryMouseMovePan(event)
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

    m_chrome.setHoverViewPos(event->pos());
    m_slideshow.updateMouseMoveSlideshowSeek(event);
    m_gallery.updateGalleryHoverAt(m_chrome.hoverViewPos());
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


bool ImageView::tryMouseReleasePan(QMouseEvent *event)
{
    if (!m_chrome.isPanning()
        || (event->button() != Qt::MiddleButton && event->button() != Qt::LeftButton)) {
        return false;
    }
    m_chrome.endPan();
    restoreToolCursor();
    m_displayPipeline->tickPrimaryTileLod(8);
    event->accept();
    return true;
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
        || tryMouseReleasePan(event)) {
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
    // Same path↔id guard as emitItemOpenInImageMode: a scrubbed/stale SessionImageId
    // must not move the session cursor to another document row.
    if (item->sessionId() != kInvalidSessionImageId
        && sessionIdMatchesPath(item->sessionId(), item->path())) {
        emit sessionImageFocused(item->sessionId());
        return;
    }
    // Unbound or id/path conflict: list-order cache, then Gallery live index.
    int listIdx = sessionListIndex(item);
    if (listIdx < 0 && isGalleryMode()) {
        const int live = m_items.indexOf(item);
        if (live >= 0
            && (!m_sessionDoc || live < m_sessionDoc->size())) {
            listIdx = live;
        }
    }
    if (listIdx < 0 && m_sessionDoc && !item->path().isEmpty()) {
        listIdx = m_sessionDoc->indexOfPathPreferId(item->path());
    }
    if (listIdx >= 0) {
        emit sessionSlotFocused(listIdx);
        return;
    }
    if (!item->path().isEmpty()) {
        emit galleryItemFocused(item->path());
    }
}

void ImageView::emitItemOpenInImageMode(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Prefer SessionImageId only when it still matches this tile's path in the
    // session document. After setItemSessionId conflict scrub, a tile can keep
    // a stale id that belongs to another path — opening by that id would load
    // the wrong file while classicPath may still be set from a prior pin.
    if (item->sessionId() != kInvalidSessionImageId
        && sessionIdMatchesPath(item->sessionId(), item->path())) {
        emit sessionImageOpenRequested(item->sessionId());
        return;
    }
    int listIdx = sessionListIndex(item);
    // Gallery pack order aligns live canvas with session rows after reorder.
    if (listIdx < 0 && isGalleryMode()) {
        const int live = m_items.indexOf(item);
        if (live >= 0
            && (!m_sessionDoc || live < m_sessionDoc->size())) {
            listIdx = live;
        }
    }
    // Unbound or id/path conflict: prefer document row by list cache, then path.
    if (listIdx < 0 && m_sessionDoc && !item->path().isEmpty()) {
        listIdx = m_sessionDoc->indexOfPathPreferId(item->path());
    }
    if (listIdx >= 0) {
        emit sessionSlotOpenRequested(listIdx);
        return;
    }
    if (!item->path().isEmpty()) {
        emit galleryItemOpenRequested(item->path());
    }
}


void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_attentionCtrl.tryKeyPressAttention(event)
        || m_cropCtrl.tryKeyPressCrop(event)
        || tryKeyPressZoomRegion(event)
        || tryKeyPressSelectAll(event)
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
    // Rapid edge clicks arrive as double-clicks (second press is not a Press event).
    // Treat them as navigation, same as a single click on the affordance.
    if (isImageMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        const EdgeZone zone = edgeZoneAt(event->pos());
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
        emit fullscreenToggleRequested();
        event->accept();
        return;
    }

    // Gallery: double-click opens the tile in Image mode (classic file view).
    // Prefer SessionImageId so duplicate paths open the correct session row.
    if (isGalleryMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                emitItemOpenInImageMode(item);
                event->accept();
                return;
            }
        }
        event->accept();
        return;
    }

    // Workspace: double-click on chrome starts a handle drag; on the image
    // body opens Image mode (same path as Gallery). Empty space is swallowed
    // so the missing second press does not clear selection via the base class.
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && m_workspace.currentTool() == Tool::Select && m_scene) {
        const QPointF scenePos = mapToScene(event->pos());
        // Selected item handles first (chrome is above tiles).
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    selected.append(ii);
                }
            }
        }
        if (selected.size() == 1) {
            ImageItem *item = selected.first();
            HandlePressScratch press;
            if (item->beginHandleInteraction(scenePos, event->modifiers(), &press)
                && press.hasContinuousHandle()) {
                m_workspace.itemInteract().beginHandleDrag(item, placementFromItem(item), press);
                event->accept();
                return;
            }
        } else if (selected.size() > 1) {
            const int gh = m_workspace.groupHandleAt(event->pos(), selected);
            if (gh >= 0 && m_workspace.beginGroupScale(gh, selected)) {
                event->accept();
                return;
            }
        }
        // Image body under cursor → Image mode for *this* session slot
        // (path-only open would always hit the first duplicate in the session).
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    emitItemOpenInImageMode(ii);
                    event->accept();
                    return;
                }
            }
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    if (m_chrome.hasMouseInfo()) {
        m_chrome.clearMouseInfo();
        emit mouseInfoChanged(m_chrome.currentMouseInfo());
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
