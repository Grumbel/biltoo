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
#include <QPaintEvent>
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
#include <QtMath>
#include <cmath>
#include <algorithm>

void ImageView::applyItemModeFlags(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Strict separation:
    //   Workspace → movable + handles
    //   Gallery   → selectable only (open on click), no chrome
    //   Image     → static, no selection chrome
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    } else if (isGalleryMode()) {
        item->setGallerySelectable(true);
        item->setScaleHandlesEnabled(false);
    } else {
        item->setGalleryCellSize({});
        item->setInteractive(false);
        item->setScaleHandlesEnabled(false);
    }
}

WorkspaceItemState ImageView::captureState(const ImageItem *item) const
{
    WorkspaceItemState s;
    s.path = item->path();
    s.pos = item->pos();
    s.scale = item->itemScaleX();
    s.scaleY = item->itemScaleY();
    s.rotation = item->itemRotation();
    s.opacity = item->itemOpacity();
    s.z = item->stackZ();
    s.hFlip = item->itemHFlip();
    s.vFlip = item->itemVFlip();
    return s;
}

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    item->setPos(state.pos);
    item->setItemScale(state.scale, state.scaleY > 0.0 ? state.scaleY : state.scale);
    item->setItemRotation(state.rotation);
    item->setItemOpacity(state.opacity);
    item->setStackZ(state.z);
    item->setItemHFlip(state.hFlip);
    item->setItemVFlip(state.vFlip);
}

void ImageView::rememberItemState(ImageItem *item)
{
    if (!item) {
        return;
    }
    m_itemStates.insert(item->path(), captureState(item));
}

void ImageView::snapshotWorkspace()
{
    m_savedWorkspace.clear();
    for (ImageItem *item : m_items) {
        const WorkspaceItemState s = captureState(item);
        m_savedWorkspace.append(s);
        m_itemStates.insert(s.path, s);
    }
}

void ImageView::restoreWorkspace()
{
    clearWorkspace();
    m_pendingWorkspacePaths.clear();
    // AUDIT M27: queue every saved state (including duplicate paths) then load.
    m_pendingRestoreStates = m_savedWorkspace;
    for (const WorkspaceItemState &state : m_savedWorkspace) {
        m_itemStates.insert(state.path, state);
        scheduleImageLoad(state.path, LoadRestore);
    }
    m_fitMode = false;
    emit statusChanged();
}

void ImageView::discardStashedGallery()
{
    for (ImageItem *item : m_stashedGalleryItems) {
        delete item;
    }
    m_stashedGalleryItems.clear();
    m_stashedGalleryPathOrder.clear();
}

void ImageView::stashGalleryItems()
{
    // Replace any previous stash (should be empty when leaving Gallery).
    discardStashedGallery();
    if (m_items.isEmpty()) {
        return;
    }
    m_stashedGalleryPathOrder = m_pathOrder;
    m_stashedGalleryItems = m_items;
    m_gallerySelectionAnchor = nullptr;
    m_galleryHoverPath.clear();
    for (ImageItem *item : m_stashedGalleryItems) {
        if (!item) {
            continue;
        }
        item->setSelected(false);
        if (item->scene()) {
            item->scene()->removeItem(item);
        }
    }
    m_items.clear();
    // Keep m_galleryDecodeScheduled / failed — paths still valid on return.
    // Pending LoadAdd for missing tiles can finish after restore.
}

void ImageView::restoreStashedGalleryItems()
{
    if (m_stashedGalleryItems.isEmpty()) {
        return;
    }
    // Live canvas should be empty (Image mode held a single item that
    // clearWorkspace removes before Gallery is entered).
    for (ImageItem *item : m_items) {
        if (item && item->scene()) {
            item->scene()->removeItem(item);
        }
        delete item;
    }
    m_items = m_stashedGalleryItems;
    m_stashedGalleryItems.clear();
    if (!m_stashedGalleryPathOrder.isEmpty()) {
        m_pathOrder = m_stashedGalleryPathOrder;
    }
    m_stashedGalleryPathOrder.clear();
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        if (!item->scene()) {
            m_scene->addItem(item);
        }
        applyItemModeFlags(item);
    }
    reorderItemsByPaths(m_pathOrder);
}

void ImageView::clearWorkspace()
{
    // AUDIT H8/H9: drop live pointers and undo commands that reference items
    // about to be destroyed.
    m_handleDragItem = nullptr;
    m_rotateItem = nullptr;
    m_rotating = false;
    m_dragItem = nullptr;
    m_gallerySelectionAnchor = nullptr;
    if (m_undoStack) {
        m_undoStack->clear();
    }
    // Session wipe / explicit clear — drop Gallery cache too.
    discardStashedGallery();
    m_items.clear();
    m_pendingScenePos.clear();
    m_pendingWorkspacePaths.clear();
    m_pendingRestoreStates.clear();
    m_galleryDecodeScheduled.clear();
    m_galleryDecodeFailed.clear();
    m_classicPath.clear();
    // clear() deletes all QGraphicsItems owned by the scene
    m_scene->clear();
    m_mouseInfo = {};
    emit mouseInfoChanged(m_mouseInfo);
}


ImageItem *ImageView::findItemByPath(const QString &path) const
{
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            return item;
        }
    }
    return nullptr;
}

void ImageView::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    m_scene->clearSelection();
    item->setSelected(true);
    if (isGalleryMode()) {
        ensureVisible(item, 48, 48);
        // Keyboard focus: show filename in the HUD like mouse hover.
        if (m_galleryHoverPath != path) {
            m_galleryHoverPath = path;
            viewport()->update();
        }
    }
}

void ImageView::revealGalleryPath(const QString &path)
{
    if (path.isEmpty() || !isGalleryMode()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    ensureVisible(item, 48, 48);
    if (m_galleryHoverPath != path) {
        m_galleryHoverPath = path;
        viewport()->update();
    }
}

WorkspaceItemState ImageView::defaultStateForPath(const QString &path, int ordinal) const
{
    WorkspaceItemState s;
    s.path = path;
    s.pos = QPointF(40.0 * ordinal, 30.0 * ordinal);
    s.scale = 1.0;
    s.scaleY = 1.0;
    s.rotation = 0.0;
    s.opacity = 1.0;
    s.z = ordinal;
    return s;
}

QPointF ImageView::findEmptyPlacement(const QSizeF &itemSize) const
{
    const QRectF viewRect = mapToScene(viewport()->rect()).boundingRect();
    QSizeF size = itemSize;
    if (size.width() < 1.0 || size.height() < 1.0) {
        size = QSizeF(200.0, 200.0);
    }

    // Cap the collision footprint so huge images still leave room nearby
    const qreal maxEdge = qMax(120.0, qMin(viewRect.width(), viewRect.height()) * 0.45);
    const qreal longest = qMax(size.width(), size.height());
    if (longest > maxEdge) {
        const qreal f = maxEdge / longest;
        size = QSizeF(size.width() * f, size.height() * f);
    }

    const qreal gap = 32.0;
    auto overlaps = [&](const QPointF &centre) {
        const QRectF proposed(centre.x() - size.width() / 2.0 - gap,
                              centre.y() - size.height() / 2.0 - gap,
                              size.width() + 2.0 * gap,
                              size.height() + 2.0 * gap);
        for (ImageItem *item : m_items) {
            if (item->sceneBoundingRect().intersects(proposed)) {
                return true;
            }
        }
        return false;
    };

    QPointF candidate = viewRect.center();
    if (m_items.isEmpty() || !overlaps(candidate)) {
        return candidate;
    }

    // Spiral search around the viewport centre
    const qreal stepX = size.width() + gap;
    const qreal stepY = size.height() + gap;
    for (int ring = 1; ring <= 48; ++ring) {
        for (int dx = -ring; dx <= ring; ++dx) {
            for (int dy = -ring; dy <= ring; ++dy) {
                if (qMax(qAbs(dx), qAbs(dy)) != ring) {
                    continue;
                }
                candidate = viewRect.center() + QPointF(dx * stepX, dy * stepY);
                if (!overlaps(candidate)) {
                    return candidate;
                }
            }
        }
    }

    // Last resort: to the right of everything currently on the canvas
    const QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isValid()) {
        return QPointF(bounds.right() + gap + size.width() / 2.0, bounds.center().y());
    }
    return viewRect.center();
}

void ImageView::destroyCanvasItem(ImageItem *item)
{
    if (!item) {
        return;
    }
    // AUDIT H8/H9: clear every view-owned pointer before delete so paint /
    // input cannot touch a dangling ImageItem (BSP crashes in scene paint).
    if (item == m_dragItem) {
        m_dragItem = nullptr;
    }
    if (item == m_rotateItem) {
        m_rotateItem = nullptr;
        m_rotating = false;
    }
    if (item == m_handleDragItem) {
        m_handleDragItem = nullptr;
    }
    if (item == m_gallerySelectionAnchor) {
        m_gallerySelectionAnchor = nullptr;
    }
    rememberItemState(item);
    item->setSelected(false);
    m_items.removeOne(item);
    if (item->scene()) {
        item->scene()->removeItem(item);
    }
    delete item;
    // TransformCommand stores raw ImageItem*; drop undo history that would
    // redo/undo against a deleted object — unless a session-level command is
    // intentionally removing canvas tiles and must stay on the stack.
    if (m_undoStack && !m_preserveUndoOnDestroy) {
        m_undoStack->clear();
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}

void ImageView::updateWorkspaceSceneRect()
{
    if (!m_scene || !isWorkspaceMode()) {
        return;
    }
    QRectF bounds = m_scene->itemsBoundingRect();
    // Viewport in scene coordinates — ensure room to pan around content.
    const QRectF viewScene = mapToScene(viewport()->rect()).boundingRect();
    const qreal mx = qMax(96.0, viewScene.width() * 0.35);
    const qreal my = qMax(96.0, viewScene.height() * 0.35);
    if (!bounds.isValid() || bounds.isEmpty()) {
        bounds = viewScene.adjusted(-mx, -my, mx, my);
    } else {
        bounds.adjust(-mx, -my, mx, my);
        // Keep a viewport-sized halo so middle-drag can always move a little.
        bounds = bounds.united(viewScene.adjusted(-mx, -my, mx, my));
    }
    // Avoid feedback loops from tiny float noise.
    const QRectF cur = m_scene->sceneRect();
    if (qAbs(cur.left() - bounds.left()) < 1.0
        && qAbs(cur.top() - bounds.top()) < 1.0
        && qAbs(cur.width() - bounds.width()) < 1.0
        && qAbs(cur.height() - bounds.height()) < 1.0) {
        return;
    }
    m_scene->setSceneRect(bounds);
}

void ImageView::reorderItemsByPaths(const QStringList &paths)
{
    if (m_items.isEmpty() || paths.isEmpty()) {
        return;
    }
    QList<ImageItem *> ordered;
    ordered.reserve(m_items.size());
    QSet<ImageItem *> seen;
    for (const QString &path : paths) {
        if (ImageItem *item = findItemByPath(path)) {
            ordered.append(item);
            seen.insert(item);
        }
    }
    for (ImageItem *item : m_items) {
        if (!seen.contains(item)) {
            ordered.append(item);
        }
    }
    if (ordered != m_items) {
        m_items = ordered;
        for (int i = 0; i < m_items.size(); ++i) {
            m_items.at(i)->setStackZ(i);
        }
    }
}

void ImageView::setWorkspacePaths(const QStringList &paths)
{
    if (isImageMode()) {
        return;
    }

    QSet<QString> wanted(paths.begin(), paths.end());
    for (int i = m_items.size() - 1; i >= 0; --i) {
        ImageItem *item = m_items.at(i);
        if (!wanted.contains(item->path())) {
            m_galleryDecodeScheduled.remove(item->path());
            m_galleryDecodeFailed.remove(item->path());
            destroyCanvasItem(item);
        }
    }

    m_pathOrder = paths;

    const bool virtualize = isGalleryMode() && paths.size() >= kGalleryVirtualThreshold;

    for (const QString &path : paths) {
        if (findItemByPath(path)) {
            continue;
        }
        if (virtualize) {
            // Fast size probe + placeholder; full decode is viewport-windowed.
            createPlaceholderItem(path, probeImageSize(path));
        } else {
            scheduleImageLoad(path, LoadAdd);
        }
    }

    // Keep canvas order aligned with session/sort order (not async load order).
    reorderItemsByPaths(m_pathOrder);

    // Workspace: seed a selection if empty. Gallery must not steal focus to
    // "last item" on layout switch / path refresh (preserves multi-select).
    if (isWorkspaceMode() && m_scene->selectedItems().isEmpty() && !m_items.isEmpty()) {
        m_items.last()->setSelected(true);
    }

    if (isGalleryMode() && !m_items.isEmpty()) {
        applyLayout();
        updateGalleryDecodeWindow();
    }

    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::updateGalleryDecodeWindow()
{
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }

    const bool virtualize = m_items.size() >= kGalleryVirtualThreshold
                            || m_pathOrder.size() >= kGalleryVirtualThreshold;

    // Visible-first priority, then the rest of the session. Never unload decoded
    // tiles — clearing pixels while a job was in flight left permanent placeholders
    // and size/scale mismatches broke pack geometry.
    QStringList visible;
    QStringList rest;
    if (virtualize) {
        const QRect viewRect = viewport()->rect().adjusted(
            -kGalleryDecodeOverscanPx, -kGalleryDecodeOverscanPx,
            kGalleryDecodeOverscanPx, kGalleryDecodeOverscanPx);
        const QRectF sceneVisible = mapToScene(viewRect).boundingRect();
        for (ImageItem *item : m_items) {
            if (!item || item->hasDecodedPixels()
                || m_galleryDecodeFailed.contains(item->path())) {
                continue;
            }
            const QRectF tile = item->contentSceneRect();
            if (tile.isNull() || tile.intersects(sceneVisible)) {
                visible.append(item->path());
            } else {
                rest.append(item->path());
            }
        }
    } else {
        for (ImageItem *item : m_items) {
            if (!item || item->hasDecodedPixels()
                || m_galleryDecodeFailed.contains(item->path())) {
                continue;
            }
            visible.append(item->path());
        }
    }

    for (const QString &path : visible) {
        scheduleGalleryDecode(path);
    }
    for (const QString &path : rest) {
        scheduleGalleryDecode(path);
    }
    emit statusChanged();
}


void ImageView::removeWorkspacePath(const QString &path)
{
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    m_pendingWorkspacePaths.remove(path);
    m_pendingScenePos.remove(path);
    m_galleryDecodeScheduled.remove(path);
    m_galleryDecodeFailed.remove(path);
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::prepareImageModeCanvas()
{
    m_undoStack->clear();
    m_scene->clearSelection();
    resetTransform();
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setValue(0);
    }
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(0);
    }
    // Drop large Gallery/Workspace scene rects so fitInView centres cleanly.
    m_scene->setSceneRect(QRectF());
    m_fitMode = true;
    m_fillMode = false;
}

void ImageView::prepareGalleryCanvas()
{
    // Drop Image-mode fit transforms and prior layout scene rects so the previous
    // frame does not linger under the new packing (visible "ghost" between switches).
    m_undoStack->clear();
    m_scene->clearSelection();
    resetTransform();
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setValue(0);
    }
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(0);
    }
    m_scene->setSceneRect(QRectF());
    m_fitMode = true;
    m_fillMode = false;
    // Force a blank pass before items are re-packed.
    viewport()->update();
}

void ImageView::setViewMode(ViewMode mode)
{

    if (mode == m_viewMode) {
        return;
    }

    const ViewMode previous = m_viewMode;
    if (previous == ViewMode::Gallery) {
        m_galleryHoverPath.clear();
        m_gallerySelectionAnchor = nullptr;
        setDragMode(QGraphicsView::NoDrag);
        // Stop deferred packs immediately — a pending 0ms debounce after
        // scrollbar/thumb resize must not re-enter applyLayout while we tear down.
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
        m_pendingGalleryRestore = false;
        m_haveGalleryScroll = false;
        m_haveGalleryViewCenter = false;
        m_applyingLayout = false;
    }

    if (previous == ViewMode::Workspace && mode != ViewMode::Workspace) {
        snapshotWorkspace();
    }

    if (mode == ViewMode::Image) {
        if (previous == ViewMode::Gallery) {
            // Keep tiles + decoded pixels for a fast return to Gallery.
            stashGalleryItems();
        }
        m_viewMode = ViewMode::Image;
        m_layoutMode = LayoutMode::FreeForm;
        viewport()->update();
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
        m_applyingLayout = false;
        prepareImageModeCanvas();
        // Prefer explicit classic path. Do not pick m_items.first() when leaving
        // Gallery — that re-decodes a random tile before setCurrentIndex loads
        // the real target (double clear + decode spike).
        const QString path = m_classicPath;
        // Clear live canvas only — do not discardStashedGallery().
        m_handleDragItem = nullptr;
        m_rotateItem = nullptr;
        m_rotating = false;
        m_dragItem = nullptr;
        m_gallerySelectionAnchor = nullptr;
        if (m_undoStack) {
            m_undoStack->clear();
        }
        m_items.clear();
        m_pendingScenePos.clear();
        m_pendingWorkspacePaths.clear();
        m_pendingRestoreStates.clear();
        m_classicPath.clear();
        if (m_scene) {
            m_scene->clear();
        }
        m_mouseInfo = {};
        emit mouseInfoChanged(m_mouseInfo);
        if (!path.isEmpty()) {
            scheduleImageLoad(path, LoadReplace);
        }
        emit statusChanged();
        return;
    }

    if (mode == ViewMode::Workspace) {
        m_viewMode = ViewMode::Workspace;
        m_layoutMode = LayoutMode::FreeForm;
        viewport()->update();
        setDragMode(m_tool == Tool::Select ? QGraphicsView::RubberBandDrag
                                           : QGraphicsView::NoDrag);
        if (!m_savedWorkspace.isEmpty() && previous == ViewMode::Image) {
            restoreWorkspace();
        } else {
            for (ImageItem *item : m_items) {
                applyItemModeFlags(item);
            }
            if (!m_items.isEmpty()) {
                m_scene->clearSelection();
                m_items.first()->setSelected(true);
            }
        }
        updateWorkspaceSceneRect();
        emit statusChanged();
        return;
    }

    // Gallery
    prepareGalleryCanvas();
    m_viewMode = ViewMode::Gallery;
    if (m_layoutMode == LayoutMode::FreeForm) {
        m_layoutMode = LayoutMode::Masonry;
    }
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
        item->setItemOpacity(1.0);
        item->setItemRotation(0.0);
    }
    if (!m_items.isEmpty()) {
        applyLayout();
    }
    emit statusChanged();
}

void ImageView::enterGallery(LayoutMode packagedLayout)
{
    if (packagedLayout == LayoutMode::FreeForm) {
        packagedLayout = LayoutMode::Masonry;
    }
    // Preserve multi-select when only switching Gallery layout (not entering
    // from Image/Workspace — prepareGalleryCanvas clears selection).
    QStringList selectedPaths;
    QString anchorPath;
    const bool layoutSwitch = isGalleryMode();

    // Returning from Image: reattach cached tiles before packing.
    if (!layoutSwitch && !m_stashedGalleryItems.isEmpty()) {
        restoreStashedGalleryItems();
    }

    if (layoutSwitch && m_scene) {
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                selectedPaths.append(ii->path());
            }
        }
        if (m_gallerySelectionAnchor) {
            anchorPath = m_gallerySelectionAnchor->path();
        }
    }

    if (m_viewMode == ViewMode::Workspace) {
        snapshotFreeFormStates();
        snapshotWorkspace();
    }
    if (layoutSwitch) {
        // Soft reset: keep items and selection paths; only clear view zoom.
        // Drop scroll snapshot — user asked for a new layout, not return-from-Image.
        m_haveGalleryScroll = false;
        m_haveGalleryViewCenter = false;
        m_pendingGalleryRestore = false;
        resetTransform();
        m_fitMode = true;
        m_fillMode = false;
    } else {
        // Clear residual Image/Workspace view state before packing.
        prepareGalleryCanvas();
    }
    m_viewMode = ViewMode::Gallery;
    m_layoutMode = packagedLayout;
    if (!layoutSwitch) {
        m_gallerySelectionAnchor = nullptr;
    }
    setDragMode(QGraphicsView::RubberBandDrag);
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
        item->setItemOpacity(1.0);
        // Entering Gallery from Image/Workspace: upright overview. Switching
        // layout inside Gallery keeps user rotate/flip on the tiles.
        if (!layoutSwitch) {
            item->setItemRotation(0.0);
            item->setItemHFlip(false);
            item->setItemVFlip(false);
        }
    }
    applyLayout();

    if (layoutSwitch && !selectedPaths.isEmpty()) {
        m_scene->clearSelection();
        for (const QString &path : selectedPaths) {
            if (ImageItem *item = findItemByPath(path)) {
                item->setSelected(true);
            }
        }
        m_gallerySelectionAnchor = anchorPath.isEmpty()
            ? nullptr
            : findItemByPath(anchorPath);
    }

    emit statusChanged();
}

void ImageView::snapshotGalleryViewport()
{
    if (!isGalleryMode()) {
        return;
    }
    // Scene centre is robust across ScrollBar AlwaysOn/Off and pack rebuilds;
    // raw scrollbar values are not (policy change zeroes the range).
    m_galleryViewCenter = mapToScene(viewport()->rect().center());
    m_haveGalleryViewCenter = true;
    if (horizontalScrollBar()) {
        m_galleryScrollH = horizontalScrollBar()->value();
    }
    if (verticalScrollBar()) {
        m_galleryScrollV = verticalScrollBar()->value();
    }
    m_haveGalleryScroll = true;
    if (ImageItem *sel = targetItem()) {
        m_galleryFocusPath = sel->path();
    }
}

void ImageView::restoreGalleryViewport(const QString &focusPath)
{
    if (!focusPath.isEmpty()) {
        m_galleryFocusPath = focusPath;
    }
    m_pendingGalleryRestore = true;
    // Try immediately if already in Gallery with items; otherwise applyLayout
    // will re-apply after each pack while pending stays true.
    if (isGalleryMode()) {
        applyPendingGalleryRestore();
    }
}

void ImageView::applyPendingGalleryRestore()
{
    if (!m_pendingGalleryRestore || !isGalleryMode()) {
        return;
    }

    // Prefer scene-centre restore (survives scrollbar policy / range rebuild).
    if (m_haveGalleryViewCenter) {
        centerOn(m_galleryViewCenter);
    } else if (m_haveGalleryScroll) {
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(m_galleryScrollH);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(m_galleryScrollV);
        }
    }

    ImageItem *focus = nullptr;
    if (!m_galleryFocusPath.isEmpty()) {
        focus = findItemByPath(m_galleryFocusPath);
    }
    if (focus) {
        focus->setSelected(true);
        const QRectF viewScene = mapToScene(viewport()->rect()).boundingRect();
        if (!viewScene.intersects(focus->sceneBoundingRect())) {
            ensureVisible(focus, 48, 48);
        }
        if (m_galleryHoverPath != focus->path()) {
            m_galleryHoverPath = focus->path();
            viewport()->update();
        }
    }

    // Stay pending while loads complete — each applyLayout would otherwise
    // centerOn(0,0) and wipe the restored position.
    if (m_pendingWorkspacePaths.isEmpty() && !m_items.isEmpty()) {
        m_pendingGalleryRestore = false;
    }
}

bool ImageView::addImage(const QString &path)
{
    if (isImageMode()) {
        return false;
    }

    if (ImageItem *existing = findItemByPath(path)) {
        m_scene->clearSelection();
        existing->setSelected(true);
        ensureVisibleItem(existing);
        emit statusChanged();
        return true;
    }

    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}

bool ImageView::addImageAt(const QString &path, const QPointF &scenePos)
{
    if (path.isEmpty()) {
        return false;
    }
    m_pendingScenePos.insert(path, scenePos);
    return addImage(path);
}

bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos)
{
    if (path.isEmpty() || isImageMode()) {
        return false;
    }
    if (ImageItem *existing = findItemByPath(path)) {
        existing->setPos(scenePos);
        if (m_scene) {
            m_scene->clearSelection();
        }
        existing->setSelected(true);
        updateWorkspaceSceneRect();
        ensureVisibleItem(existing);
        emit statusChanged();
        return true;
    }
    return addImageAt(path, scenePos);
}


ImageItem *ImageView::primaryItem() const
{
    if (m_items.isEmpty()) {
        return nullptr;
    }
    return m_items.first();
}

ImageItem *ImageView::targetItem() const
{
    // Transform targets:
    //   Image → primary (sole) canvas object
    //   Gallery / Workspace → first selected item; Workspace also falls back to
    //   the sole object when the selection is empty
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            return item;
        }
    }
    if (isImageMode() || m_items.size() == 1) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    return nullptr;
}

QList<ImageItem *> ImageView::transformTargets() const
{
    QList<ImageItem *> out;
    if (!m_scene) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            out.append(item);
        }
    }
    if (!out.isEmpty()) {
        return out;
    }
    if (isImageMode() || m_items.size() == 1) {
        if (!m_items.isEmpty()) {
            out.append(m_items.first());
        }
    }
    return out;
}

bool ImageView::hasTransformTargets() const
{
    return !transformTargets().isEmpty();
}

QSizeF ImageView::nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    return QSizeF(item->pixmap().size());
}

void ImageView::snapshotFreeFormStates()
{
    m_freeFormStates.clear();
    for (ImageItem *item : m_items) {
        m_freeFormStates.insert(item->path(), captureState(item));
    }
    m_freeFormViewTransform = transform();
    m_hasFreeFormViewTransform = true;
}

void ImageView::restoreFreeFormStates()
{
    for (ImageItem *item : m_items) {
        const auto it = m_freeFormStates.constFind(item->path());
        if (it != m_freeFormStates.constEnd()) {
            applyState(item, *it);
            m_itemStates.insert(item->path(), *it);
        }
    }
    if (m_hasFreeFormViewTransform) {
        setTransform(m_freeFormViewTransform);
    }
}

void ImageView::setLayoutMode(LayoutMode mode)
{
    // Packaged layouts belong only to Gallery; FreeForm only to Workspace.
    if (mode == LayoutMode::FreeForm) {
        if (!isWorkspaceMode()) {
            return;
        }
        if (m_layoutMode != LayoutMode::FreeForm) {
            // Should not happen in Workspace (always FreeForm).
        }
        m_layoutMode = LayoutMode::FreeForm;
        for (ImageItem *item : m_items) {
            applyItemModeFlags(item);
        }
        restoreFreeFormStates();
        if (!m_items.isEmpty()) {
            m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-64, -64, 64, 64));
        }
        m_fitMode = false;
        emit statusChanged();
        return;
    }

    // Packaged layout → Gallery only (enterGallery if needed).
    if (!isGalleryMode()) {
        enterGallery(mode);
        return;
    }

    if (m_layoutMode == LayoutMode::FreeForm && mode != LayoutMode::FreeForm) {
        snapshotFreeFormStates();
    }

    m_layoutMode = mode;
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
    }
    applyLayout();
}

void ImageView::setGridColumns(int columns)
{
    const int clamped = qMax(0, columns); // 0 = automatic
    if (clamped == m_gridColumns) {
        return;
    }
    m_gridColumns = clamped;
    if (isGalleryMode()
        && (m_layoutMode == LayoutMode::Grid || m_layoutMode == LayoutMode::GridCrop)) {
        applyLayout();
    }
}

void ImageView::setMasonryColumns(int columns)
{
    const int clamped = qBound(1, columns, 32);
    if (clamped == m_masonryColumns) {
        return;
    }
    m_masonryColumns = clamped;
    if (m_layoutMode == LayoutMode::Masonry && !m_items.isEmpty()) {
        applyLayout();
    }
}

void ImageView::setMasonryRows(int rows)
{
    const int clamped = qBound(1, rows, 32);
    if (clamped == m_masonryRows) {
        return;
    }
    m_masonryRows = clamped;
    if (m_layoutMode == LayoutMode::MasonryRows && !m_items.isEmpty()) {
        applyLayout();
    }
}

void ImageView::scheduleApplyLayout()
{
    if (!m_layoutDebounceTimer) {
        applyLayout();
        return;
    }
    m_layoutDebounceTimer->start();
}

void ImageView::applyLayout()
{
    if (m_applyingLayout) {
        return;
    }
    // Packaged packing is Gallery-only; never rearrange Workspace free-form items.
    if (!isGalleryMode() || m_items.isEmpty() || m_layoutMode == LayoutMode::FreeForm) {
        return;
    }

    if (!m_pathOrder.isEmpty()) {
        reorderItemsByPaths(m_pathOrder);
    }

    m_applyingLayout = true;

    // Packaged layouts use view pixels as scene units so images scale to the window
    resetTransform();
    if (!m_pendingGalleryRestore) {
        centerOn(0, 0);
    }

    const qreal margin = 16.0;
    const qreal gap = 12.0;
    const qreal availW = qMax(32.0, static_cast<qreal>(viewport()->width()) - 2.0 * margin);
    const qreal availH = qMax(32.0, static_cast<qreal>(viewport()->height()) - 2.0 * margin);

    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_masonryColumns;
    params.gridColumns = m_gridColumns;
    params.masonryRows = m_masonryRows;
    switch (m_layoutMode) {
    case LayoutMode::SideBySide:
        params.mode = GalleryLayout::Mode::SideBySide;
        break;
    case LayoutMode::Vertical:
        params.mode = GalleryLayout::Mode::Vertical;
        break;
    case LayoutMode::Grid:
        params.mode = GalleryLayout::Mode::Grid;
        break;
    case LayoutMode::GridCrop:
        params.mode = GalleryLayout::Mode::GridCrop;
        break;
    case LayoutMode::Masonry:
        params.mode = GalleryLayout::Mode::Masonry;
        break;
    case LayoutMode::MasonryRows:
        params.mode = GalleryLayout::Mode::MasonryRows;
        break;
    default:
        params.mode = GalleryLayout::Mode::Masonry;
        break;
    }

    GalleryLayout::pack(m_items, params, [this](ImageItem *item) {
        m_itemStates.insert(item->path(), captureState(item));
    });

    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
    // Never call setHorizontalScrollBarPolicy here — it resizes the viewport and
    // re-enters via resizeEvent (stack overflow). Policy is owned by MainWindow.
    if (m_scene->sceneRect() != bounds) {
        m_scene->setSceneRect(bounds);
    }
    m_fitMode = true;
    // Keep the guard until after statusChanged so slots cannot re-enter layout.
    emit statusChanged();
    m_applyingLayout = false;
    // Re-apply scroll after centerOn(0,0) above when returning from Image.
    applyPendingGalleryRestore();
    updateGalleryDecodeWindow();
}

