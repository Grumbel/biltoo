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


void ImageView::bakeItemRotate90(ImageItem *item, int quarterTurns)
{
    if (!item || quarterTurns == 0) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureContentBakeBeforeState(item);

    const SessionImageId sid = resolveContentEditSessionId(item);
    const int turns = ContentXform::normalizeQuarterTurns(
        beforeSt.contentQuarterTurns + quarterTurns);
    // Keep full-source crop geometry in sync with content orientation so
    // re-entering crop mode still frames the same region.
    WorkspaceItemState cropMap = appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentRotate90(cropMap, quarterTurns);
    if (cropMap.hasCrop) {
        item->setSessionCrop(true, cropMap.cropRect);
    }

    // Absolute want after this edit.
    WorkspaceItemState want = beforeSt;
    want.contentQuarterTurns = turns;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentHFlip = item->contentHFlip();
    want.contentVFlip = item->contentVFlip();
    want.colorAdjust = item->colorAdjustments();

    // ContentXform is ground truth: absolute want from store + delta, pure
    // materialize from unoriented host. Never stack incremental transforms.
    // ≤512 host: GUI pure. Multi-MP: soft stand-in from clamped host + async.
    if (!tryRematerializeFromHost(item, want)) {
        const QString path = item->path();
        const QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
        bool gotDisplay = false;
        if (!host.isNull()) {
            QImage soft = host;
            if (ContentXform::longEdge(host.size())
                > ContentXform::kGuiMaterializeMaxEdge) {
                soft = ImageCache::clampToMaxEdge(
                    host, ContentXform::kGuiMaterializeMaxEdge);
            }
            const QImage display = SessionAppearance::materializeDisplay(
                soft, want, SessionAppearance::PixelKind::SoftPreview);
            if (!display.isNull()) {
                // Soft attach must not be ignored when full was present.
                item->clearDecodedPixels();
                attachDisplaySample(item, display, want,
                                    SessionAppearance::PixelKind::SoftPreview);
                gotDisplay = true;
            }
        }
        if (!gotDisplay) {
            // No host at all: last resort incremental on whatever is shown.
            item->bakeRotate90(quarterTurns);
        }
        applyContentLayoutSize(item, want);
        scheduleAsyncHostRematerialize(path, sid, want);
    } else {
        applyContentLayoutSize(item, want);
    }

    // Write ContentXform absolute state first — before commitItemSessionEdit
    // captureState, so commit cannot resurrect stale path-map turns.
    {
        WorkspaceItemState s = want;
        s.sessionId = sid;
        s.path = item->path();
        s.orientation = 0.0;
        s.contentQuarterTurns = turns;
        if (sid != kInvalidSessionImageId) {
            // Preserve placement fields from previous appearance when present.
            if (const WorkspaceItemState *prev = appearance().get(sid)) {
                s.pos = prev->pos;
                s.scale = prev->scale;
                s.scaleY = prev->scaleY;
                s.shear = prev->shear;
                s.rotation = prev->rotation;
                s.opacity = prev->opacity;
                s.z = prev->z;
                s.hFlip = prev->hFlip;
                s.vFlip = prev->vFlip;
                s.sessionIndex = prev->sessionIndex;
            }
            appearance().set(sid, s);
            persistDurableContentAppearance(item, s, "bakeRotate");
        }
        // Keep path map content fields in sync so pack afterEach cannot leave
        // stale turns for any reader that still peeks at m_itemStateBook.byPath.
        {
            WorkspaceItemState pathSlot;
            if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
                pathSlot = *st;
            }
            pathSlot.path = item->path();
            pathSlot.contentQuarterTurns = turns;
            pathSlot.contentHFlip = want.contentHFlip;
            pathSlot.contentVFlip = want.contentVFlip;
            pathSlot.hasCrop = want.hasCrop;
            pathSlot.cropRect = want.cropRect;
            pathSlot.cropRotation = want.cropRotation;
            pathSlot.cropSourceSize = want.cropSourceSize;
            m_itemStateBook.set(item->path(), pathSlot);
        }
        item->setAppliedContentXform(ContentXform::Value::fromState(s));
    }

    commitItemSessionEdit(item);

    // Image mode: contentRect axes may have swapped — refresh tight sceneRect
    // so pan/fit are not locked to the pre-rotate box.
    if (isImageMode() && m_scene && m_items.size() == 1) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.cropRotation = cropMap.cropRotation;
    afterSt.cropSourceSize = cropMap.cropSourceSize;
    afterSt.contentHFlip = item->contentHFlip();
    afterSt.contentVFlip = item->contentVFlip();
    afterSt.contentQuarterTurns = turns;
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(tr("Rotate"), item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
}


void ImageView::bakeItemFlip(ImageItem *item, bool horizontal, bool vertical)
{
    if (!item || (!horizontal && !vertical)) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureContentBakeBeforeState(item);

    // Content-orientation flags track the source raster so durable crop mapping
    // (flip then quarter-turn on the full raster) stay consistent with the
    // display-space flip just applied to the oriented pixels.
    //
    // With quarter-turns t: display H/V axes map to source axes as
    //   t=0,2: same axes;  t=1,3: H↔V  (conjugation through 90°/270° CW).
    // Crop stays in post-content space, so mapCropThroughContentFlip below
    // still uses the display axes the user pressed.
    bool h = beforeSt.contentHFlip;
    bool v = beforeSt.contentVFlip;
    int turns = beforeSt.contentQuarterTurns % 4;
    if (turns < 0) {
        turns += 4;
    }
    const bool swapAxes = (turns == 1 || turns == 3);
    const bool srcH = swapAxes ? vertical : horizontal;
    const bool srcV = swapAxes ? horizontal : vertical;
    if (srcH) {
        h = !h;
    }
    if (srcV) {
        v = !v;
    }

    const SessionImageId sid = resolveContentEditSessionId(item);
    WorkspaceItemState cropMap = appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentFlip(cropMap, horizontal, vertical);
    if (cropMap.hasCrop) {
        item->setSessionCrop(true, cropMap.cropRect);
    }

    WorkspaceItemState want = beforeSt;
    want.contentHFlip = h;
    want.contentVFlip = v;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentQuarterTurns = cropMap.contentQuarterTurns;

    // Prefer pure rematerialize from unoriented host; else incremental + async.
    item->setContentHFlip(h);
    item->setContentVFlip(v);
    if (!tryRematerializeFromHost(item, want)) {
        item->bakeFlip(horizontal, vertical);
        item->setContentHFlip(h);
        item->setContentVFlip(v);
        scheduleAsyncHostRematerialize(item->path(), sid, want);
    }
    applyContentLayoutSize(item, want);

    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState s = captureState(item);
        s.sessionId = sid;
        s.hasCrop = cropMap.hasCrop;
        s.cropRect = cropMap.cropRect;
        s.cropRotation = cropMap.cropRotation;
        s.cropSourceSize = cropMap.cropSourceSize;
        s.contentHFlip = h;
        s.contentVFlip = v;
        s.contentQuarterTurns = cropMap.contentQuarterTurns;
        appearance().set(sid, s);
        persistDurableContentAppearance(item, s, "bakeFlip");
    } else if (cropMap.hasCrop) {
        WorkspaceItemState s = captureState(item);
        s.hasCrop = true;
        s.cropRect = cropMap.cropRect;
        s.cropRotation = cropMap.cropRotation;
        s.cropSourceSize = cropMap.cropSourceSize;
        s.contentHFlip = h;
        s.contentVFlip = v;
        m_itemStateBook.set(item->path(), s);
    }

    commitItemSessionEdit(item);

    {
        WorkspaceItemState tag;
        tag.contentHFlip = h;
        tag.contentVFlip = v;
        tag.contentQuarterTurns = beforeSt.contentQuarterTurns;
        tag.hasCrop = item->sessionHasCrop();
        tag.cropRect = item->sessionCropRect();
        tag.cropRotation = cropMap.cropRotation;
        tag.cropSourceSize = cropMap.cropSourceSize;
        item->setAppliedContentXform(ContentXform::Value::fromState(tag));
    }

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.cropRotation = cropMap.cropRotation;
    afterSt.cropSourceSize = cropMap.cropSourceSize;
    afterSt.contentHFlip = h;
    afterSt.contentVFlip = v;
    afterSt.contentQuarterTurns = beforeSt.contentQuarterTurns;
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(horizontal && !vertical ? tr("Flip horizontal")
                          : vertical && !horizontal ? tr("Flip vertical")
                          : tr("Flip"),
                           item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
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

