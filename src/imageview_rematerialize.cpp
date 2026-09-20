// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Rematerialize, bake flip/rotate, and interactive color-grade commits.

#include "imageview.h"
#include "contentxform.h"
#include "thumtoocache.h"
#include "imagecache.h"
#include "imageitem.h"
#include "sessionappearance.h"
#include "viewtransform.h"

#include <QImage>
#include <QTimer>

void ImageView::attachDisplaySample(ImageItem *item, const QImage &display,
                                      const WorkspaceItemState &want,
                                      SessionAppearance::PixelKind kind)
{
    if (!item || display.isNull()) {
        return;
    }
    const QString path = item->path();

    // Display samples are always display-ready (materializeDisplay or host-raw
    // identity). Never use setSourceImage here — it re-runs updateDisplayedPixmap
    // and double-applies m_colorAdjust on Gallery/Workspace tiles.
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        item->setPreviewImage(display);
    } else {
        item->setSourceImageReady(display);
    }

    // Layout: ONE rule — ContentXform::layoutSize(fileNative, want).
    // Soft/full sample pixels never define intrinsic (SIZE.md / CONTENT_PIPELINE).
    // The old "crop → display.size()" branch set soft crop pixels as geometry,
    // which collapsed Workspace scale to ~1% and broke second-crop draft size.
    applyContentLayoutSize(item, want);
    // SIZE.md: samples (LQIP / soft / full ladder) never write intrinsic.
    // Cold open keeps the provisional stand-in until sizeReady / cachedSize.
    // Adopting display.size() made 32× LQIP the layout box, then jumped when
    // the durable probe arrived.
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")
        || (want.hasCrop && item->imageSize().width() <= 1)) {
        const QSize isz = item->imageSize();
        if (want.hasCrop && (isz.width() <= 1 || isz.height() <= 1)) {
            qCritical("attachDisplaySample: crop want but intrinsic %dx%d (display %dx%d path=%s)",
                      isz.width(), isz.height(), display.width(), display.height(),
                      qPrintable(path));
        }
    }

    item->setContentHFlip(want.contentHFlip);
    item->setContentVFlip(want.contentVFlip);
    item->setSessionCrop(want.hasCrop, want.cropRect);
    item->setColorAdjustmentsRecord(want.colorAdjust);
    item->setAppliedContentXform(ContentXform::Value::fromState(want));
}


void ImageView::rematerializeItemContent(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Crop draft owns the live sample — pure rematerialize must not soft↔full.
    if (isCropDraftLockedItem(item)) {
        return;
    }
    if (tryRematerializeFromHost(item, want)) {
        return;
    }
    const QString path = item->path();
    // Host must be unoriented. Never bake from preview/display — that double-applies
    // crop when the tile already shows a soft crop (Gallery after Image crop).
    QImage raw = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (raw.isNull() && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        // FullSource without applied xform is still host-shaped (rare).
        raw = item->sourceImage();
    }
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
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
        item->clearDecodedPixels();
    }
    attachDisplaySample(item, display, want, bakeKind);
    if (scheduleFull) {
        scheduleAsyncHostRematerialize(path, sid, want);
    }
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
    const WorkspaceItemState *st = appearance().get(sid);
    if (!st || !SessionAppearance::hasContentAppearance(*st)) {
        return;
    }
    const ContentXform::Value want = ContentXform::Value::fromState(*st);
    const ContentXform::Value applied = item->hasAppliedContentXform()
        ? item->appliedContentXform()
        : ContentXform::Value{};
    if (ContentXform::equal(applied, want) && item->hasDisplayPixels()) {
        // Applied matches store; still fix layout if intrinsic is full-frame.
        applyContentLayoutSize(item, *st);
        return;
    }
    rematerializeItemContent(item, *st);
}


bool ImageView::tryRematerializeFromHost(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return false;
    }
    if (isCropDraftLockedItem(item)) {
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
    attachDisplaySample(item, display, want, kind);
    applyContentLayoutSize(item, want);
    return true;
}


void ImageView::applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Intrinsic is always ContentXform layout of file-native size — never sample
    // pixel dimensions. Using displayImage().size() for crops shrank Workspace
    // tiles to soft resolution (and stretched wrong pixels on re-crop).
    const QString path = item->path();
    QSize fileNative = logicalSizeForPath(path);
    if (!isPositiveSize(fileNative) || fileNative.width() <= 1 || fileNative.height() <= 1
        || (!path.isEmpty() && isProvisionalImageSize(path))) {
        // Fall back: crop rect in recorded source space, or orient-only current.
        if (want.hasCrop && !want.cropRect.isEmpty()) {
            const QSize basis = (want.cropSourceSize.isValid()
                                && want.cropSourceSize.width() > 1)
                                   ? want.cropSourceSize
                                   : item->imageSize();
            const QRect c = SessionAppearance::scaleCropRect(
                want.cropRect.normalized(), want.cropSourceSize, basis);
            if (c.width() > 1 && c.height() > 1) {
                item->setIntrinsicSize(c.size());
            }
        }
        return;
    }
    const QSize lay = ContentXform::layoutSize(fileNative, want);
    if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
        item->setIntrinsicSize(lay);
    }
}


void ImageView::scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                                 const WorkspaceItemState &want)
{
    if (path.isEmpty()) {
        return;
    }
    if (isCropDraftLockedPath(path)) {
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
    if (isCropDraftLockedPath(path)) {
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
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *cur = appearance().get(sid)) {
            if (!ContentXform::equal(ContentXform::Value::fromState(*cur), wantX)) {
                return;
            }
        }
    }
    // Already settled FullSource for this want at ≥ this resolution — skip.
    // Soft (or soft-sized) FullSource with applied identity used to match and
    // discard Prefer/Full async bakes permanently.
    if (item->hasDecodedPixels() && item->hasAppliedContentXform()
        && ContentXform::equal(item->appliedContentXform(), wantX)
        && !item->shouldUpgradeDisplayTo(ImageCache::longEdge(display))) {
        return;
    }
    const QSize before = item->imageSize();
    attachDisplaySample(item, display, want, SessionAppearance::PixelKind::FullSource);
    applyContentLayoutSize(item, want);
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
        ensureWorkspaceQualityClimb();
    } else if (isImageMode()) {
        driveImageFocusSurface();
    }
    if (viewport()) {
        viewport()->update();
    }
}




void ImageView::applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Prefer pure live grade when the tile still holds unbaked host pixels
    // (no orient/crop/grade bake). Avoids materializeDisplay on the GUI.
    const bool contentGeom = want.hasCrop || want.contentHFlip || want.contentVFlip
        || want.contentQuarterTurns != 0;
    if (!contentGeom && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        item->setColorAdjustments(want.colorAdjust);
        const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, item->path(), appearance);
            }
        }
        return;
    }

    const QString path = item->path();
    QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull() && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        host = item->sourceImage();
    }
    if (host.isNull()) {
        item->setColorAdjustmentsRecord(want.colorAdjust);
        item->update();
        return;
    }
    // materializeDisplay asserts NOT GUI when long edge > kGuiMaterializeMaxEdge.
    // Interactive path must stay ≤ that limit (full bake is async on commit).
    const int kInteractiveGradeMaxEdge = ContentXform::kGuiMaterializeMaxEdge;
    if (ImageCache::longEdge(host) > kInteractiveGradeMaxEdge) {
        host = ImageCache::clampToMaxEdge(host, kInteractiveGradeMaxEdge);
    }
    const auto kind = SessionAppearance::PixelKind::SoftPreview;
    const QImage display = SessionAppearance::materializeDisplay(host, want, kind);
    if (display.isNull()) {
        item->setColorAdjustmentsRecord(want.colorAdjust);
        return;
    }
    if (item->hasDecodedPixels()
        && ImageCache::longEdge(item->sourceImage()) > kInteractiveGradeMaxEdge) {
        // Soft stand-in for the drag; keep session id / path on the item.
        item->clearDecodedPixels();
    }
    attachDisplaySample(item, display, want, kind);
    // Filmstrip / Gallery chrome: push soft appearance while dragging so the
    // strip does not wait for the idle commit (and does not require FullSource).
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(sid, item->path(), appearance);
        }
    }
}


void ImageView::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_colorAdjustCommit.schedule(sid, path);
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->start();
    } else {
        flushColorAdjustCommit();
    }
}


void ImageView::flushColorAdjustCommit()
{
    SessionImageId sid = kInvalidSessionImageId;
    QString path;
    if (!m_colorAdjustCommit.take(&sid, &path)) {
        return;
    }
    ImageItem *item = nullptr;
    if (sid != kInvalidSessionImageId) {
        for (ImageItem *it : m_items) {
            if (it && it->sessionId() == sid) {
                item = it;
                break;
            }
        }
    }
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    WorkspaceItemState want;
    if (sid != kInvalidSessionImageId && appearance().contains(sid)) {
        want = appearance().value(sid);
    } else if (item) {
        want = captureState(item);
        want.colorAdjust = item->colorAdjustments();
    } else {
        return;
    }
    if (!item) {
        return;
    }
    // Crop draft freezes pixels; colour commit waits until crop exits.
    if (isCropDraftLockedItem(item)) {
        return;
    }
    // Full rematerialize from host (async when multi-MP). Do **not** write
    // grade into thumtoo durable appearance — SessionAppearanceStore / project
    // already own it; path cache is for orient/crop hints, not slider spam.
    rematerializeItemContent(item, want);
    // Gallery: same session id may be stashed while Image mode edits — the
    // live tile update covers Image/Gallery focus; filmstrip uses the emit.
    const QImage appearance = sessionAppearanceImage(item);
    if (!appearance.isNull()) {
        const SessionImageId emitSid = sid != kInvalidSessionImageId
            ? sid
            : item->sessionId();
        if (emitSid != kInvalidSessionImageId) {
            emit sessionAppearanceChanged(emitSid,
                                          item->path().isEmpty() ? path : item->path(),
                                          appearance);
        }
    }
}


void ImageView::setTargetColorAdjustments(const ColorAdjustments &adj)
{
    ImageItem *item = targetItem();
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    if (!item) {
        return;
    }
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    WorkspaceItemState slot = (sid != kInvalidSessionImageId && appearance().contains(sid))
        ? appearance().value(sid)
        : captureState(item);
    if (sid != kInvalidSessionImageId) {
        slot.sessionId = sid;
        slot.path = item->path();
        slot.colorAdjust = adj;
        appearance().set(sid, slot);
    } else {
        slot.colorAdjust = adj;
    }
    // Fast path while dragging: bake from clamped host (no SQLite / filmstrip).
    applyInteractiveColorGrade(item, slot);
    if (sid != kInvalidSessionImageId || !item->path().isEmpty()) {
        scheduleColorAdjustCommit(sid, item->path());
    }
    emit statusChanged();
}

