// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"

#include <QScrollBar>
#include <QtMath>

void ImageView::snapshotWorkspace()
{
    m_savedWorkspace.clear();
    for (ImageItem *item : m_items) {
        const WorkspaceItemState s = captureState(item);
        m_savedWorkspace.append(s);
        // Path map is session/Image appearance only; unbound duplicates stay
        // in the list and must not collapse into a single path entry.
        if (s.sessionId != kInvalidSessionImageId) {
            m_appearance.set(s.sessionId, s);
        }
        if (s.sessionIndex >= 0) {
            m_itemStates.insert(s.path, s);
        }
    }
    // Durable view backup when the live stash is later discarded (e.g. Gallery).
    m_savedWorkspaceViewTransform = transform();
    m_savedWorkspaceViewCenter = mapToScene(viewport()->rect().center());
    m_hasSavedWorkspaceView = true;
}

void ImageView::restoreWorkspace()
{
    clearWorkspace();
    m_pendingWorkspacePaths.clear();
    // Merge session appearance (crop / flip / orientation) from the live map into
    // the durable snapshot so Image-mode edits survive a full rebuild.
    for (WorkspaceItemState &slot : m_savedWorkspace) {
        if (slot.sessionId != kInvalidSessionImageId) {
            if (const WorkspaceItemState *sit = m_appearance.get(slot.sessionId)) {
                slot.hasCrop = sit->hasCrop;
                slot.cropRect = sit->cropRect;
                slot.hFlip = sit->hFlip;
                slot.vFlip = sit->vFlip;
                slot.contentQuarterTurns = sit->contentQuarterTurns;
                slot.contentHFlip = sit->contentHFlip;
                slot.contentVFlip = sit->contentVFlip;
                continue;
            }
        }
        const auto it = m_itemStates.constFind(slot.path);
        if (it == m_itemStates.cend()) {
            continue;
        }
        // Path-level appearance only for matching bound session slots (legacy).
        if (slot.sessionIndex < 0 || it->sessionIndex < 0
            || slot.sessionIndex != it->sessionIndex) {
            continue;
        }
        slot.hasCrop = it->hasCrop;
        slot.cropRect = it->cropRect;
        slot.hFlip = it->hFlip;
        slot.vFlip = it->vFlip;
        slot.contentQuarterTurns = it->contentQuarterTurns;
        slot.contentHFlip = it->contentHFlip;
        slot.contentVFlip = it->contentVFlip;
        slot.orientation = 0.0;
    }
    // AUDIT M27: queue every saved state (including duplicate paths) then load.
    m_pendingRestoreStates = m_savedWorkspace;
    for (const WorkspaceItemState &state : m_savedWorkspace) {
        m_itemStates.insert(state.path, state);
        scheduleImageLoad(state.path, LoadRestore);
    }
    m_fitMode = false;
    m_fillMode = false;
    // Apply zoom before scene-rect expansion; pan after range is valid.
    if (m_hasSavedWorkspaceView) {
        setTransform(m_savedWorkspaceViewTransform);
    }
    updateWorkspaceSceneRect();
    if (m_hasSavedWorkspaceView) {
        centerOn(m_savedWorkspaceViewCenter);
        m_hasSavedWorkspaceView = false;
    }
    emit statusChanged();
}

void ImageView::discardStashedWorkspace()
{
    for (ImageItem *item : m_stashedWorkspaceItems) {
        if (!item) {
            continue;
        }
        if (QGraphicsScene *sc = item->scene()) {
            sc->removeItem(item);
        }
        delete item;
    }
    m_stashedWorkspaceItems.clear();
    m_hasStashedWorkspaceView = false;
}

void ImageView::stashWorkspaceItems()
{
    // Replace any previous workspace stash (e.g. nested mode switches).
    discardStashedWorkspace();
    // Zoom lives in the view matrix; pan lives in scrollbars — capture both.
    // Scene centre is robust across sceneRect rebuilds (same idea as Gallery).
    m_stashedWorkspaceViewTransform = transform();
    m_stashedWorkspaceViewCenter = mapToScene(viewport()->rect().center());
    m_hasStashedWorkspaceView = true;
    if (m_items.isEmpty()) {
        return;
    }
    m_stashedWorkspaceItems = m_items;
    m_handleDragItem = nullptr;
    m_groupScaleDrag = false;
    m_groupRotateDrag = false;
    m_groupHandle = -1;
    m_groupHoverHandle = -1;
    m_groupDragItems.clear();
    m_groupDragStartStates.clear();
    m_rotateItem = nullptr;
    m_rotating = false;
    m_dragItem = nullptr;
    for (ImageItem *item : m_stashedWorkspaceItems) {
        if (!item) {
            continue;
        }
        // Keep selection flags on the item for restore; only detach from scene.
        if (item->scene()) {
            item->scene()->removeItem(item);
        }
    }
    m_items.clear();
}

void ImageView::restoreStashedWorkspaceItems()
{
    if (m_stashedWorkspaceItems.isEmpty()) {
        return;
    }
    // Drop Image-mode canvas (single tile) without touching the stash.
    while (!m_items.isEmpty()) {
        destroyCanvasItem(m_items.last());
    }
    if (m_scene) {
        m_scene->blockSignals(true);
        m_scene->clear();
        m_scene->blockSignals(false);
    }
    m_items = m_stashedWorkspaceItems;
    m_stashedWorkspaceItems.clear();
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        if (!item->scene()) {
            m_scene->addItem(item);
        }
        applyItemModeFlags(item);
        // Prefer per-session-slot appearance (value copy for this slot).
        // Fall back to path map only for the sole instance of a path.
        const WorkspaceItemState *app = nullptr;
        WorkspaceItemState pathFallback;
        if (item->sessionId() != kInvalidSessionImageId) {
            if (const WorkspaceItemState *sit = m_appearance.get(item->sessionId())) {
                app = sit;
            }
        }
        if (!app) {
            int samePath = 0;
            for (ImageItem *peer : m_items) {
                if (peer && peer->path() == item->path()) {
                    ++samePath;
                }
            }
            if (samePath == 1) {
                const auto it = m_itemStates.constFind(item->path());
                if (it != m_itemStates.cend()) {
                    pathFallback = *it;
                    app = &pathFallback;
                }
            }
        }
        if (!app) {
            continue;
        }
        // Pixels are usually already updated by commitItemSessionEdit while
        // stashed. Rebuild only when size/metadata disagree with slot state.
        const QSize want = app->hasCrop ? app->cropRect.size() : QSize();
        const bool sizeMismatch = app->hasCrop && item->imageSize() != want;
        const bool missing = item->sourceImage().isNull();
        const bool flagsMismatch =
            item->contentHFlip() != app->contentHFlip
            || item->contentVFlip() != app->contentVFlip
            || item->sessionHasCrop() != app->hasCrop;
        if (!(sizeMismatch || missing || flagsMismatch)) {
            continue;
        }
        if (!app->hasCrop && app->contentQuarterTurns == 0
            && !app->contentHFlip && !app->contentVFlip) {
            continue;
        }
        const QImage full = ImageLoader::load(item->path());
        if (!full.isNull()) {
            item->setSourceImage(full);
            applySessionCrop(item, *app);
            applyContentBakes(item, *app);
            item->setSessionCrop(app->hasCrop, app->cropRect);
        }
    }
    m_fitMode = false;
    m_fillMode = false;
    // Order: zoom → expand sceneRect for the new scale → pan to saved centre.
    // updateWorkspaceSceneRect alone would leave scrollbars at Image-mode zeros.
    if (m_hasStashedWorkspaceView) {
        setTransform(m_stashedWorkspaceViewTransform);
    }
    updateWorkspaceSceneRect();
    if (m_hasStashedWorkspaceView) {
        centerOn(m_stashedWorkspaceViewCenter);
        m_hasStashedWorkspaceView = false;
    }
    viewport()->update();
}

void ImageView::updateWorkspaceSceneRect()
{
    if (!m_scene || !isWorkspaceMode()) {
        return;
    }
    QRectF bounds = m_scene->itemsBoundingRect();
    if (m_pageGuideVisible) {
        bounds = bounds.united(pageGuideSceneRect());
    }
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
