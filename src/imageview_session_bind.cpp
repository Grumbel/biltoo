// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds, session-id canvas membership, and load-add placement.

#include "imageview.h"
#include "itemcomponents.h"
#include "imageitem.h"
#include "display/imagecache.h"
#include "session/sessionappearance.h"
#include "session/sessionbindbook.h"
#include "view/viewtransform.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QScrollBar>





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
    // Pose-only persist (explicit drop); content stays on session id / path map.
    persistGeometrySessionState(item, item->placement());
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
    const ItemComponents::Placement pl0 = item->placement();
    const qreal sx0 = pl0.scale;
    const qreal sy0 = pl0.scaleY > 0.0 ? pl0.scaleY : sx0;
    const qreal footW = before.width() * sx0;
    const qreal footH = before.height() * sy0;
    // Leave Gallery pack geometry on Workspace tiles.
    item->setGalleryCellSize({});
    m_displayPipeline.installDisplayPixels(item, image, SessionAppearance::PixelKind::FullSource,
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
            // List-order refresh + applied→ItemWorld migrate (2083/2084).
            setItemSessionId(item, out->id);
            if (sessionListIndex(item) < 0 && out->index >= 0) {
                item->setSessionIndex(out->index);
            }
        } else if (out->index >= 0) {
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
        && m_itemWorld.hasDurableAppearance(bound.id)) {
        // Thumbnail membership toggle: restore last Workspace pose.
        applyState(item, sessionAppearanceValue(bound.id));
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
        m_displayPipeline.galleryDecodeResetPath(path);
        m_displayPipeline.loadGate().removePendingScenePos(path);
        // destroyCanvasItem clears selection anchor / drag pointers and
        // removes from m_items and both stashes (safe if already only in one).
        // persistState=false: caller already removeAppearance for this id —
        // rememberItemState would setAppearance and undo the delete.
        destroyCanvasItem(item, false);
    }
    return removedPaths;
}
