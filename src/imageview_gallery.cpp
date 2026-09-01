// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"

#include <QScrollBar>
#include <QSet>

void ImageView::discardStashedGallery()
{
    // Take ownership first so re-entrant callers (and double-discard) see an
    // empty list. Duplicates in the list would otherwise double-free.
    QList<ImageItem *> doomed = m_stashedGalleryItems;
    m_stashedGalleryItems.clear();
    m_stashedGalleryPathOrder.clear();
    QSet<ImageItem *> seen;
    for (ImageItem *item : doomed) {
        if (!item || seen.contains(item)) {
            continue;
        }
        seen.insert(item);
        if (QGraphicsScene *sc = item->scene()) {
            sc->removeItem(item);
        }
        delete item;
    }
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

    reassertGalleryViewport();

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

void ImageView::reassertGalleryViewport()
{
    if (!isGalleryMode()) {
        return;
    }
    if (m_haveGalleryViewCenter) {
        centerOn(m_galleryViewCenter);
    }
    if (m_haveGalleryScroll) {
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(m_galleryScrollH);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(m_galleryScrollV);
        }
    }
}

void ImageView::leaveForImageMode()
{
    // Snapshot must run while still in Gallery (scrollbars + scene centre valid).
    if (isGalleryMode()) {
        snapshotGalleryViewport();
    }
    // Workspace: setViewMode snapshots + stashes free-form tiles.
    setViewMode(ViewMode::Image);
}

void ImageView::returnToGalleryFromImage(LayoutMode layout, const QString &focusPath)
{
    // Arm restore before enterGallery/applyLayout so packs re-centre on the
    // snapshotted scene point (flags preserved across Gallery→Image leave).
    restoreGalleryViewport(focusPath);
    enterGallery(layout);
    applyPendingGalleryRestore();
}

void ImageView::returnToWorkspaceFromImage()
{
    setViewMode(ViewMode::Workspace);
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
        // Only trust items we still own — selection can briefly hold stale
        // pointers after session deletes that skipped destroyCanvasItem.
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            auto *ii = qgraphicsitem_cast<ImageItem *>(gi);
            if (!ii || !m_items.contains(ii) || ii->scene() != m_scene) {
                continue;
            }
            selectedPaths.append(ii->path());
        }
        if (m_gallerySelectionAnchor
            && m_items.contains(m_gallerySelectionAnchor)
            && m_gallerySelectionAnchor->scene() == m_scene) {
            anchorPath = m_gallerySelectionAnchor->path();
        } else {
            m_gallerySelectionAnchor = nullptr;
        }
    }

    if (m_viewMode == ViewMode::Workspace) {
        snapshotFreeFormStates();
        snapshotWorkspace();
    }
    // Leaving Workspace/Image for Gallery: drop workspace stash (layout uses
    // live m_items or rebuilds from session paths).
    discardStashedWorkspace();
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
        if (!item) {
            continue;
        }
        applyItemModeFlags(item);
        item->setItemOpacity(1.0);
        // Entering Gallery from Image/Workspace: upright overview. Switching
        // layout inside Gallery keeps user content transforms on the tiles.
        if (!layoutSwitch) {
            item->setItemRotation(0.0);
            item->setItemHFlip(false);
            item->setItemVFlip(false);
        }
    }
    // Explicit layout action: pack only live items (drop stale path-order holes).
    if (layoutSwitch) {
        QStringList livePaths;
        livePaths.reserve(m_items.size());
        for (ImageItem *item : m_items) {
            if (item) {
                livePaths.append(item->path());
            }
        }
        if (!livePaths.isEmpty()) {
            m_pathOrder = livePaths;
        }
    }
    applyLayout(GalleryPackReason::EnterGallery);

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
