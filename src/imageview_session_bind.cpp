// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds, session-id canvas membership, and load-add placement.

#include "imageview.h"
#include "itemcomponents.h"
#include "imageitem.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "sessionbindbook.h"
#include "viewtransform.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QScrollBar>

bool ImageView::hasPendingSessionBindForPath(const QString &path) const
{
    return m_bindBook.hasBindForPath(path);
}


int ImageView::countPendingSessionBinds(const QString &path) const
{
    return m_bindBook.countBindsForPath(path);
}


void ImageView::purgeSatisfiedPendingBinds(const QString &path)
{
    for (int bi = m_bindBook.bindCount() - 1; bi >= 0; --bi) {
        const PendingSessionBind &b = m_bindBook.bindAt(bi);
        if (b.path != path || b.id == kInvalidSessionImageId) {
            continue;
        }
        if (findItemBySessionId(b.id)) {
            m_bindBook.removeBindAt(bi);
        }
    }
}


bool ImageView::takePendingSessionBind(const QString &path, PendingSessionBind *out)
{
    return m_bindBook.takeBind(path, out);
}


void ImageView::applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound)
{
    if (!item || !bound.hasScenePos) {
        return;
    }
    // Explicit drop pose: free-form identity placement only (never revive
    // gallery pack scale/cell or a prior non-uniform footprint scale).
    item->setGalleryCellSize({});
    {
        ItemComponents::Placement pl;
        pl.pos = bound.scenePos;
        pl.z = m_items.size() - 1;
        item->applyPlacement(pl);
    }
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    }
    m_displayPipeline.loadGate().removePendingScenePos(item->path());
    rememberItemState(item);
}


bool ImageView::installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image)
{
    if (!item || image.isNull()) {
        return false;
    }
    // Soft mis-labeled as decoded must still accept a stricter long edge.
    if (item->hasDecodedPixels()
        && !item->shouldUpgradeDisplayTo(ImageCache::longEdge(image))) {
        return false;
    }
    // Drop / LoadAdd placeholder → full. Never non-uniform scale: that stretched
    // oriented (or just aspect-correct) pixels into the provisional footprint and
    // looked like "wrong rotation + stretch" on drag-drop without any rotate.
    const QSize before = item->imageSize();
    const qreal sx0 = item->itemScaleX();
    const qreal sy0 = item->itemScaleY() > 0.0 ? item->itemScaleY() : sx0;
    const qreal footW = before.width() * sx0;
    const qreal footH = before.height() * sy0;
    // Leave Gallery pack geometry on Workspace tiles.
    item->setGalleryCellSize({});
    installDisplayPixels(item, image, SessionAppearance::PixelKind::FullSource,
                         item->sessionId());
    const QSize after = item->imageSize();
    const bool grew = before.isValid() && after.isValid()
        && (before.width() != after.width() || before.height() != after.height());
    if (grew && isWorkspaceMode() && m_layout.isFreeForm()
        && after.width() > 0 && after.height() > 0) {
        const bool neutralScale =
            qAbs(sx0 - 1.0) < 1e-6 && qAbs(sy0 - 1.0) < 1e-6;
        if (neutralScale) {
            // Fresh drop: 1:1 scene units = content pixels (no footprint squash).
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        } else {
            // Prior intentional scale (e.g. moved from Gallery): fit uniformly.
            const qreal s = ViewTransform::uniformFitScale(
                footW, footH, qreal(after.width()), qreal(after.height()));
            if (s > 1e-6) {
                ItemComponents::Placement pl = item->placement();
                pl.scale = s;
                pl.scaleY = s;
                item->applyPlacement(pl);
            }
        }
        return true;
    }
    if (grew) {
        return true;
    }
    item->update();
    return false;
}


bool ImageView::takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                                 PendingSessionBind *out)
{
    if (!out || path.isEmpty() || !item) {
        return false;
    }
    for (int bi = 0; bi < m_bindBook.bindCount(); ++bi) {
        if (m_bindBook.bindAt(bi).path != path) {
            continue;
        }
        const PendingSessionBind candidate = m_bindBook.bindAt(bi);
        if (candidate.id != kInvalidSessionImageId) {
            if (ImageItem *owner = findItemBySessionId(candidate.id)) {
                if (owner != item) {
                    m_bindBook.removeBindAt(bi);
                    --bi;
                    continue;
                }
            }
        }
        m_bindBook.takeBindAt(bi, out);
        if (out->id != kInvalidSessionImageId) {
            item->setSessionId(out->id);
        }
        if (out->index >= 0 && out->id != kInvalidSessionImageId) {
            item->setSessionIndex(out->index);
        }
        return true;
    }
    return false;
}


void ImageView::placeNewLoadAddItem(ImageItem *item, const QString &path,
                                    const QImage &image, bool haveBound,
                                    const PendingSessionBind &bound)
{
    if (!item) {
        return;
    }
    if (isGalleryMode()) {
        // Packed layout owns pose; keep item transform neutral (pos/scale later).
        ItemComponents::Placement pl = item->placement();
        pl.rotation = 0.0;
        pl.shear = 0.0;
        pl.hFlip = false;
        pl.vFlip = false;
        pl.opacity = 1.0;
        item->applyPlacement(pl);
        return;
    }
    if (haveBound && bound.hasScenePos) {
        // Explicit drop: place at the drop point (new placement).
        applyPendingBindScenePos(item, bound);
        return;
    }
    if (haveBound && bound.id != kInvalidSessionImageId
        && m_itemWorld.getAppearance(bound.id)) {
        // Thumbnail membership toggle: restore last Workspace pose.
        applyState(item, *m_itemWorld.getAppearance(bound.id));
        return;
    }
    QPointF pos;
    if (m_displayPipeline.loadGate().takePendingScenePos(path, &pos)) {
        {
            ItemComponents::Placement pl;
            pl.pos = pos;
            pl.z = m_items.size() - 1;
            item->applyPlacement(pl);
        }
        return;
    }
    if (const WorkspaceItemState *st = m_itemWorld.getPathState(path)) {
        applyState(item, *st);
        return;
    }
    WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
    const QSizeF sz(image.width(), image.height());
    s.pos = findEmptyPlacement(sz);
    applyState(item, s);
}


QList<ImageItem *> ImageView::collectItemsForSessionId(SessionImageId sessionId) const
{
    QList<ImageItem *> doomed;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *item : list) {
            if (item && item->sessionId() == sessionId && !doomed.contains(item)) {
                doomed.append(item);
            }
        }
    };
    collect(m_items);
    collect(m_workspace.stashedItems());
    collect(m_gallery.stashedItems());
    return doomed;
}


QStringList ImageView::destroySessionIdItems(const QList<ImageItem *> &doomed)
{
    QStringList removedPaths;
    for (ImageItem *item : doomed) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        removedPaths.append(path);
        // Drop in-flight decodes so a late LoadAdd cannot create a tile or
        // call applyLayout after this session image is gone.
        m_displayPipeline.loadGate().removePendingWorkspacePath(path);
        gallerySoftResetPath(path);
        m_displayPipeline.loadGate().removePendingScenePos(path);
        m_bindBook.removeIndexForPath(path);
        // destroyCanvasItem clears selection anchor / drag pointers and
        // removes from m_items and both stashes (safe if already only in one).
        destroyCanvasItem(item);
    }
    return removedPaths;
}
