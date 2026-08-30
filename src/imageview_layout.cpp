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
    for (const WorkspaceItemState &state : m_savedWorkspace) {
        m_itemStates.insert(state.path, state);
        scheduleImageLoad(state.path, LoadRestore);
    }
    m_fitMode = false;
    emit statusChanged();
}

void ImageView::clearWorkspace()
{
    m_rotateItem = nullptr;
    m_rotating = false;
    m_dragItem = nullptr;
    m_items.clear();
    m_pendingScenePos.clear();
    m_pendingWorkspacePaths.clear();
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

void ImageView::setWorkspacePaths(const QStringList &paths)
{
    if (isImageMode()) {
        return;
    }

    // Remember state of items that will be removed
    QSet<QString> wanted(paths.begin(), paths.end());
    for (int i = m_items.size() - 1; i >= 0; --i) {
        ImageItem *item = m_items.at(i);
        if (!wanted.contains(item->path())) {
            if (item == m_dragItem) {
                m_dragItem = nullptr;
            }
            if (item == m_rotateItem) {
                m_rotateItem = nullptr;
                m_rotating = false;
            }
            rememberItemState(item);
            m_scene->removeItem(item);
            m_items.removeAt(i);
            delete item;
        }
    }

    // Load missing paths off the GUI thread
    for (const QString &path : paths) {
        if (findItemByPath(path)) {
            continue;
        }
        scheduleImageLoad(path, LoadAdd);
    }

    // Select something if nothing selected
    if (m_scene->selectedItems().isEmpty() && !m_items.isEmpty()) {
        m_items.last()->setSelected(true);
    }

    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::removeWorkspacePath(const QString &path)
{
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    rememberItemState(item);
    m_pendingWorkspacePaths.remove(path);
    m_items.removeOne(item);
    m_scene->removeItem(item);
    delete item;
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

    if (previous == ViewMode::Workspace && mode != ViewMode::Workspace) {
        snapshotWorkspace();
    }

    if (mode == ViewMode::Image) {
        m_viewMode = ViewMode::Image;
        m_layoutMode = LayoutMode::FreeForm;
        viewport()->update();
        prepareImageModeCanvas();
        const QString path = !m_classicPath.isEmpty()
                                 ? m_classicPath
                                 : (m_items.isEmpty() ? QString() : m_items.first()->path());
        clearWorkspace();
        m_pendingWorkspacePaths.clear();
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

void ImageView::setWorkspaceMode(bool on)
{
    setViewMode(on ? ViewMode::Workspace : ViewMode::Image);
}

void ImageView::enterGallery(LayoutMode packagedLayout)
{
    if (packagedLayout == LayoutMode::FreeForm) {
        packagedLayout = LayoutMode::Masonry;
    }
    if (m_viewMode == ViewMode::Workspace) {
        snapshotFreeFormStates();
        snapshotWorkspace();
    }
    // Clear residual Image/Workspace view state before packing so the old
    // composition is not left painted behind the new layout.
    prepareGalleryCanvas();
    m_viewMode = ViewMode::Gallery;
    m_layoutMode = packagedLayout;
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
        // Pack also resets these; do it here so a delayed layout still looks clean.
        item->setItemOpacity(1.0);
        item->setItemRotation(0.0);
    }
    applyLayout();
    emit statusChanged();
}

void ImageView::snapshotGalleryViewport()
{
    if (!isGalleryMode()) {
        return;
    }
    if (horizontalScrollBar()) {
        m_galleryScrollH = horizontalScrollBar()->value();
    }
    if (verticalScrollBar()) {
        m_galleryScrollV = verticalScrollBar()->value();
    }
    m_haveGalleryScroll = true;
    // Prefer current selection as focus for return highlight
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
    // Try immediately if items already present (async loads will retry via applyLayout).
    applyPendingGalleryRestore();
}

void ImageView::applyPendingGalleryRestore()
{
    if (!m_pendingGalleryRestore || !isGalleryMode()) {
        return;
    }
    ImageItem *focus = nullptr;
    if (!m_galleryFocusPath.isEmpty()) {
        focus = findItemByPath(m_galleryFocusPath);
    }
    if (focus) {
        m_scene->clearSelection();
        focus->setSelected(true);
        // Scroll first to snapshot, then ensureVisible so the item is in view.
        if (m_haveGalleryScroll) {
            if (horizontalScrollBar()) {
                horizontalScrollBar()->setValue(m_galleryScrollH);
            }
            if (verticalScrollBar()) {
                verticalScrollBar()->setValue(m_galleryScrollV);
            }
        }
        ensureVisible(focus, 48, 48);
        m_pendingGalleryRestore = false;
        return;
    }
    // Items still loading: restore scroll alone so the view is not jumped to origin.
    if (m_haveGalleryScroll && !m_items.isEmpty()) {
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(m_galleryScrollH);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(m_galleryScrollV);
        }
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


ImageItem *ImageView::primaryItem() const
{
    if (m_items.isEmpty()) {
        return nullptr;
    }
    return m_items.first();
}

ImageItem *ImageView::targetItem() const
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            return item;
        }
    }
    if (m_items.size() == 1) {
        return m_items.first();
    }
    return nullptr;
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

    m_applyingLayout = true;

    // Packaged layouts use view pixels as scene units so images scale to the window
    resetTransform();
    centerOn(0, 0);

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
    applyPendingGalleryRestore();
}

