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
#include <QDebug>

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

void ImageController::flushAppliedContentToItemWorld()
{
    if (!m_view) {
        return;
    }
    // Applied ContentXform is mid-edit *presentation* state. Sparse contentBake
    // / crop are the durable ground truth. Before mode leave: commit applied →
    // sparse, then clear ItemWorld applied residual so the next mode's underlay
    // materializes from contentBake only (not a stale dual-write fingerprint).
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        // Prefer item-local applied (this presentation); fall back to ItemWorld residual.
        ContentXform::Value applied;
        if (item->hasAppliedContentXform()) {
            applied = item->tileContentXform();
        } else if (m_view->itemWorld().hasAppliedContentXform(sid)) {
            applied = m_view->itemWorld().appliedContentXform(sid);
        } else {
            continue;
        }
        const WorkspaceItemState s = SessionAppearance::mergeAppliedIntoDurable(
            m_view->sessionAppearanceValue(sid), applied, sid, item->path());
        m_view->itemWorld().setContentBake(sid, ItemComponents::contentBakeFromState(s));
        m_view->itemWorld().setCrop(sid, ItemComponents::cropFromState(s));
        // Intentionally no setColor — applied.colorAdjust is not durable authority.
        // Drop live applied on the tile so stash/restore cannot treat mid-edit
        // fingerprint as parallel authority (ECS_GUI_BYPASSES #4 / #6).
        m_view->clearLiveContentMeta(item);
    }
    m_view->itemWorld().clearAllAppliedContentXforms();
}

void ImageController::setItemSessionId(ImageItem *item, SessionImageId id)
{
    if (!item || !m_view) {
        return;
    }
    // IDENTITY: one SessionImageId maps to one path. If a live or stashed tile
    // already holds this id on a different path, unbind it (peer-sync was
    // seeing sid=2 on both 001.jpg and 002.jpg).
    if (id != kInvalidSessionImageId && !item->path().isEmpty()) {
        auto scrub = [&](const QList<ImageItem *> &list) {
            for (ImageItem *other : list) {
                if (!other || other == item) {
                    continue;
                }
                if (other->sessionId() != id) {
                    continue;
                }
                if (other->path() == item->path()) {
                    continue;
                }
                qCritical("setItemSessionId: SessionImageId %lld path conflict "
                          "(%s vs %s) — unbinding other tile",
                          static_cast<long long>(id),
                          qPrintable(item->path()),
                          qPrintable(other->path()));
                other->setSessionId(kInvalidSessionImageId);
            }
        };
        scrub(m_view->liveItems());
        scrub(m_view->hostWorkspace().stashedItems());
        scrub(m_view->hostGallery().stashedItems());
    }
    item->setSessionId(id);
    m_view->refreshSessionIndexCache(item);
    // Live lag is paint/slider residual. Durable Color is store authority.
    // Do not insert an identity lag row on every bind (pollutes hasLiveColorLag
    // and made freeze→setAppearance look like a grade commit).
    if (id != kInvalidSessionImageId) {
        switch (SessionAppearance::bindLiveColorLagAction(
            m_view->itemWorld().hasColor(id), item->colorAdjustments().isIdentity())) {
        case SessionAppearance::BindLagAction::FromDurableColor:
            m_view->itemWorld().setLiveColorLag(id, m_view->itemWorld().color(id).grade);
            break;
        case SessionAppearance::BindLagAction::FromItemGrade:
            m_view->itemWorld().setLiveColorLag(id, item->colorAdjustments());
            break;
        case SessionAppearance::BindLagAction::None:
            break;
        }
    }
}

void ImageController::persistSessionAppearanceSlot(ImageItem *item)
{
    if (!item || !m_view) {
        return;
    }
    // Per-session-image appearance is a value copy keyed by stable id.
    // Image mode may bind the cursor id when the live item is not yet tagged.
    // Workspace/Gallery must not invent an id — that merges edits onto peers.
    const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
    WorkspaceItemState contentSlot;
    bool haveContentSlot = false;
    if (sid != kInvalidSessionImageId) {
        if (item->sessionId() == kInvalidSessionImageId) {
            setItemSessionId(item, sid);
        }
        WorkspaceItemState slot = m_view->freezeItemAppearance(item);
        slot.sessionId = sid;
        slot.sessionIndex = m_view->sessionListIndex(item);
        slot.path = item->path();
        // freeze may carry live color lag; durable Color is grade-commit only.
        slot = SessionAppearance::preferDurableColor(
            slot, m_view->itemWorld().hasColor(sid), m_view->itemWorld().color(sid).grade);
        // Full freeze replace into sparse tables (placement preserved when
        // identity — tip 2059). Color field is durable (above), not lag.
        m_view->itemWorld().setAppearance(sid, slot);
        slot = SessionAppearance::preferDurableColor(
            slot, m_view->itemWorld().hasColor(sid), m_view->itemWorld().color(sid).grade);
        contentSlot = slot;
        haveContentSlot = true;
    } else {
        // Unbound tile: still persist content-hash state for the file.
        contentSlot = m_view->freezeItemAppearance(item);
        haveContentSlot = true;
    }
    if (haveContentSlot) {
        // Durable local state (XDG_STATE_HOME/thumtoo): content-hash keyed.
        // Bound: orient/flip/grade only in XDG — crop lives in ItemWorld sparse by id.
        SessionAppearance::persistPathContentAppearance(
            item->path(), sid != kInvalidSessionImageId, contentSlot);
    }
    if (sid != kInvalidSessionImageId) {
        // Bound: do not last-write appearance onto the path map (duplicates
        // share a path). Placement remains in path book from Workspace
        // rememberItemState / snapshot only.
        const QImage appearance = m_view->hostSessionAppearanceImage(item);
        if (!appearance.isNull()) {
            // Id-keyed only — path signals paint every filmstrip row with
            // the same file (IDENTITY.md).
            emit m_view->sessionAppearanceChanged(sid, item->path(), appearance);
            const bool hasCrop = m_view->itemWorld().hasCrop(sid)
                || m_view->itemAppliedContentXform(item).hasCrop;
            emit m_view->sessionCropApplied(sid, item->path(), appearance, hasCrop);
        }
    }
}

void ImageController::rememberItemState(ImageItem *item)
{
    if (!item || !m_view) {
        return;
    }
    const SessionImageId editSid = m_view->hostResolveContentEditSessionId(item);
    // Pose-only for bound: freeze carries live color lag; setAppearance would
    // promote lag into durable Color (ECS_GUI_BYPASSES #5). Content commits
    // go through persistSessionAppearanceSlot / crop / bake paths.
    switch (SessionAppearance::rememberKind(
        m_view->isImageMode(), editSid, item->sessionId())) {
    case SessionAppearance::RememberKind::Skip:
        return;
    case SessionAppearance::RememberKind::WritePlacementOnly:
        m_view->itemWorld().setPlacement(item->sessionId(),
                                         item->placement());
        return;
    case SessionAppearance::RememberKind::WritePathFreeze: {
        WorkspaceItemState s = m_view->freezeItemAppearance(item);
        s.path = item->path();
        m_view->itemWorld().setPathState(item->path(), s);
        return;
    }
    }
}

void ImageController::propagateSessionAppearanceToViews(ImageItem *item)
{
    if (!item || !m_view) {
        return;
    }
    // Filmstrip: persistSessionAppearanceSlot already emits when display pixels
    // exist. Re-emit after peer sync so soft-only tiles that gained pixels, and
    // paths that skipped emit, still update the strip.
    const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
    if (sid != kInvalidSessionImageId) {
        const QImage appearanceImage = m_view->hostSessionAppearanceImage(item);
        if (!appearanceImage.isNull()) {
            emit m_view->sessionAppearanceChanged(sid, item->path(), appearanceImage);
            if (m_view->itemWorld().hasCrop(sid)
                || m_view->itemAppliedContentXform(item).hasCrop) {
                emit m_view->sessionCropApplied(sid, item->path(), appearanceImage,
                                                /*hasCrop=*/true);
            }
        }
    }

    if (m_view->isGalleryMode()) {
        m_view->hostGallery().onContentAppearancePropagated();
    } else if (m_view->isWorkspaceMode()) {
        m_view->hostWorkspace().updateSceneRect();
    } else if (m_view->isImageMode()) {
        onContentAppearancePropagated(item);
    }
}
