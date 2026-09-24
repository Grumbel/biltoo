// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Content flip / quarter-turn rotate (all modes). Pipeline bakes pixels;
// Image-mode framing and Gallery pack run as post-steps. ImageView thin routers.

#include "image/imagecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "display/displaypipelinecontroller.h"
#include "gallery/gallerycontroller.h"
#include "view/viewframing.h"
#include "host/thumtoocache.h"
#include "session/sessionappearance.h"
#include "item/itemcomponents.h"
#include "color/coloradjust.h"

void ImageController::flipHorizontal()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        m_view->hostDisplayPipeline().bakeItemFlip(item, true, false);
        if (m_framing.isFitMode() && m_view->isImageMode()) {
            m_view->fitItem(item, m_view->currentFitAspectMode());
        }
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    emit m_view->statusChanged();
}

void ImageController::flipVertical()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        m_view->hostDisplayPipeline().bakeItemFlip(item, false, true);
        if (m_framing.isFitMode() && m_view->isImageMode()) {
            m_view->fitItem(item, m_view->currentFitAspectMode());
        }
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    emit m_view->statusChanged();
}

void ImageController::rotateContentByQuarterTurns(ImageItem *item, int quarterTurns)
{
    // One content-rotate path for Workspace chrome, toolbar, and keyboard.
    // DisplayPipelineController::bakeItemRotate90 composes want + ItemWorld/undo + pixels.
    // Placement scale is NOT adjusted: fitting into the pre-rotate AABB shrinks
    // non-square images on every 90°. Workspace scene units = content pixels × scale.
    if (!item || quarterTurns == 0) {
        return;
    }

    m_view->hostDisplayPipeline().bakeItemRotate90(item, quarterTurns);

    if (m_view->isImageMode()) {
        if (m_framing.isFitMode()) {
            m_view->fitItem(item, m_view->currentFitAspectMode());
        } else if (m_framing.isFillMode()) {
            m_view->fitItem(item, Qt::KeepAspectRatioByExpanding);
        }
    }
}

void ImageController::rotateLeft()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        rotateContentByQuarterTurns(item, -1);
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    } else if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    emit m_view->statusChanged();
}

void ImageController::rotateRight()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        rotateContentByQuarterTurns(item, 1);
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    } else if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    emit m_view->statusChanged();
}

int ImageController::resetContentAppearanceForTargets()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return 0;
    }
    int n = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);

        // 1) Drop durable XDG state for this content.
        ThumtooCache::clearContentAppearance(path);

        // 2) Clear sparse content (crop / bake / color); keep attention + Placement.
        if (sid != kInvalidSessionImageId) {
            m_view->itemWorld().clearContentComponents(sid);
        }
        // Unbound path map may still hold content; clear so captureState cannot
        // resurrect orient after Reset Content Appearance.
        if (const WorkspaceItemState *st = m_view->itemWorld().getPathState(path)) {
            WorkspaceItemState pathSlot = SessionAppearance::clearedContentOps(*st);
            m_view->itemWorld().setPathState(path, pathSlot);
        }

        // Identity: drop applied ContentXform fingerprint and live grade.
        m_view->clearLiveContentMeta(item);
        m_view->syncLiveColorFromState(item, ColorAdjustments{});
        {
            ItemComponents::Placement pl = item->placement();
            pl.hFlip = false;
            pl.vFlip = false;
            item->applyPlacement(pl);
        }

        // Restore layout geometry to the unoriented native size (content
        // rotate may have transposed intrinsic).
        {
            QSize native = ThumtooCache::cachedSize(path);
            if (!native.isValid() || native.width() < 1 || native.height() < 1) {
                const QSize known = m_view->hostSizeBook().known(path);
                if (!known.isEmpty()) {
                    native = known;
                }
            }
            if (SessionAppearance::isUsableNativeSize(native)) {
                m_view->hostDisplayPipeline().hostSetIntrinsicSize(item, native);
            }
        }

        // 3) Reinstall mode-appropriate identity pixels (pipeline owns soft vs full).
        m_view->hostDisplayPipeline().reinstallModePixelsAfterIdentityReset(item, sid);

        if (m_view->isImageMode() && m_framing.isFitMode()) {
            m_view->fitItem(item, m_view->currentFitAspectMode());
        }

        // Filmstrip: emit current display pixels (soft in Gallery, full in Image).
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = m_view->hostSessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit m_view->sessionAppearanceChanged(sid, path, appearance);
            }
        }
        ++n;
    }
    if (n > 0 && m_view->isGalleryMode()) {
        m_view->hostGallery().onContentAppearanceReset();
    }
    if (n > 0) {
        emit m_view->statusChanged();
    }
    return n;
}

void ImageController::renderForPrint(QPainter *painter, const QRectF &pageRect) const
{
    if (!painter || !painter->isActive() || !pageRect.isValid() || !m_view) {
        return;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item) {
        item = m_view->targetItem();
    }
    if (!item || item->path().isEmpty()) {
        return;
    }
    const QImage img = m_view->hostDisplayPipeline().blockingExportDisplayForItem(item);
    if (img.isNull()) {
        return;
    }
    QSizeF fitted(img.size());
    fitted.scale(pageRect.size(), Qt::KeepAspectRatio);
    const QRectF target(
        pageRect.center().x() - fitted.width() / 2.0,
        pageRect.center().y() - fitted.height() / 2.0,
        fitted.width(), fitted.height());
    painter->save();
    painter->translate(target.center());
    const ItemComponents::Placement pl = item->placement();
    painter->rotate(pl.rotation);
    painter->scale(pl.hFlip ? -1.0 : 1.0, pl.vFlip ? -1.0 : 1.0);
    painter->translate(-target.center());
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(target, img);
    painter->restore();
}

void ImageController::copySessionAppearance(SessionImageId fromId, SessionImageId toId)
{
    if (fromId == kInvalidSessionImageId || toId == kInvalidSessionImageId
        || fromId == toId || !m_view) {
        return;
    }
    // Prefer the session store; fall back to a live donor tile so drop-duplicate
    // from a graded filmstrip row still carries crop / bakes / colour grade.
    WorkspaceItemState src;
    if (m_view->itemWorld().hasDurableAppearance(fromId)) {
        src = m_view->sessionAppearanceValue(fromId);
    } else {
        ImageItem *donor = m_view->findItemBySessionId(fromId);
        if (!donor && m_view->isImageMode()) {
            donor = m_view->primaryItem();
            if (donor && donor->sessionId() != fromId) {
                donor = nullptr;
            }
        }
        if (!donor) {
            return;
        }
        src = m_view->freezeItemAppearance(donor);
    }
    const WorkspaceItemState dst =
        SessionAppearance::appearanceCopyWithIdentityPose(src, toId);
    m_view->itemWorld().setAppearance(toId, dst);

    ImageItem *donor = m_view->findItemBySessionId(fromId);
    if (!donor && m_view->isImageMode()) {
        donor = m_view->primaryItem();
        if (donor && donor->sessionId() != fromId) {
            donor = nullptr;
        }
    }
    if (donor) {
        const QImage appearance = m_view->hostSessionAppearanceImage(donor);
        if (!appearance.isNull()) {
            emit m_view->sessionAppearanceChanged(toId, dst.path, appearance);
            if (dst.hasCrop) {
                emit m_view->sessionCropApplied(toId, dst.path, appearance, /*hasCrop=*/true);
            }
        }
    }
}
