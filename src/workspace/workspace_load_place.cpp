// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// LoadAdd / drop footprint install and placement (was ImageView session bind).

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "display/imagecache.h"
#include "session/sessionappearance.h"
#include "session/sessionbindbook.h"
#include "view/viewtransform.h"

bool WorkspaceController::installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image)
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
    m_view->hostDisplayPipeline().installDisplayPixels(item, image, SessionAppearance::PixelKind::FullSource,
                         item->sessionId());
    const QSize after = item->imageSize();
    const bool grew = before.isValid() && after.isValid()
        && (before.width() != after.width() || before.height() != after.height());
    if (grew && m_view->isWorkspaceMode() && m_view->hostLayout().isFreeForm()
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

void WorkspaceController::applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound)
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
        pl.z = m_view->liveItems().size() - 1;
        item->applyPlacement(pl);
    }
    if (m_view->isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    }
    m_view->hostDisplayPipeline().loadGate().removePendingScenePos(item->path());
    // Pose-only persist (explicit drop); content stays on session id / path map.
    m_view->persistGeometrySessionState(item, item->placement());
}

void WorkspaceController::placeNewLoadAddItem(ImageItem *item, const QString &path,
                                    const QImage &image, bool haveBound,
                                    const PendingSessionBind &bound)
{
    if (!item) {
        return;
    }
    if (m_view->isGalleryMode()) {
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
        && m_view->itemWorld().hasDurableAppearance(bound.id)) {
        // Thumbnail membership toggle: restore last Workspace pose.
        m_view->applyState(item, m_view->sessionAppearanceValue(bound.id));
        return;
    }
    QPointF pos;
    if (m_view->hostDisplayPipeline().loadGate().takePendingScenePos(path, &pos)) {
        {
            ItemComponents::Placement pl;
            pl.pos = pos;
            pl.z = m_view->liveItems().size() - 1;
            item->applyPlacement(pl);
        }
        return;
    }
    if (const WorkspaceItemState *st = m_view->itemWorld().getPathState(path)) {
        m_view->applyState(item, *st);
        return;
    }
    WorkspaceItemState s = m_view->defaultStateForPath(path, m_view->liveItems().size() - 1);
    const QSizeF sz(image.width(), image.height());
    s.pos = findEmptyPlacement(sz);
    m_view->applyState(item, s);
}
