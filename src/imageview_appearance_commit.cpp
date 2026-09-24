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
#include "host/imageloader.h"

void ImageView::syncSessionEditPeers(ImageItem *item)
{
    // Propagate pixel / flip / orientation session edits to matching canvas and
    // stashed instances. Placement (pos, scale, free tilt) is preserved.
    const QString path = item->path();
    // Strict identity: only a valid SessionImageId. Never m_sessionId.currentIdValue()
    // fallback here — that would push this item's pixels onto another tile.
    const SessionImageId sessionId = item->sessionId();
    const QImage src = item->sourceImage();
    const ItemComponents::Placement itemPl = item->placement();
    const bool hFlip = itemPl.hFlip;
    const bool vFlip = itemPl.vFlip;

    QList<ImageItem *> peers;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *other : list) {
            if (other && other != item) {
                peers.append(other);
            }
        }
    };
    collect(m_items);
    collect(m_workspace.stashedItems());
    collect(m_gallery.stashedItems());

    auto shouldSync = [&](ImageItem *other) -> bool {
        if (!other || other == item) {
            return false;
        }
        // Same stable session-image id only. Path / list-index must never merge
        // independent duplicates on the Workspace.
        if (sessionId == kInvalidSessionImageId || other->sessionId() != sessionId) {
            return false;
        }
        if (other->path() != path) {
            qCritical("commitItemSessionEdit: SessionImageId %lld bound to different paths (%s vs %s) — refusing peer sync",
                      static_cast<long long>(sessionId),
                      qPrintable(path), qPrintable(other->path()));
            return false;
        }
        return true;
    };

    auto syncOne = [&](ImageItem *other) {
        if (!shouldSync(other)) {
            return;
        }
        // Already-baked display from the edited peer. Must replace peers fully:
        // setPreviewImage is a no-op when the peer still holds full m_source
        // (Gallery stash after Image crop). That left crop intrinsic + full
        // pixels for one frame / until next soft install (Gallery return glitch).
        const QImage baked = !src.isNull() ? src
            : (!item->previewImage().isNull() ? item->previewImage()
                                              : item->displayImage());
        if (!baked.isNull()) {
            other->clearDecodedPixels();
            // Already-baked display from the editor. Attach via the same gate
            // as install (layout + applied + chrome) — do not put into ImageCache.
            WorkspaceItemState want;
            if (sessionId != kInvalidSessionImageId) {
                want = sessionAppearanceValue(sessionId);
            }
            // Applied fingerprint is mid-edit authority when store slot is empty.
            if (!SessionAppearance::hasContentAppearance(want) && itemHasAppliedContentXform(item)) {
                itemAppliedContentXform(item).applyToState(want);
            }
            const auto kind = !src.isNull()
                ? SessionAppearance::PixelKind::FullSource
                : SessionAppearance::PixelKind::SoftPreview;
            attachDisplaySample(other, baked, want, kind);
        } else if (sessionId != kInvalidSessionImageId) {
            applyContentLayoutSize(other, sessionAppearanceValue(sessionId));
        }
        {
            ItemComponents::Placement pl = other->placement();
            pl.hFlip = hFlip;
            pl.vFlip = vFlip;
            other->applyPlacement(pl);
        }
    };
    for (ImageItem *other : peers) {
        syncOne(other);
    }
}


void ImageView::updateWorkspaceSavedAppearance(ImageItem *item)
{
    // Bound durable content is ItemWorld only (Workspace restore re-reads
    // sessionAppearanceValue). Snapshot slots hold pose identity + path —
    // never dual-write crop/orient into m_savedItems (2194 / ECS #5 / 2212).
    if (!item) {
        return;
    }
    const SessionImageId sessionId = item->sessionId();
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    const QString path = item->path();
    const ItemComponents::Placement itemPl = placementFromItem(item);
    for (WorkspaceItemState &slot : m_workspace.savedItems()) {
        if (slot.sessionId != sessionId) {
            continue;
        }
        // Never rewrite another tile's path under the same id (IDENTITY).
        if (!slot.path.isEmpty() && slot.path != path) {
            qCritical("updateWorkspaceSavedAppearance: sid %lld slot path %s != %s — skip",
                      static_cast<long long>(sessionId),
                      qPrintable(slot.path), qPrintable(path));
            continue;
        }
        // Full Placement pose from the live item; content stays on ItemWorld
        // sparse tables (same bridge as restore / completeLoadRestore — 2214).
        ItemComponents::applyPlacementToState(slot, itemPl);
        slot.sessionId = sessionId;
        slot.path = path;
    }
}


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
    syncSessionEditPeers(item);
    updateWorkspaceSavedAppearance(item);
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
        // Aspect / crop may change pack cell size — debounce packs concurrent
        // multi-select rotates into one layout pass.
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    } else if (isImageMode() && m_scene && item->scene() == m_scene) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
        if (viewport()) {
            viewport()->update();
        }
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
    WorkspaceItemState dst;
    if (m_itemWorld.hasDurableAppearance(fromId)) {
        dst = sessionAppearanceValue(fromId);
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
        dst = freezeItemAppearance(donor);
    }
    dst.sessionId = toId;
    dst.pos = QPointF();
    dst.scale = 1.0;
    dst.scaleY = 1.0;
    dst.shear = 0.0;
    dst.rotation = 0.0;
    dst.opacity = 1.0;
    dst.z = 0.0;
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
        // Bound: ItemWorld sparse tables are authority (Crop / ContentBake / Color).
        if (sid != kInvalidSessionImageId) {
            if (m_itemWorld.hasCrop(sid) || m_itemWorld.hasContentBake(sid)
                || m_itemWorld.hasColor(sid)) {
                return true;
            }
            if (m_itemWorld.hasDurableAppearance(sid)
                && SessionAppearance::hasContentAppearance(sessionAppearanceValue(sid))) {
                return true;
            }
        }
        if (SessionAppearance::liveItemHasContentMods(itemAppliedContentXform(item))) {
            return true;
        }
        if (ThumtooCache::hasContentAppearance(item->path())) {
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
                const QSize known = m_sizeBook.known(path);
                if (!known.isEmpty()) {
                    native = known;
                }
            }
            if (native.isValid() && native.width() > 1 && native.height() > 1
                && native != QSize(1000, 1000) && native != QSize(1024, 1024)) {
                setItemIntrinsicSize(item, native);
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
        if (isGalleryMode()) {
            m_displayPipeline.galleryDecodeResetPath(path);
            clearItemDecodedPixels(item);
            const int softEdge = ThumtooCache::kGalleryLadderEdge;
            QImage soft = ImageLoader::loadThumbnail(path, softEdge);
            if (!soft.isNull()) {
                // Identity appearance: SoftPreview install without content bake.
                m_displayPipeline.installDisplayPixels(item, soft, SessionAppearance::PixelKind::SoftPreview,
                                     sid);
            }
            // else: decode window will refill after soft state reset
        } else {
            const QImage full = m_displayPipeline.fullRasterForEdit(path);
            if (!full.isNull()) {
                m_displayPipeline.installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource,
                                     sid);
            } else {
                clearItemDecodedPixels(item);
            }
        }

        if (isImageMode() && m_framing.isFitMode()) {
            fitItem(item, currentFitAspectMode());
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
        m_gallery.applyLayout(GalleryPackReason::ContentChange);
        // Soft state was reset; kick the ladder for visible tiles.
        m_gallery.updateDecodeWindow();
    }
    if (n > 0) {
        emit statusChanged();
    }
    return n;
}

