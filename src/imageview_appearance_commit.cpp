// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session appearance commit, peer sync, copy, and content-reset.

#include "imageview.h"
#include "crop/cropsession.h"
#include "display/imagecache.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "host/thumtoocache.h"
#include "imageitem.h"
#include "item/itemcomponents.h"





void ImageView::commitItemSessionEdit(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Bound session id: persistSessionAppearanceSlot is the single setAppearance
    // + durable write. rememberItemState would re-write sparse tables again.
    // Unbound: path-map still needs rememberItemState (Workspace/Gallery/Image).
    if (item->sessionId() == kInvalidSessionImageId) {
        rememberItemState(item);
    }
    persistSessionAppearanceSlot(item);
    validateUniqueLiveSessionIds("commitItemSessionEdit");
    m_displayPipeline->syncSessionEditPeers(item);
    m_workspace.updateSavedAppearanceFromItem(item);
    // All modes / widgets that depend on content aspect or appearance pixels.
    propagateSessionAppearanceToViews(item);
    emit statusChanged();
}


void ImageView::propagateSessionAppearanceToViews(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Filmstrip: persistSessionAppearanceSlot already emits when display pixels
    // exist. Re-emit after peer sync so soft-only tiles that gained pixels, and
    // paths that skipped emit, still update the strip.
    const SessionImageId sid = resolveContentEditSessionId(item);
    if (sid != kInvalidSessionImageId) {
        const QImage appearanceImage = sessionAppearanceImage(item);
        if (!appearanceImage.isNull()) {
            emit sessionAppearanceChanged(sid, item->path(), appearanceImage);
            if (m_itemWorld.hasCrop(sid)
                || itemAppliedContentXform(item).hasCrop) {
                emit sessionCropApplied(sid, item->path(), appearanceImage, /*hasCrop=*/true);
            }
        }
    }

    if (isGalleryMode()) {
        m_gallery.onContentAppearancePropagated();
    } else if (isWorkspaceMode()) {
        m_workspace.updateSceneRect();
    } else if (isImageMode()) {
        m_image.onContentAppearancePropagated(item);
    }
}


void ImageView::copySessionAppearance(SessionImageId fromId, SessionImageId toId)
{
    if (fromId == kInvalidSessionImageId || toId == kInvalidSessionImageId
        || fromId == toId) {
        return;
    }
    // Prefer the session store; fall back to a live donor tile so drop-duplicate
    // from a graded filmstrip row still carries crop / bakes / colour grade.
    WorkspaceItemState src;
    if (m_itemWorld.hasDurableAppearance(fromId)) {
        src = sessionAppearanceValue(fromId);
    } else {
        ImageItem *donor = findItemBySessionId(fromId);
        if (!donor && isImageMode()) {
            donor = primaryItem();
            if (donor && donor->sessionId() != fromId) {
                donor = nullptr;
            }
        }
        if (!donor) {
            return;
        }
        src = freezeItemAppearance(donor);
    }
    const WorkspaceItemState dst =
        SessionAppearance::appearanceCopyWithIdentityPose(src, toId);
    m_itemWorld.setAppearance(toId, dst);

    ImageItem *donor = findItemBySessionId(fromId);
    if (!donor && isImageMode()) {
        donor = primaryItem();
        if (donor && donor->sessionId() != fromId) {
            donor = nullptr;
        }
    }
    if (donor) {
        const QImage appearance = sessionAppearanceImage(donor);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(toId, dst.path, appearance);
            if (dst.hasCrop) {
                emit sessionCropApplied(toId, dst.path, appearance, /*hasCrop=*/true);
            }
        }
    }
}






// --- content appearance targets (from transform) ---

bool ImageView::targetHasContentAppearance() const
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return false;
    }
    for (const ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = resolveContentEditSessionId(item);
        const bool hasDurable = sid != kInvalidSessionImageId
            && m_itemWorld.hasDurableAppearance(sid);
        if (SessionAppearance::itemShowsContentEdit(
                sid,
                sid != kInvalidSessionImageId && m_itemWorld.hasContentEditComponents(sid),
                hasDurable,
                hasDurable && SessionAppearance::hasContentAppearance(
                                  sessionAppearanceValue(sid)),
                SessionAppearance::liveItemHasContentMods(itemAppliedContentXform(item)),
                ThumtooCache::hasContentAppearance(item->path()))) {
            return true;
        }
    }
    return false;
}


int ImageView::resetContentAppearanceForTargets()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return 0;
    }
    int n = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        const SessionImageId sid = resolveContentEditSessionId(item);

        // 1) Drop durable XDG state for this content.
        ThumtooCache::clearContentAppearance(path);

        // 2) Clear sparse content (crop / bake / color); keep attention + Placement.
        if (sid != kInvalidSessionImageId) {
            m_itemWorld.clearContentComponents(sid);
        }
        // Unbound path map may still hold content; clear so captureState cannot
        // resurrect orient after Reset Content Appearance.
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(path)) {
            WorkspaceItemState pathSlot = SessionAppearance::clearedContentOps(*st);
            m_itemWorld.setPathState(path, pathSlot);
        }

        // Identity: drop applied ContentXform fingerprint and live grade.
        clearLiveContentMeta(item);
        syncLiveColorFromState(item, ColorAdjustments{});
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
                const QSize known = m_size.book().known(path);
                if (!known.isEmpty()) {
                    native = known;
                }
            }
            if (SessionAppearance::isUsableNativeSize(native)) {
                m_displayPipeline->hostSetIntrinsicSize(item, native);
            }
        }

        // 3) Reinstall *mode-appropriate* pixels — never promote a full decode
        // into Gallery soft tiles (that stuck tiles on native res and skipped
        // the soft ladder forever via hasDecodedPixels()).
        //
        //   Gallery  → soft ladder (≤ kGalleryLadderEdge), reset soft state
        //   Image / Workspace → full on-disk decode (user is inspecting / placing)
        //
        // Gallery focused tiles often already hold a *full* oriented decode
        // (Image-mode visit or soft→full upgrade). setPreviewImage no-ops when
        // full source is present, so identity soft would never replace the
        // oriented pixels — Gallery + filmstrip stayed flipped while Image mode
        // (FullSource install) looked correct. Always drop pixels first.
        // Mode-appropriate identity pixels — pipeline owns soft vs full install.
        m_displayPipeline->reinstallModePixelsAfterIdentityReset(item, sid);

        if (isImageMode() && m_image.framing().isFitMode()) {
            m_image.fitItem(item, currentFitAspectMode());
        }

        // Filmstrip: emit current *display* pixels (soft in Gallery, full in Image).
        // Do not emit a separate full decode for Gallery filmstrip overrides.
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, path, appearance);
            }
        }
        ++n;
    }
    if (n > 0 && isGalleryMode()) {
        m_gallery.onContentAppearanceReset();
    }
    if (n > 0) {
        emit statusChanged();
    }
    return n;
}

