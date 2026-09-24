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
    m_image.copySessionAppearance(fromId, toId);
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
    return m_image.resetContentAppearanceForTargets();
}


