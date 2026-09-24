// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Rematerialize, bake flip/rotate, and interactive color-grade commits.

#include "imageview.h"
#include "content/contentxform.h"
#include "host/thumtoocache.h"
#include "display/imagecache.h"
#include "imageitem.h"
#include "session/sessionappearance.h"
#include "view/viewtransform.h"

#include <QImage>
#include <QTimer>
#include "util/biltoo_thread.h"
#include <QPointer>
#include <QThreadPool>
#include <QMetaObject>

void ImageView::attachDisplaySample(ImageItem *item, const QImage &display,
                                      const WorkspaceItemState &want,
                                      SessionAppearance::PixelKind kind)
{
    // Pixel attach lives on DisplayPipelineController (Phase 5 ownership).
    // External callers keep this ImageView entry; implementation is pipeline.
    m_displayPipeline.attachDisplaySample(item, display, want, kind);
}


void ImageView::rematerializeItemContent(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Crop draft owns the live sample — pure rematerialize must not soft↔full.
    if (m_cropCtrl.isCropDraftLockedItem(item)) {
        return;
    }
    if (tryRematerializeFromHost(item, want)) {
        return;
    }
    const QString path = item->path();
    // Host must be unoriented. Never bake from preview/display — that double-applies
    // crop when the tile already shows a soft crop (Gallery after Image crop).
    QImage raw = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (raw.isNull() && item->hasDecodedPixels() && !itemHasAppliedContentXform(item)) {
        // FullSource without applied xform is still host-shaped (rare).
        raw = item->sourceImage();
    }
    const SessionImageId sid = resolveContentEditSessionId(item);
    if (raw.isNull()) {
        // No unoriented host: schedule async; do not claim applied yet.
        if (!path.isEmpty() && SessionAppearance::hasContentAppearance(want)) {
            scheduleAsyncHostRematerialize(path, sid, want);
        }
        return;
    }
    const int maxGui = ContentXform::kGuiMaterializeMaxEdge;
    int edge = ContentXform::longEdge(raw.size());
    QImage host = raw;
    SessionAppearance::PixelKind bakeKind = item->hasDecodedPixels()
        ? SessionAppearance::PixelKind::FullSource
        : SessionAppearance::PixelKind::SoftPreview;
    bool scheduleFull = false;
    if (edge > maxGui) {
        // Soft stand-in now (crop/orient visible); full bake async.
        host = ImageCache::clampToMaxEdge(raw, maxGui);
        edge = ContentXform::longEdge(host.size());
        bakeKind = SessionAppearance::PixelKind::SoftPreview;
        scheduleFull = true;
    }
    if (edge <= 0 || edge > maxGui) {
        scheduleAsyncHostRematerialize(path, sid, want);
        return;
    }
    const QImage display = SessionAppearance::materializeDisplay(host, want, bakeKind);
    if (display.isNull()) {
        scheduleAsyncHostRematerialize(path, sid, want);
        return;
    }
    if (bakeKind == SessionAppearance::PixelKind::SoftPreview && item->hasDecodedPixels()) {
        clearItemDecodedPixels(item);
    }
    attachDisplaySample(item, display, want, bakeKind);
    if (scheduleFull) {
        scheduleAsyncHostRematerialize(path, sid, want);
    }
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


bool ImageView::tryRematerializeFromHost(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return false;
    }
    if (m_cropCtrl.isCropDraftLockedItem(item)) {
        return false;
    }
    const QString path = item->path();
    const QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull()) {
        return false;
    }
    if (ContentXform::longEdge(host.size()) > ContentXform::kGuiMaterializeMaxEdge) {
        return false;
    }
    // Prefer SoftPreview when the live item is soft-only so setPreviewImage
    // accepts the sample (setPreviewImage ignores soft when full is present).
    // If full is already shown, rematerialize as FullSource.
    const auto kind = item->hasDecodedPixels()
        ? SessionAppearance::PixelKind::FullSource
        : SessionAppearance::PixelKind::SoftPreview;
    const QImage display =
        SessionAppearance::materializeDisplay(host, want, kind);
    if (display.isNull()) {
        return false;
    }
    // attachDisplaySample applies layout via pipeline applyContentLayoutSize.
    attachDisplaySample(item, display, want, kind);
    return true;
}


void ImageView::applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &wantIn)
{
    // Intrinsic layout lives on DisplayPipelineController (Phase 5 ownership).
    m_displayPipeline.applyContentLayoutSize(item, wantIn);
}


void ImageView::scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                                 const WorkspaceItemState &want)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_cropCtrl.isCropDraftLockedPath(path)) {
        return;
    }
    const QImage hostProbe = ImageCache::get(path);
    if (hostProbe.isNull()) {
        return;
    }
    if (ContentXform::longEdge(hostProbe.size())
        <= ContentXform::kGuiMaterializeMaxEdge) {
        return; // GUI path already handled by tryRematerializeFromHost
    }
    const quint64 gen = m_displayPipeline.loadGate().generation();
    QPointer<ImageView> guard(this);
    const WorkspaceItemState wantCopy = want;
    QThreadPool::globalInstance()->start([guard, path, sid, wantCopy, gen]() {
        if (!guard) {
            return;
        }
        const QImage host = ImageCache::get(path);
        if (host.isNull()) {
            return;
        }
        // Worker thread: multi-MP materialize is allowed.
        const QImage display = SessionAppearance::materializeDisplay(
            host, wantCopy, SessionAppearance::PixelKind::FullSource);
        if (display.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, path, sid, wantCopy, display, gen]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                return;
            }
            guard->finishAsyncHostRematerialize(path, sid, wantCopy, display);
        }, Qt::QueuedConnection);
    });
}


void ImageView::finishAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                               const WorkspaceItemState &want,
                                               const QImage &display)
{
    ASSERT_GUI_THREAD();
    if (display.isNull() || path.isEmpty()) {
        return;
    }
    // Crop draft owns the target item — do not reinstall over orient-only draft.
    if (m_cropCtrl.isCropDraftLockedPath(path)) {
        return;
    }
    ImageItem *item = nullptr;
    for (ImageItem *it : m_items) {
        if (!it || it->path() != path) {
            continue;
        }
        if (sid != kInvalidSessionImageId && it->sessionId() != sid
            && it->sessionId() != kInvalidSessionImageId) {
            continue;
        }
        item = it;
        break;
    }
    if (!item) {
        return;
    }
    const ContentXform::Value wantX = ContentXform::Value::fromState(want);
    // Discard stale worker result if the store moved on for this session id.
    if (sid != kInvalidSessionImageId && m_itemWorld.hasDurableAppearance(sid)) {
        const WorkspaceItemState cur = sessionAppearanceValue(sid);
        if (!ContentXform::equal(ContentXform::Value::fromState(cur), wantX)) {
            return;
        }
    }
    // Already settled FullSource for this want at ≥ this resolution — skip.
    // Soft (or soft-sized) FullSource with applied identity used to match and
    // discard Prefer/Full async bakes permanently.
    if (item->hasDecodedPixels() && itemHasAppliedContentXform(item)
        && ContentXform::equal(itemAppliedContentXform(item), wantX)
        && !item->shouldUpgradeDisplayTo(ImageCache::longEdge(display))) {
        return;
    }
    const QSize before = item->imageSize();
    // attachDisplaySample applies layout via pipeline applyContentLayoutSize.
    attachDisplaySample(item, display, want, SessionAppearance::PixelKind::FullSource);
    if (before != item->imageSize()) {
        preserveImageViewOnLogicalSizeChange(item, before, item->imageSize());
    }
    if (isImageMode() && m_scene && m_items.size() == 1) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }
    if (isGalleryMode() && before != item->imageSize()) {
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
    if (isWorkspaceMode()) {
        m_displayPipeline.ensureWorkspaceQualityClimb();
    } else if (isImageMode()) {
        m_displayPipeline.driveImageFocusSurface();
    }
    if (viewport()) {
        viewport()->update();
    }
}
