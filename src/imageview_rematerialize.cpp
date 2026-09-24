// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Rematerialize host forwards — implementation on DisplayPipelineController.

#include "imageview.h"
#include "imageitem.h"
#include "session/sessionappearance.h"
#include "content/contentxform.h"

void ImageView::attachDisplaySample(ImageItem *item, const QImage &display,
                                      const WorkspaceItemState &want,
                                      SessionAppearance::PixelKind kind)
{
    m_displayPipeline.attachDisplaySample(item, display, want, kind);
}

void ImageView::rematerializeItemContent(ImageItem *item, const WorkspaceItemState &want)
{
    m_displayPipeline.rematerializeItemContent(item, want);
}

void ImageView::applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &wantIn)
{
    m_displayPipeline.applyContentLayoutSize(item, wantIn);
}

void ImageView::scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                                 const WorkspaceItemState &want)
{
    m_displayPipeline.scheduleAsyncHostRematerialize(path, sid, want);
}

void ImageView::clearStaleAppliedFingerprintIfNeeded(ImageItem *item)
{
    if (!item) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId || !m_itemWorld.hasDurableAppearance(sid)) {
        return;
    }
    if (!itemHasAppliedContentXform(item)) {
        return;
    }
    const WorkspaceItemState st = sessionAppearanceValue(sid);
    if (!SessionAppearance::hasContentAppearance(st)) {
        return;
    }
    const ContentXform::Value want = ContentXform::Value::fromState(st);
    if (ContentXform::equal(itemAppliedContentXform(item), want)) {
        return;
    }
    // Stash may hold a stale applied fingerprint from Image-mode edits that
    // were committed to ItemWorld while this tile was off-canvas.
    clearLiveContentMeta(item);
}

void ImageView::rematerializeGalleryItemFromStore(ImageItem *item)
{
    if (!item) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    if (!m_itemWorld.hasDurableAppearance(sid)) {
        return;
    }
    const WorkspaceItemState st = sessionAppearanceValue(sid);
    if (!SessionAppearance::hasContentAppearance(st)) {
        return;
    }
    const ContentXform::Value want = ContentXform::Value::fromState(st);
    const ContentXform::Value applied = itemAppliedContentXform(item);
    if (itemHasAppliedContentXform(item) && ContentXform::equal(applied, want)
        && item->hasDisplayPixels()) {
        // Applied matches store; still fix layout if intrinsic is full-frame.
        applyContentLayoutSize(item, st);
        return;
    }
    clearStaleAppliedFingerprintIfNeeded(item);
    rematerializeItemContent(item, st);
}
