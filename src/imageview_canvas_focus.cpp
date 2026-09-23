// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas focus, reveal, destroy, and Workspace scene placement
// (split from imageview_canvas.cpp).

#include <cstdio>
#include <cstdlib>
#include "imageview.h"
#include <QScrollBar>
#include <QtMath>
#include "workspace/workspacegeometry.h"
#include "viewtransform.h"
#include "session/sessionappearance.h"
#include "ttfp_trace.h"
#include "display/imagecache.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QSet>
#include <QDebug>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>
#include <QGraphicsItem>

// --- Focus / destroy (was imageview_canvas_focus.cpp) ---

ImageItem *ImageView::primaryItem() const
{
    // Image mode: prefer the tile bound to the current SessionImageId so
    // duplicate paths do not resolve to the wrong live item via first-in-list.
    if (isImageMode()) {
        const SessionImageId sid = m_sessionId.currentIdValue();
        if (sid != kInvalidSessionImageId) {
            if (ImageItem *byId = findItemBySessionId(sid)) {
                return byId;
            }
        }
    }
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
    if (!m_scene) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            // Selection can briefly hold stale pointers after destroyCanvasItem.
            if (!m_items.contains(item) || item->scene() != m_scene) {
                continue;
            }
            return item;
        }
    }
    if (isImageMode() || m_items.size() == 1) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    return nullptr;
}



// --- from imageview_layout.cpp (canvas) ---










int ImageView::workspacePathOccurrenceCount(const QString &path) const
{
    int n = 0;
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            ++n;
        }
    }
    return n;
}




void ImageView::focusGalleryItem(ImageItem *item)
{
    if (!item || !m_scene) {
        return;
    }
    m_scene->clearSelection();
    item->setSelected(true);
    if (!isGalleryMode()) {
        return;
    }
    // Open/TTFP: first index is often already in view after pack — ensureVisible
    // on a large scene was hundreds of ms for no visual change.
    bool needScroll = true;
    if (viewport()) {
        const QRectF vis = mapToScene(viewport()->rect()).boundingRect();
        if (vis.isValid() && item->sceneBoundingRect().intersects(vis)) {
            needScroll = false;
        }
    }
    if (needScroll) {
        ensureVisible(item, ViewTransform::kEnsureVisibleMargin, ViewTransform::kEnsureVisibleMargin);
    }
    // Keyboard focus: show filename in the HUD like mouse hover.
    const QString path = item->path();
    if (m_gallery.hoverPath() != path) {
        m_gallery.setHoverPath(path);
        if (viewport()) {
            viewport()->update();
        }
    }
}

void ImageView::focusSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    focusGalleryItem(findItemBySessionId(sessionId));
}

void ImageView::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Before clearSelection so a selected duplicate wins over first-match.
    focusGalleryItem(findItemForPath(path));
}


void ImageView::revealGalleryPath(const QString &path)
{
    if (path.isEmpty() || !isGalleryMode()) {
        return;
    }
    // Prefer the selected instance of this path when duplicates exist (LoadAdd).
    ImageItem *item = findItemForPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    ensureVisible(item, ViewTransform::kEnsureVisibleMargin, ViewTransform::kEnsureVisibleMargin);
    if (m_gallery.hoverPath() != path) {
        m_gallery.setHoverPath(path);
        viewport()->update();
    }
}

void ImageView::revealGallerySessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId || !isGalleryMode()) {
        return;
    }
    ImageItem *item = findItemBySessionId(sessionId);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    ensureVisible(item, ViewTransform::kEnsureVisibleMargin, ViewTransform::kEnsureVisibleMargin);
    const QString path = item->path();
    if (!path.isEmpty() && m_gallery.hoverPath() != path) {
        m_gallery.setHoverPath(path);
        viewport()->update();
    }
}


void ImageView::destroyCanvasItem(ImageItem *item, bool persistState)
{
    if (!item) {
        return;
    }
    const QString path = item->path();
    // Stage 2: drop tile session and release pipeline-owned bag before delete.
    m_displayPipeline.dropItemTileLodSession(item);
    m_displayPipeline.releaseTileBag(item);
    m_displayPipeline.unregisterItemDisplaySurface(item);
    // Re-entrancy / double-destroy: after the first call the pointer is gone from
    // live and stash lists. A second call must not touch a deleted QGraphicsItem
    // (seen as SIGSEGV in QObject::blockSignals on a garbage scene pointer).
    const bool inLive = m_items.contains(item);
    const bool inGalleryStash = m_gallery.stashedItems().contains(item);
    const bool inWorkspaceStash = m_workspace.stashedItems().contains(item);
    if (!inLive && !inGalleryStash && !inWorkspaceStash) {
        return;
    }
    // AUDIT H8/H9: clear every view-owned pointer before delete so paint /
    // input cannot touch a dangling ImageItem (BSP crashes in scene paint).
    m_itemInteract.dropIfItem(item);
    if (item == m_gallery.selectionAnchor()) {
        m_gallery.setSelectionAnchor(nullptr);
    }
    // Group scale holds raw pointers — drop before delete or BSP paint UAF.
    if (m_groupXform.isScaleDrag() || m_groupXform.isRotateDrag() || m_groupXform.hasDragItems()) {
        m_groupXform.endDrag();
    }
    // Also drop from gallery stash so discardStashedGallery cannot double-free.
    m_gallery.stashedItems().removeAll(item);
    m_workspace.stashedItems().removeAll(item);

    // Default: snapshot bound appearance/pose before the tile is gone.
    // Session-id *delete* paths pass persistState=false after removeAppearance
    // so this cannot re-insert the DTO that was just cleared.
    if (persistState) {
        rememberItemState(item);
    }
    m_items.removeAll(item);
    // Off-canvas neighbor prefetch may still hold a controller for this path.
    if (!path.isEmpty() && !pathOnLiveCanvas(path)) {
        m_tileNeighborPrefetch.dropPath(path);
    }
    if (QGraphicsScene *sc = item->scene()) {
        // selectionChanged → statusChanged → paint must not run mid-teardown
        // (re-entrant paint was UAF in the BSP / item lists).
        const bool blocked = sc->blockSignals(true);
        item->setSelected(false);
        sc->removeItem(item);
        sc->blockSignals(blocked);
    } else {
        item->setSelected(false);
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






// --- session remove / bind residual from layout ---












// --- canvas clipboard / session place-remove (from transform) ---

// --- Workspace scene / placement (was imageview_workspace.cpp) ---

bool ImageView::hasWorkspaceContent() const
{
    if (!m_items.isEmpty()) {
        return true;
    }
    if (!m_workspace.stashedItems().isEmpty()) {
        return true;
    }
    return !m_workspace.savedItems().isEmpty();
}








void ImageView::updateWorkspaceSceneRect()
{
    if (!m_scene || !isWorkspaceMode()) {
        return;
    }
    QRectF bounds = m_scene->itemsBoundingRect();
    if (m_pageGuide.isVisible()) {
        bounds = bounds.united(pageGuideSceneRect());
    }
    // Viewport in scene coordinates — ensure room to pan around content.
    const QRectF viewScene = mapToScene(viewport()->rect()).boundingRect();
    const QSizeF margins = WorkspaceGeometry::sceneMargins(viewScene.size());
    const qreal mx = margins.width();
    const qreal my = margins.height();
    if (!bounds.isValid() || bounds.isEmpty()) {
        bounds = WorkspaceGeometry::paddedSceneRect(viewScene);
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

QPointF ImageView::findEmptyPlacement(const QSizeF &itemSize) const
{
    const QRectF viewRect = mapToScene(viewport()->rect()).boundingRect();
    QSizeF size = itemSize;
    if (size.width() < 1.0 || size.height() < 1.0) {
        size = QSizeF(200.0, 200.0);
    }

    // Cap the collision footprint so huge images still leave room nearby
    const qreal maxEdge = WorkspaceGeometry::placementMaxEdge(
        viewRect.width(), viewRect.height());
    size = WorkspaceGeometry::cappedFootprint(size, maxEdge);

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
