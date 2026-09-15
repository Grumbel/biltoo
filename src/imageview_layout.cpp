// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "biltoo_thread.h"
#include "thumtoocache.h"

#include <QDebug>
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"
#include "contentxform.h"
#include "imagecache.h"

#include <QHash>
#include <QImage>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QTransform>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTimer>
#include <QSet>
#include <QThreadPool>
#include <QPointer>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>
#include <algorithm>

void ImageView::applyItemModeFlags(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Strict separation:
    //   Workspace → movable + handles
    //   Gallery   → selectable only (open on click), no chrome
    //   Image     → static, no selection chrome
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    } else if (isGalleryMode()) {
        item->setGallerySelectable(true);
        item->setScaleHandlesEnabled(false);
    } else {
        item->setGalleryCellSize({});
        item->setInteractive(false);
        item->setScaleHandlesEnabled(false);
    }
}

WorkspaceItemState ImageView::captureState(const ImageItem *item) const
{
    WorkspaceItemState s;
    s.path = item->path();
    s.sessionId = item->sessionId();
    s.sessionIndex = item->sessionIndex(); // order cache only
    s.pos = item->pos();
    s.scale = item->itemScaleX();
    s.scaleY = item->itemScaleY();
    s.shear = item->itemShear();
    s.rotation = item->itemRotation(); // placement only
    s.orientation = 0.0;
    s.opacity = item->itemOpacity();
    s.z = item->stackZ();
    s.hFlip = item->itemHFlip();
    s.vFlip = item->itemVFlip();
    // Live item is authoritative for per-instance crop rect + content flips.
    // cropRotation / cropSourceSize are not stored on ImageItem — load them
    // from the session-image appearance store (or path map for unbound).
    s.hasCrop = item->sessionHasCrop();
    s.cropRect = item->sessionCropRect();
    s.contentHFlip = item->contentHFlip();
    s.contentVFlip = item->contentVFlip();
    s.colorAdjust = item->colorAdjustments();
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *app = m_appearance.get(sid)) {
            s.cropRotation = app->cropRotation;
            s.cropSourceSize = app->cropSourceSize;
            // Appearance store is sole content-orient authority for bound ids
            // (ContentXform ground truth). Never fall back to path map when
            // turns==0 — that resurrected stale 1..3 after a full 360° and
            // corrupted Gallery on the 4th rotate (commit → captureState →
            // m_appearance overwrite).
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(app->contentQuarterTurns);
            if (!s.contentHFlip && app->contentHFlip) {
                s.contentHFlip = true;
            }
            if (!s.contentVFlip && app->contentVFlip) {
                s.contentVFlip = true;
            }
            if (!s.hasCrop && app->hasCrop) {
                s.hasCrop = app->hasCrop;
                s.cropRect = app->cropRect;
            }
        } else if (item->hasAppliedContentXform()) {
            // Store empty but live fingerprint exists (mid-edit).
            s.contentQuarterTurns = item->appliedContentXform().quarterTurns;
            s.contentHFlip = item->appliedContentXform().hFlip;
            s.contentVFlip = item->appliedContentXform().vFlip;
        }
        // Bound session image: path map is placement-only. Do not read content
        // turns/crop meta from m_itemStates.
    } else {
        // Unbound tile: path map may hold content orient.
        const auto prev = m_itemStates.constFind(item->path());
        if (prev != m_itemStates.cend()) {
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(prev->contentQuarterTurns);
            s.cropRotation = prev->cropRotation;
            s.cropSourceSize = prev->cropSourceSize;
            if (s.sessionIndex < 0 && prev->sessionIndex >= 0) {
                s.sessionIndex = prev->sessionIndex;
            }
        } else if (item->hasAppliedContentXform()) {
            s.contentQuarterTurns = item->appliedContentXform().quarterTurns;
        }
    }
    // Placement path-map hint for session index only (bound or unbound).
    if (s.sessionIndex < 0) {
        const auto prev = m_itemStates.constFind(item->path());
        if (prev != m_itemStates.cend() && prev->sessionIndex >= 0) {
            s.sessionIndex = prev->sessionIndex;
        }
    }
    return s;
}

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    item->setPos(state.pos);
    item->setItemScale(state.scale, state.scaleY > 0.0 ? state.scaleY : state.scale);
    item->setItemShear(state.shear);
    item->setItemRotation(state.rotation);
    item->setItemOpacity(state.opacity);
    item->setStackZ(state.z);
    item->setItemHFlip(state.hFlip);
    item->setItemVFlip(state.vFlip);
    // Content pixels/applied are set at install (installDisplayPixels / attachDisplaySample),
    // not here — otherwise a second apply would crop already-cropped display.
}

void ImageView::rememberItemState(ImageItem *item)
{
    if (!item) {
        return;
    }
    const SessionImageId sid =
        item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);

    // Image mode must not overwrite Workspace placement (pos / scale / free tilt).
    // Bound session images: appearance lives only in m_appearance (Phase 2).
    if (isImageMode()) {
        if (sid != kInvalidSessionImageId) {
            // Leave path-map placement untouched; do not last-write crop/flip by path.
            return;
        }
        // Unbound legacy tile: path map is the only store.
        WorkspaceItemState s;
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend()) {
            s = *it;
        } else {
            s.path = item->path();
        }
        s.sessionIndex = item->sessionIndex() >= 0 ? item->sessionIndex() : s.sessionIndex;
        s.hFlip = item->itemHFlip();
        s.vFlip = item->itemVFlip();
        s.orientation = 0.0;
        s.hasCrop = item->sessionHasCrop();
        s.cropRect = item->sessionCropRect();
        s.contentHFlip = item->contentHFlip();
        s.contentVFlip = item->contentVFlip();
    s.colorAdjust = item->colorAdjustments();
        if (it != m_itemStates.cend()) {
            s.contentQuarterTurns = it->contentQuarterTurns;
            s.cropRotation = it->cropRotation;
            s.cropSourceSize = it->cropSourceSize;
        }
        m_itemStates.insert(item->path(), s);
        return;
    }
    // Workspace / Gallery: path map is legacy placement for *unbound* tiles only.
    // Bound session images: placement + content live in m_appearance (by id).
    // Never write pose by path — duplicates would steal each other's layout.
    if (item->sessionId() != kInvalidSessionImageId) {
        WorkspaceItemState slot = captureState(item);
        slot.sessionId = item->sessionId();
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        m_appearance.set(item->sessionId(), slot);
        return;
    }
    m_itemStates.insert(item->path(), captureState(item));
}

QImage ImageView::sessionAppearanceImage(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Content 90°/flip/crop/grade are baked into source *or* soft-preview pixels
    // once an applied ContentXform is set. Gallery soft tiles often have only
    // m_preview — using sourceImage alone left the filmstrip on the unflipped
    // ladder after Gallery flip/rotate. Placement rotation is not included.
    QImage img = item->displayImage();
    if (img.isNull()) {
        return {};
    }
    // Legacy live flip flags (should be empty after bake).
    if (item->itemHFlip() || item->itemVFlip()) {
        Qt::Orientations axes;
        if (item->itemHFlip()) {
            axes |= Qt::Horizontal;
        }
        if (item->itemVFlip()) {
            axes |= Qt::Vertical;
        }
        img = img.flipped(axes);
    }
    // Live grade only when display is still unbaked host (no applied xform).
    // Re-applying on a materialize bake double-grades the filmstrip override.
    if (!item->hasAppliedContentXform()) {
        const ColorAdjustments adj = item->colorAdjustments();
        if (!adj.isIdentity()) {
            img = applyColorAdjustments(img, adj);
        }
    }
    return img;
}

QImage ImageView::imageWithSessionAppearance(const QImage &src, SessionImageId sid,
                                             const QString &path) const
{
    if (src.isNull()) {
        return {};
    }
    const WorkspaceItemState *app = nullptr;
    WorkspaceItemState fallback;
    if (sid != kInvalidSessionImageId) {
        app = m_appearance.get(sid);
    }
    if ((!app || !SessionAppearance::hasContentAppearance(*app)) && !path.isEmpty()) {
        const auto it = m_itemStates.constFind(path);
        if (it != m_itemStates.cend()) {
            fallback = *it;
            app = &fallback;
        }
    }
    // Durable XDG appearance when session store is empty (slideshow may paint a
    // path before installDisplayPixels seeds m_appearance for that id).
    // Orient/flip only — never adopt path crop into an id-keyed soft paint.
    if ((!app || !SessionAppearance::hasContentAppearance(*app)) && !path.isEmpty()) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored)
            && (stored.contentHFlip || stored.contentVFlip
                || stored.contentQuarterTurns != 0)) {
            fallback = {};
            fallback.path = path;
            fallback.sessionId = sid;
            fallback.contentHFlip = stored.contentHFlip;
            fallback.contentVFlip = stored.contentVFlip;
            fallback.contentQuarterTurns = stored.contentQuarterTurns;
            app = &fallback;
        }
    }
    if (!app || !SessionAppearance::hasContentAppearance(*app)) {
        return src;
    }
    // Single pipeline — SoftPreview scales crop into soft pixel space
    // (SessionAppearance::materializeDisplay contract). Never strip crop.
    const QImage out = SessionAppearance::materializeDisplay(
        src, *app, SessionAppearance::PixelKind::SoftPreview);
    return out.isNull() ? src : out;
}





WorkspaceItemState ImageView::sessionAppearanceValue(SessionImageId id) const
{
    if (id == kInvalidSessionImageId) {
        return {};
    }
    return m_appearance.value(id);
}

bool ImageView::hasSessionAppearance(SessionImageId id) const
{
    return id != kInvalidSessionImageId && m_appearance.contains(id);
}

void ImageView::setSessionAppearance(SessionImageId id, const WorkspaceItemState &state)
{
    if (id == kInvalidSessionImageId) {
        return;
    }
    m_appearance.set(id, state);
}

WorkspaceItemState ImageView::captureContentBakeBeforeState(ImageItem *item) const
{
    // ContentXform ground truth: applied fingerprint > appearance store >
    // captureState placement. Never path-map turns for bound ids.
    WorkspaceItemState beforeSt = captureState(item);
    beforeSt.hasCrop = item->sessionHasCrop();
    beforeSt.cropRect = item->sessionCropRect();
    beforeSt.contentHFlip = item->contentHFlip();
    beforeSt.contentVFlip = item->contentVFlip();
    const SessionImageId sid0 = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
    beforeSt.sessionId = sid0;
    if (item->hasAppliedContentXform()) {
        const ContentXform::Value x = item->appliedContentXform();
        beforeSt.contentQuarterTurns = x.quarterTurns;
        beforeSt.contentHFlip = x.hFlip;
        beforeSt.contentVFlip = x.vFlip;
        beforeSt.hasCrop = x.hasCrop;
        beforeSt.cropRect = x.cropRect;
        beforeSt.cropSourceSize = x.cropSourceSize;
        beforeSt.cropRotation = x.cropRotation;
        return beforeSt;
    }
    if (sid0 != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid0)) {
            beforeSt.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(it->contentQuarterTurns);
            beforeSt.contentHFlip = it->contentHFlip;
            beforeSt.contentVFlip = it->contentVFlip;
        }
    }
    return beforeSt;
}

SessionImageId ImageView::resolveContentEditSessionId(ImageItem *item) const
{
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_currentSessionId;
    }
    return sid;
}

WorkspaceItemState ImageView::appearanceCropMapForEdit(ImageItem *item,
                                                       const WorkspaceItemState &fallback,
                                                       SessionImageId sid) const
{
    Q_UNUSED(item);
    WorkspaceItemState cropMap = fallback;
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            cropMap = *it;
        }
    }
    return cropMap;
}

void ImageView::persistDurableContentAppearance(ImageItem *item, const WorkspaceItemState &s,
                                                const char *debugTag)
{
    // Bound session images: path XDG may keep orient/flip as a file-level hint,
    // but never crop (SessionAppearanceStore owns crop by SessionImageId).
    const bool bound = item && item->sessionId() != kInvalidSessionImageId;
    const bool writeCrop = !bound && s.hasCrop && !s.cropRect.isEmpty();
    const bool contentful =
        s.contentHFlip || s.contentVFlip
        || s.contentQuarterTurns != 0
        || writeCrop;
    if (contentful) {
        ThumtooCache::StoredContentAppearance stored;
        stored.contentHFlip = s.contentHFlip;
        stored.contentVFlip = s.contentVFlip;
        stored.contentQuarterTurns = s.contentQuarterTurns;
        stored.hasCrop = writeCrop;
        if (writeCrop) {
            stored.cropRect = s.cropRect;
            stored.cropSourceSize = s.cropSourceSize;
            stored.cropRotation = s.cropRotation;
        }
        ThumtooCache::saveContentAppearance(item->path(), stored);
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_APPEARANCE")) {
            qWarning().noquote()
                << QStringLiteral("[appearance] %1 save path=%2 h=%3 v=%4 turns=%5")
                       .arg(QLatin1String(debugTag))
                       .arg(item->path())
                       .arg(s.contentHFlip)
                       .arg(s.contentVFlip)
                       .arg(s.contentQuarterTurns);
        }
    } else {
        ThumtooCache::clearContentAppearance(item->path());
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_APPEARANCE")) {
            qWarning().noquote()
                << QStringLiteral("[appearance] %1 clear (identity) path=%2")
                       .arg(QLatin1String(debugTag))
                       .arg(item->path());
        }
    }
}

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
    {
        const QSize cur = item->imageSize();
        if ((cur.width() <= 1 || cur.height() <= 1)
            && display.width() > 1 && display.height() > 1) {
            // Cold open only: no durable native yet.
            item->setIntrinsicSize(display.size());
        }
    }
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
        : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
    if (raw.isNull()) {
        // No unoriented host: schedule async; do not claim applied yet.
        if (!path.isEmpty() && SessionAppearance::hasContentAppearance(want)) {
            scheduleAsyncHostRematerialize(path, sid, want);
        }
        return;
    }
    const int maxGui = ContentXform::kGuiMaterializeMaxEdge;
    int edge = qMax(raw.width(), raw.height());
    QImage host = raw;
    SessionAppearance::PixelKind bakeKind = item->hasDecodedPixels()
        ? SessionAppearance::PixelKind::FullSource
        : SessionAppearance::PixelKind::SoftPreview;
    bool scheduleFull = false;
    if (edge > maxGui) {
        // Soft stand-in now (crop/orient visible); full bake async.
        host = ImageCache::clampToMaxEdge(raw, maxGui);
        edge = qMax(host.width(), host.height());
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
    const WorkspaceItemState *st = m_appearance.get(sid);
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
    const QString path = item->path();
    const QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull()) {
        return false;
    }
    if (qMax(host.width(), host.height()) > ContentXform::kGuiMaterializeMaxEdge) {
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
    if (qMax(hostProbe.width(), hostProbe.height())
        <= ContentXform::kGuiMaterializeMaxEdge) {
        return; // GUI path already handled by tryRematerializeFromHost
    }
    const quint64 gen = m_loadGeneration.load();
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
        if (const WorkspaceItemState *cur = m_appearance.get(sid)) {
            if (!ContentXform::equal(ContentXform::Value::fromState(*cur), wantX)) {
                return;
            }
        }
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
            if (qMax(host.width(), host.height())
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
            if (const WorkspaceItemState *prev = m_appearance.get(sid)) {
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
            m_appearance.set(sid, s);
            persistDurableContentAppearance(item, s, "bakeRotate");
        }
        // Keep path map content fields in sync so pack afterEach cannot leave
        // stale turns for any reader that still peeks at m_itemStates.
        {
            WorkspaceItemState pathSlot;
            const auto it = m_itemStates.constFind(item->path());
            if (it != m_itemStates.cend()) {
                pathSlot = *it;
            }
            pathSlot.path = item->path();
            pathSlot.contentQuarterTurns = turns;
            pathSlot.contentHFlip = want.contentHFlip;
            pathSlot.contentVFlip = want.contentVFlip;
            pathSlot.hasCrop = want.hasCrop;
            pathSlot.cropRect = want.cropRect;
            pathSlot.cropRotation = want.cropRotation;
            pathSlot.cropSourceSize = want.cropSourceSize;
            m_itemStates.insert(item->path(), pathSlot);
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
        m_appearance.set(sid, s);
        persistDurableContentAppearance(item, s, "bakeFlip");
    } else if (cropMap.hasCrop) {
        WorkspaceItemState s = captureState(item);
        s.hasCrop = true;
        s.cropRect = cropMap.cropRect;
        s.cropRotation = cropMap.cropRotation;
        s.cropSourceSize = cropMap.cropSourceSize;
        s.contentHFlip = h;
        s.contentVFlip = v;
        m_itemStates.insert(item->path(), s);
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

void ImageView::persistSessionAppearanceSlot(ImageItem *item)
{
    // Per-session-image appearance is a value copy keyed by stable id.
    SessionImageId sid = item->sessionId();
    // Image mode may bind the cursor id when the live item is not yet tagged.
    // Workspace/Gallery must not invent an id — that merges edits onto peers.
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_currentSessionId;
    }
    WorkspaceItemState contentSlot;
    bool haveContentSlot = false;
    if (sid != kInvalidSessionImageId) {
        if (item->sessionId() == kInvalidSessionImageId) {
            item->setSessionId(sid);
        }
        WorkspaceItemState slot = captureState(item);
        slot.sessionId = sid;
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        // ContentXform applied fingerprint wins. Do NOT resurrect prev turns
        // when capture says 0 — that is a real full-circle identity and was
        // the Gallery 4th-rotate corruption path.
        if (item->hasAppliedContentXform()) {
            const ContentXform::Value x = item->appliedContentXform();
            slot.contentQuarterTurns = x.quarterTurns;
            slot.contentHFlip = x.hFlip;
            slot.contentVFlip = x.vFlip;
            if (x.hasCrop) {
                slot.hasCrop = true;
                slot.cropRect = x.cropRect;
                slot.cropSourceSize = x.cropSourceSize;
                slot.cropRotation = x.cropRotation;
            }
        }
        m_appearance.set(sid, slot);
        contentSlot = slot;
        haveContentSlot = true;
    } else {
        // Unbound tile: still persist content-hash state for the file.
        contentSlot = captureState(item);
        contentSlot.contentHFlip = item->contentHFlip();
        contentSlot.contentVFlip = item->contentVFlip();
        contentSlot.hasCrop = item->sessionHasCrop();
        contentSlot.cropRect = item->sessionCropRect();
        haveContentSlot = true;
    }
    if (haveContentSlot) {
        // Durable local state (XDG_STATE_HOME/thumtoo): content-hash keyed.
        // Bound: orient/flip only — crop lives in SessionAppearanceStore by id.
        // Unbound: may include crop (legacy single-instance path edit).
        // Writing identity deletes the SQLite row; intentional clear goes
        // through clearContentAppearance (Reset / undo-to-identity).
        const bool writeCrop = (sid == kInvalidSessionImageId)
            && contentSlot.hasCrop && !contentSlot.cropRect.isEmpty();
        const bool contentful =
            contentSlot.contentHFlip || contentSlot.contentVFlip
            || contentSlot.contentQuarterTurns != 0
            || writeCrop;
        if (contentful) {
            ThumtooCache::StoredContentAppearance stored;
            stored.contentHFlip = contentSlot.contentHFlip;
            stored.contentVFlip = contentSlot.contentVFlip;
            stored.contentQuarterTurns = contentSlot.contentQuarterTurns;
            stored.hasCrop = writeCrop;
            if (writeCrop) {
                stored.cropRect = contentSlot.cropRect;
                stored.cropSourceSize = contentSlot.cropSourceSize;
                stored.cropRotation = contentSlot.cropRotation;
            }
            ThumtooCache::saveContentAppearance(item->path(), stored);
        }
    }
    if (sid != kInvalidSessionImageId) {
        // Bound: do not last-write appearance onto the path map (duplicates
        // share a path). Placement remains in m_itemStates from Workspace
        // rememberItemState / snapshot only.
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            // Id-keyed only — path signals paint every filmstrip row with
            // the same file (IDENTITY.md).
            emit sessionAppearanceChanged(sid, item->path(), appearance);
            emit sessionCropApplied(sid, item->path(), appearance,
                                    item->sessionHasCrop());
        }
    }
}

void ImageView::syncSessionEditPeers(ImageItem *item)
{
    // Propagate pixel / flip / orientation session edits to matching canvas and
    // stashed instances. Placement (pos, scale, free tilt) is preserved.
    const QString path = item->path();
    // Strict identity: only a valid SessionImageId. Never m_currentSessionId
    // fallback here — that would push this item's pixels onto another tile.
    const SessionImageId sessionId = item->sessionId();
    const QImage src = item->sourceImage();
    const bool hFlip = item->itemHFlip();
    const bool vFlip = item->itemVFlip();

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
                if (const WorkspaceItemState *st = m_appearance.get(sessionId)) {
                    want = *st;
                }
            }
            if (!SessionAppearance::hasContentAppearance(want) && item->hasAppliedContentXform()) {
                ContentXform::Value x = item->appliedContentXform();
                x.applyToState(want);
            }
            const auto kind = !src.isNull()
                ? SessionAppearance::PixelKind::FullSource
                : SessionAppearance::PixelKind::SoftPreview;
            attachDisplaySample(other, baked, want, kind);
        } else if (sessionId != kInvalidSessionImageId) {
            if (const WorkspaceItemState *st = m_appearance.get(sessionId)) {
                applyContentLayoutSize(other, *st);
            }
        }
        other->setItemHFlip(hFlip);
        other->setItemVFlip(vFlip);
    };
    for (ImageItem *other : peers) {
        syncOne(other);
    }
}

void ImageView::updateWorkspaceSavedAppearance(ImageItem *item)
{
    // Durable snapshot: update the entry for this session image id.
    const SessionImageId sessionId = item->sessionId();
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    const WorkspaceItemState *st = m_appearance.get(sessionId);
    if (!st) {
        return;
    }
    const QString path = item->path();
    const bool hFlip = item->itemHFlip();
    const bool vFlip = item->itemVFlip();
    for (WorkspaceItemState &slot : m_workspace.savedItems()) {
        if (slot.sessionId != sessionId) {
            continue;
        }
        slot.hasCrop = st->hasCrop;
        slot.cropRect = st->cropRect;
        slot.hFlip = hFlip;
        slot.vFlip = vFlip;
        slot.contentQuarterTurns = st->contentQuarterTurns;
        slot.contentHFlip = st->contentHFlip;
        slot.contentVFlip = st->contentVFlip;
        slot.orientation = 0.0;
        slot.sessionId = sessionId;
        slot.path = path;
    }
}

void ImageView::commitItemSessionEdit(ImageItem *item)
{
    if (!item) {
        return;
    }
    rememberItemState(item);
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
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(sid, item->path(), appearance);
            if (item->sessionHasCrop()
                || (m_appearance.contains(sid)
                    && m_appearance.value(sid).hasCrop)) {
                emit sessionCropApplied(sid, item->path(), appearance, /*hasCrop=*/true);
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


ImageItem *ImageView::findItemByPath(const QString &path) const
{
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            return item;
        }
    }
    return nullptr;
}

ImageItem *ImageView::findPreferredItemForPath(const QString &path) const
{
    if (path.isEmpty() || !m_scene) {
        return nullptr;
    }
    // Selected tiles with this path win (duplicate open / crop must hit B not A).
    ImageItem *selectedMatch = nullptr;
    int selectedMatches = 0;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->path() != path) {
            continue;
        }
        ++selectedMatches;
        if (!selectedMatch) {
            selectedMatch = item;
        }
    }
    if (selectedMatches == 1) {
        return selectedMatch;
    }
    // Sole live instance of this path.
    ImageItem *only = nullptr;
    int liveMatches = 0;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        ++liveMatches;
        only = item;
    }
    if (liveMatches == 1) {
        return only;
    }
    // Ambiguous multi-match with no exclusive selection — caller falls back.
    return nullptr;
}

ImageItem *ImageView::findItemBySessionIndex(int sessionIndex) const
{
    if (sessionIndex < 0) {
        return nullptr;
    }
    for (ImageItem *item : m_items) {
        if (item->sessionIndex() == sessionIndex) {
            return item;
        }
    }
    return nullptr;
}

ImageItem *ImageView::findItemBySessionId(SessionImageId sessionId) const
{
    if (sessionId == kInvalidSessionImageId) {
        return nullptr;
    }
    for (ImageItem *item : m_items) {
        if (item->sessionId() == sessionId) {
            return item;
        }
    }
    for (ImageItem *item : m_workspace.stashedItems()) {
        if (item && item->sessionId() == sessionId) {
            return item;
        }
    }
    return nullptr;
}


bool ImageView::hasPendingSessionBindForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }
    for (const PendingSessionBind &b : m_pendingSessionBinds) {
        if (b.path == path) {
            return true;
        }
    }
    return false;
}

int ImageView::countPendingSessionBinds(const QString &path) const
{
    int n = 0;
    for (const PendingSessionBind &b : m_pendingSessionBinds) {
        if (b.path == path) {
            ++n;
        }
    }
    return n;
}

void ImageView::purgeSatisfiedPendingBinds(const QString &path)
{
    for (int bi = m_pendingSessionBinds.size() - 1; bi >= 0; --bi) {
        const PendingSessionBind &b = m_pendingSessionBinds.at(bi);
        if (b.path != path || b.id == kInvalidSessionImageId) {
            continue;
        }
        if (findItemBySessionId(b.id)) {
            m_pendingSessionBinds.removeAt(bi);
        }
    }
}

bool ImageView::takePendingSessionBind(const QString &path, PendingSessionBind *out)
{
    if (!out || path.isEmpty()) {
        return false;
    }
    for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
        if (m_pendingSessionBinds.at(bi).path != path) {
            continue;
        }
        *out = m_pendingSessionBinds.takeAt(bi);
        return true;
    }
    return false;
}

void ImageView::applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound)
{
    if (!item || !bound.hasScenePos) {
        return;
    }
    // Explicit drop pose: free-form identity placement only (never revive
    // gallery pack scale/cell or a prior non-uniform footprint scale).
    item->setGalleryCellSize({});
    item->setPos(bound.scenePos);
    item->setItemScale(1.0, 1.0);
    item->setItemRotation(0.0);
    item->setItemShear(0.0);
    item->setItemOpacity(1.0);
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    item->setStackZ(m_items.size() - 1);
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    }
    m_pendingScenePos.remove(item->path());
    rememberItemState(item);
}

bool ImageView::installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image)
{
    if (!item || image.isNull() || item->hasDecodedPixels()) {
        return false;
    }
    // Drop / LoadAdd placeholder → full. Never non-uniform scale: that stretched
    // oriented (or just aspect-correct) pixels into the provisional footprint and
    // looked like "wrong rotation + stretch" on drag-drop without any rotate.
    const QSize before = item->imageSize();
    const qreal sx0 = item->itemScaleX();
    const qreal sy0 = item->itemScaleY() > 0.0 ? item->itemScaleY() : sx0;
    const qreal footW = before.width() * sx0;
    const qreal footH = before.height() * sy0;
    // Leave Gallery pack geometry on Workspace tiles.
    item->setGalleryCellSize({});
    installDisplayPixels(item, image, SessionAppearance::PixelKind::FullSource,
                         item->sessionId());
    const QSize after = item->imageSize();
    const bool grew = before.isValid() && after.isValid()
        && (before.width() != after.width() || before.height() != after.height());
    if (grew && isWorkspaceMode() && m_layoutMode == LayoutMode::FreeForm
        && after.width() > 0 && after.height() > 0) {
        const bool neutralScale =
            qAbs(sx0 - 1.0) < 1e-6 && qAbs(sy0 - 1.0) < 1e-6;
        if (neutralScale) {
            // Fresh drop: 1:1 scene units = content pixels (no footprint squash).
            item->setItemScale(1.0, 1.0);
        } else {
            // Prior intentional scale (e.g. moved from Gallery): fit uniformly.
            const qreal s = qMin(footW / qreal(after.width()),
                                 footH / qreal(after.height()));
            if (s > 1e-6) {
                item->setItemScale(s, s);
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

bool ImageView::takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                                 PendingSessionBind *out)
{
    if (!out || path.isEmpty() || !item) {
        return false;
    }
    for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
        if (m_pendingSessionBinds.at(bi).path != path) {
            continue;
        }
        const PendingSessionBind candidate = m_pendingSessionBinds.at(bi);
        if (candidate.id != kInvalidSessionImageId) {
            if (ImageItem *owner = findItemBySessionId(candidate.id)) {
                if (owner != item) {
                    m_pendingSessionBinds.removeAt(bi);
                    --bi;
                    continue;
                }
            }
        }
        *out = m_pendingSessionBinds.takeAt(bi);
        if (out->id != kInvalidSessionImageId) {
            item->setSessionId(out->id);
        }
        if (out->index >= 0 && out->id != kInvalidSessionImageId) {
            item->setSessionIndex(out->index);
        }
        return true;
    }
    return false;
}

void ImageView::placeNewLoadAddItem(ImageItem *item, const QString &path,
                                    const QImage &image, bool haveBound,
                                    const PendingSessionBind &bound)
{
    if (!item) {
        return;
    }
    if (isGalleryMode()) {
        // Packed layout owns pose; keep item transform neutral.
        item->setItemRotation(0.0);
        item->setItemShear(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
        item->setItemOpacity(1.0);
        return;
    }
    if (haveBound && bound.hasScenePos) {
        // Explicit drop: place at the drop point (new placement).
        applyPendingBindScenePos(item, bound);
        return;
    }
    if (haveBound && bound.id != kInvalidSessionImageId
        && m_appearance.get(bound.id)) {
        // Thumbnail membership toggle: restore last Workspace pose.
        applyState(item, *m_appearance.get(bound.id));
        return;
    }
    if (m_pendingScenePos.contains(path)) {
        const QPointF pos = m_pendingScenePos.take(path);
        item->setPos(pos);
        item->setItemScale(1.0);
        item->setItemRotation(0.0);
        item->setItemOpacity(1.0);
        item->setStackZ(m_items.size() - 1);
        return;
    }
    const auto it = m_itemStates.constFind(path);
    if (it != m_itemStates.cend()) {
        applyState(item, *it);
        return;
    }
    WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
    const QSizeF sz(image.width(), image.height());
    s.pos = findEmptyPlacement(sz);
    applyState(item, s);
}



QList<ImageItem *> ImageView::collectItemsForSessionId(SessionImageId sessionId) const
{
    QList<ImageItem *> doomed;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *item : list) {
            if (item && item->sessionId() == sessionId && !doomed.contains(item)) {
                doomed.append(item);
            }
        }
    };
    collect(m_items);
    collect(m_workspace.stashedItems());
    collect(m_gallery.stashedItems());
    return doomed;
}

QStringList ImageView::destroySessionIdItems(const QList<ImageItem *> &doomed)
{
    QStringList removedPaths;
    for (ImageItem *item : doomed) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        removedPaths.append(path);
        // Drop in-flight decodes so a late LoadAdd cannot create a tile or
        // call applyLayout after this session image is gone.
        m_pendingWorkspacePaths.remove(path);
        gallerySoftResetPath(path);
        m_pendingScenePos.remove(path);
        m_pendingSessionIndexByPath.remove(path);
        // destroyCanvasItem clears selection anchor / drag pointers and
        // removes from m_items and both stashes (safe if already only in one).
        destroyCanvasItem(item);
    }
    return removedPaths;
}

void ImageView::prunePendingBindsAndSavedForSessionId(SessionImageId sessionId)
{
    for (int i = m_pendingSessionBinds.size() - 1; i >= 0; --i) {
        // Match by session id only — same path may still need other binds.
        if (m_pendingSessionBinds.at(i).id == sessionId) {
            m_pendingSessionBinds.removeAt(i);
        }
    }
    for (int i = m_workspace.savedItems().size() - 1; i >= 0; --i) {
        if (m_workspace.savedItems().at(i).sessionId == sessionId) {
            m_workspace.savedItems().removeAt(i);
        }
    }
}

void ImageView::prunePathOrdersAfterSessionRemove(const QStringList &removedPaths)
{
    if (removedPaths.isEmpty()) {
        return;
    }
    // Rebuild path/id order aligned with remaining tiles (IDENTITY: id first).
    QStringList prunedPaths;
    QVector<SessionImageId> prunedIds;
    prunedPaths.reserve(m_items.size());
    prunedIds.reserve(m_items.size());
    QSet<SessionImageId> seenIds;
    QHash<QString, int> unboundBudget;
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            if (seenIds.contains(sid)) {
                continue;
            }
            seenIds.insert(sid);
            prunedPaths.append(item->path());
            prunedIds.append(sid);
        } else {
            unboundBudget[item->path()] += 1;
        }
    }
    // Preserve prior order for unbound path slots still live.
    for (int i = 0; i < m_pathOrder.size(); ++i) {
        const QString &path = m_pathOrder.at(i);
        const SessionImageId sid = (i < m_sessionIdOrder.size())
            ? m_sessionIdOrder.at(i) : kInvalidSessionImageId;
        if (sid != kInvalidSessionImageId) {
            continue; // already taken from live bound tiles
        }
        if (unboundBudget.value(path) > 0) {
            prunedPaths.append(path);
            prunedIds.append(kInvalidSessionImageId);
            unboundBudget[path] -= 1;
        }
    }
    m_pathOrder = prunedPaths;
    m_sessionIdOrder = prunedIds;
}

void ImageView::restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                                  const QPointF &keptCenter, int scrollH, int scrollV)
{
    // Gallery: repack so deleted tiles do not leave empty holes. Preserve the
    // pre-delete viewport centre afterward (same idea as return-from-Image).
    if (gallery && m_scene) {
        if (!m_items.isEmpty()) {
            applyLayout(GalleryPackReason::SessionMutate);
        } else if (keptSceneRect.isValid()) {
            m_scene->setSceneRect(keptSceneRect);
        }
        if (!keptCenter.isNull()) {
            centerOn(keptCenter);
        }
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(scrollH);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(scrollV);
        }
        m_gallery.setViewportSnapshot(keptCenter, scrollH, scrollV);
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
}

void ImageView::removeWorkspaceSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    m_appearance.remove(sessionId);

    // Capture view before any item is destroyed — Qt may shrink sceneRect
    // while removeItem runs, which zeroes scrollbar ranges mid-loop.
    const bool gallery = isGalleryMode();
    QRectF keptSceneRect = (m_scene && gallery) ? m_scene->sceneRect() : QRectF();
    if (gallery && m_scene && !keptSceneRect.isValid()) {
        keptSceneRect = m_scene->itemsBoundingRect();
        if (keptSceneRect.isValid()) {
            keptSceneRect.adjust(-64, -64, 64, 64);
        }
    }
    const QPointF keptCenter = gallery
        ? mapToScene(viewport()->rect().center())
        : QPointF();
    const int scrollH = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
    const int scrollV = verticalScrollBar() ? verticalScrollBar()->value() : 0;

    // Collect first — destroyCanvasItem mutates m_items / stashes.
    const QList<ImageItem *> doomed = collectItemsForSessionId(sessionId);
    const QStringList removedPaths = destroySessionIdItems(doomed);
    prunePendingBindsAndSavedForSessionId(sessionId);
    prunePathOrdersAfterSessionRemove(removedPaths);
    restoreViewportAfterSessionRemove(gallery, keptSceneRect, keptCenter, scrollH, scrollV);

    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::setCurrentSessionId(SessionImageId id)
{
    if (m_currentSessionId == id) {
        return;
    }
    m_currentSessionId = id;
    // Attention marker is per SessionImageId — reload draft for the new image.
    if (m_attentionMode) {
        m_attentionDraftValid = false;
        m_attentionDraftSessionId = kInvalidSessionImageId;
        ensureAttentionPoint();
        if (viewport()) {
            viewport()->update();
        }
    }
}

bool ImageView::hasWorkspaceSessionIndex(int sessionIndex) const
{
    return findItemBySessionIndex(sessionIndex) != nullptr;
}

void ImageView::removeWorkspaceSessionIndex(int sessionIndex)
{
    ImageItem *item = findItemBySessionIndex(sessionIndex);
    if (!item) {
        return;
    }
    // Prefer id-based detach when the tile is bound (duplicate-safe).
    if (item->sessionId() != kInvalidSessionImageId) {
        detachCanvasSessionId(item->sessionId());
        return;
    }
    const QString path = item->path();
    // Only cancel pending work if no other live tile still uses this path.
    bool pathStillLive = false;
    for (ImageItem *other : m_items) {
        if (other && other != item && other->path() == path) {
            pathStillLive = true;
            break;
        }
    }
    if (!pathStillLive) {
        m_pendingWorkspacePaths.remove(path);
        m_pendingScenePos.remove(path);
        m_pendingSessionIndexByPath.remove(path);
        gallerySoftResetPath(path);
}
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::detachCanvasSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    // Canvas membership only — keep session appearance and session list entry.
    QList<ImageItem *> doomed;
    for (ImageItem *item : m_items) {
        if (item && item->sessionId() == sessionId) {
            doomed.append(item);
        }
    }
    for (ImageItem *item : doomed) {
        const QString path = item->path();
        bool pathStillLive = false;
        for (ImageItem *other : m_items) {
            if (other && other != item && other->path() == path) {
                pathStillLive = true;
                break;
            }
        }
        if (!pathStillLive) {
            takePendingWorkspacePath(path);
            m_pendingScenePos.remove(path);
            m_pendingSessionIndexByPath.remove(path);
        gallerySoftResetPath(path);
}
        // Drop pending binds for this id only (not every same-path bind).
        for (int i = m_pendingSessionBinds.size() - 1; i >= 0; --i) {
            if (m_pendingSessionBinds.at(i).id == sessionId) {
                m_pendingSessionBinds.removeAt(i);
            }
        }
        destroyCanvasItem(item);
    }
    if (!doomed.isEmpty()) {
        emit statusChanged();
        emit workspacePathsChanged();
    }
}

void ImageView::bindSelectedSessionIndices(int firstSessionIndex)
{
    if (firstSessionIndex < 0) {
        return;
    }
    int next = firstSessionIndex;
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            item->setSessionIndex(next);
            ++next;
        }
    }
}

void ImageView::bindSelectedSessionIds(const QList<SessionImageId> &ids)
{
    int i = 0;
    for (ImageItem *item : m_items) {
        if (!item->isSelected()) {
            continue;
        }
        if (i >= ids.size()) {
            break;
        }
        const SessionImageId id = ids.at(i++);
        if (id == kInvalidSessionImageId) {
            continue;
        }
        // Never give the same SessionImageId to two live tiles (drop path used
        // to stamp {sid} onto every selected item while LoadAdd also bound it).
        if (ImageItem *owner = findItemBySessionId(id)) {
            if (owner != item) {
                qCritical("bindSelectedSessionIds: SessionImageId %lld already on another tile — skip",
                          static_cast<long long>(id));
                continue;
            }
        }
        WorkspaceItemState slot;
        if (m_pendingItemAppearance.contains(item)) {
            slot = m_pendingItemAppearance.take(item);
            // Pending may carry colour grade from Duplicate before the live item
            // was fully synced — apply it so the tile and sessionAppearanceImage match.
            item->setColorAdjustments(slot.colorAdjust);
        } else {
            slot = captureState(item);
        }
        item->setSessionId(id);
        // Live placement from the canvas item (Duplicate offsets, scales, …).
        slot.pos = item->pos();
        slot.scale = item->itemScaleX();
        slot.scaleY = item->itemScaleY();
        slot.shear = item->itemShear();
        slot.rotation = item->itemRotation();
        slot.opacity = item->itemOpacity();
        slot.z = item->stackZ();
        slot.hFlip = item->itemHFlip();
        slot.vFlip = item->itemVFlip();
        slot.hasCrop = item->sessionHasCrop();
        slot.cropRect = item->sessionCropRect();
        slot.contentHFlip = item->contentHFlip();
        slot.contentVFlip = item->contentVFlip();
        slot.colorAdjust = item->colorAdjustments();
        slot.sessionId = id;
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        m_appearance.set(id, slot);
        // Drive ThumbnailBar per-id override (cropped/rotated/graded pixels).
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(id, item->path(), appearance);
            emit sessionCropApplied(id, item->path(), appearance, item->sessionHasCrop());
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
    if (const WorkspaceItemState *src = m_appearance.get(fromId)) {
        dst = *src;
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
        dst = captureState(donor);
    }
    dst.sessionId = toId;
    dst.pos = QPointF();
    dst.scale = 1.0;
    dst.scaleY = 1.0;
    dst.shear = 0.0;
    dst.rotation = 0.0;
    dst.opacity = 1.0;
    dst.z = 0.0;
    m_appearance.set(toId, dst);

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

int ImageView::workspacePathOccurrenceCount(const QString &path) const
{
    int n = 0;
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            ++n;
        }
    }
    return n;
}

void ImageView::removeWorkspacePathOccurrence(const QString &path, int occurrence)
{
    if (occurrence < 0) {
        return;
    }
    int found = 0;
    for (ImageItem *item : m_items) {
        if (item->path() != path) {
            continue;
        }
        if (found == occurrence) {
            takePendingWorkspacePath(path);
            m_pendingScenePos.remove(path);
            m_pendingSessionIndexByPath.remove(path);
        gallerySoftResetPath(path);
destroyCanvasItem(item);
            emit statusChanged();
            emit workspacePathsChanged();
            return;
        }
        ++found;
    }
}

void ImageView::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    m_scene->clearSelection();
    item->setSelected(true);
    if (isGalleryMode()) {
        ensureVisible(item, 48, 48);
        // Keyboard focus: show filename in the HUD like mouse hover.
        if (m_gallery.hoverPath() != path) {
            m_gallery.setHoverPath(path);
            viewport()->update();
        }
    }
}

void ImageView::revealGalleryPath(const QString &path)
{
    if (path.isEmpty() || !isGalleryMode()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    ensureVisible(item, 48, 48);
    if (m_gallery.hoverPath() != path) {
        m_gallery.setHoverPath(path);
        viewport()->update();
    }
}



void ImageView::destroyCanvasItem(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Re-entrancy / double-destroy: after the first call the pointer is gone from
    // live and stash lists. A second call must not touch a deleted QGraphicsItem
    // (seen as SIGSEGV in QObject::blockSignals on a garbage scene pointer).
    const bool inLive = m_items.contains(item);
    const bool inGalleryStash = m_gallery.stashedItems().contains(item);
    const bool inWorkspaceStash = m_workspace.stashedItems().contains(item);
    if (!inLive && !inGalleryStash && !inWorkspaceStash) {
        return;
    }
    // AUDIT H8/H9: clear every view-owned pointer before delete so paint /
    // input cannot touch a dangling ImageItem (BSP crashes in scene paint).
    if (item == m_dragItem) {
        m_dragItem = nullptr;
    }
    if (item == m_rotateItem) {
        m_rotateItem = nullptr;
        m_rotating = false;
    }
    if (item == m_handleDragItem) {
        m_handleDragItem = nullptr;
    }
    if (item == m_gallery.selectionAnchor()) {
        m_gallery.setSelectionAnchor(nullptr);
    }
    // Group scale holds raw pointers — drop before delete or BSP paint UAF.
    if (m_groupScaleDrag || m_groupRotateDrag || !m_groupDragItems.isEmpty()) {
        m_groupScaleDrag = false;
        m_groupRotateDrag = false;
        m_groupHandle = -1;
        m_groupHoverHandle = -1;
        m_groupDragItems.clear();
        m_groupDragStartStates.clear();
    }
    // Also drop from gallery stash so discardStashedGallery cannot double-free.
    m_gallery.stashedItems().removeAll(item);
    m_workspace.stashedItems().removeAll(item);

    rememberItemState(item);
    m_items.removeAll(item);
    if (QGraphicsScene *sc = item->scene()) {
        // selectionChanged → statusChanged → paint must not run mid-teardown
        // (re-entrant paint was UAF in the BSP / item lists).
        const bool blocked = sc->blockSignals(true);
        item->setSelected(false);
        sc->removeItem(item);
        sc->blockSignals(blocked);
    } else {
        item->setSelected(false);
    }
    delete item;
    // TransformCommand stores raw ImageItem*; drop undo history that would
    // redo/undo against a deleted object — unless a session-level command is
    // intentionally removing canvas tiles and must stay on the stack.
    if (m_undoStack && !m_preserveUndoOnDestroy) {
        m_undoStack->clear();
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}




qreal ImageView::pageGuidePxPerMm()
{
    // Workspace items use native image pixels as scene units. A 12MP photo is
    // ~4000px wide; at screen 96dpi an A4 sheet is only ~794px and looks tiny.
    // Use 300dpi so a page is roughly photo-scale (~2480×3508 for A4) while
    // still mapping 1:1 to physical paper on print/PDF.
    constexpr qreal kPageGuideDpi = 300.0;
    return kPageGuideDpi / 25.4;
}

void ImageView::setPageGuideVisible(bool on)
{
    if (m_pageGuideVisible == on) {
        return;
    }
    m_pageGuideVisible = on;
    if (!m_pageGuideVisible) {
        m_pageGuideSelected = false;
        m_pageGuideHoverHandle = -1;
        m_pageGuideDragHandle = -1;
    }
    if (m_pageGuideVisible && !m_pageGuideSize.isValid()) {
        const qreal pxPerMm = pageGuidePxPerMm();
        m_pageGuideSize = QSizeF(210.0 * pxPerMm, 297.0 * pxPerMm);
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}

void ImageView::setPageGuideFromPrinter(const QPrinter &printer)
{
    // fullRect = physical paper (size + orientation). paintRect is only the
    // printable inset and can look unchanged when the user picks a different
    // stock size with similar aspect (or when margins dominate).
    const QPageLayout layout = printer.pageLayout();
    QRectF mm = layout.fullRect(QPageLayout::Millimeter);
    if (!mm.isValid() || mm.width() <= 1.0 || mm.height() <= 1.0) {
        const QSizeF sz = layout.pageSize().size(QPageSize::Millimeter);
        if (sz.width() > 1.0 && sz.height() > 1.0) {
            mm = QRectF(QPointF(0, 0), sz);
            if (layout.orientation() == QPageLayout::Landscape && mm.width() < mm.height()) {
                mm = QRectF(0, 0, mm.height(), mm.width());
            }
        } else {
            mm = QRectF(0, 0, 210.0, 297.0);
        }
    }
    const qreal pxPerMm = pageGuidePxPerMm();
    m_pageGuideSize = QSizeF(mm.width() * pxPerMm, mm.height() * pxPerMm);
    m_pageGuideRect = QRectF(); // printer pages stay centred on the origin
    if (m_pageGuideVisible) {
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        viewport()->update();
        emit statusChanged();
    }
}


void ImageView::renderForPrint(QPainter *painter, const QRectF &pageRect) const
{
    if (!painter || !painter->isActive() || !pageRect.isValid()) {
        return;
    }

    if (isWorkspaceMode() && m_pageGuideVisible && m_scene) {
        // Exact mapping: guide scene rect → page rect. KeepAspectRatio letterboxed
        // when full-sheet guide and margin-only pageRect differed, shifting items.
        const QRectF source = pageGuideSceneRect();
        m_scene->render(painter, pageRect, source, Qt::IgnoreAspectRatio);
        return;
    }

    if (isImageMode()) {
        ImageItem *item = primaryItem();
        if (!item) {
            item = targetItem();
        }
        if (item && item->hasDecodedPixels()) {
            const QImage &img = item->sourceImage();
            if (!img.isNull()) {
                QSizeF fitted(img.size());
                fitted.scale(pageRect.size(), Qt::KeepAspectRatio);
                const QRectF target(
                    pageRect.center().x() - fitted.width() / 2.0,
                    pageRect.center().y() - fitted.height() / 2.0,
                    fitted.width(), fitted.height());
                painter->save();
                painter->translate(target.center());
                painter->rotate(item->itemRotation());
                painter->scale(item->itemHFlip() ? -1.0 : 1.0,
                               item->itemVFlip() ? -1.0 : 1.0);
                painter->translate(-target.center());
                painter->drawImage(target, img);
                painter->restore();
                return;
            }
        }
    }

    if (m_scene) {
        QRectF source = m_scene->itemsBoundingRect();
        if (!source.isValid() || source.isEmpty()) {
            return;
        }
        source.adjust(-4, -4, 4, 4);
        m_scene->render(painter, pageRect, source, Qt::KeepAspectRatio);
    }
}

QRectF ImageView::pageGuideSceneRect() const
{
    if (m_pageGuideRect.isValid() && m_pageGuideRect.width() > 0 && m_pageGuideRect.height() > 0) {
        return m_pageGuideRect;
    }
    QSizeF sz = m_pageGuideSize;
    if (!sz.isValid() || sz.width() <= 0 || sz.height() <= 0) {
        const qreal pxPerMm = pageGuidePxPerMm();
        sz = QSizeF(210.0 * pxPerMm, 297.0 * pxPerMm);
    }
    return QRectF(-sz.width() / 2.0, -sz.height() / 2.0, sz.width(), sz.height());
}

void ImageView::fitPageGuideToContent(qreal marginPx)
{
    QRectF bounds = contentExportBounds();
    if (!bounds.isValid() || bounds.isEmpty()) {
        return;
    }
    if (marginPx > 0.0) {
        // contentExportBounds already pads 4px; add the requested extra margin.
        bounds.adjust(-marginPx, -marginPx, marginPx, marginPx);
    }
    m_pageGuideRect = bounds;
    m_pageGuideSize = bounds.size();
    m_pageGuideVisible = true;
    m_pageGuideSelected = true;
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}

void ImageView::setPageGuideSelected(bool on)
{
    if (!m_pageGuideVisible) {
        on = false;
    }
    if (m_pageGuideSelected == on) {
        return;
    }
    m_pageGuideSelected = on;
    if (!on) {
        m_pageGuideHoverHandle = -1;
        m_pageGuideDragHandle = -1;
    }
    viewport()->update();
}

int ImageView::pageGuideHandleAt(const QPoint &viewPos) const
{
    if (!m_pageGuideVisible || !m_pageGuideSelected || !isWorkspaceMode()) {
        return -1;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.isEmpty()) {
        return -1;
    }
    const QRect viewRect = mapFromScene(page).boundingRect();
    constexpr qreal kScaleHit = 12.0;
    const QPointF corners[8] = {
        viewRect.topLeft(),
        QPointF(viewRect.center().x(), viewRect.top()),
        viewRect.topRight(),
        QPointF(viewRect.right(), viewRect.center().y()),
        viewRect.bottomRight(),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        viewRect.bottomLeft(),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    for (int i = 0; i < 8; ++i) {
        if (QLineF(QPointF(viewPos), corners[i]).length() <= kScaleHit) {
            return i;
        }
    }
    return -1;
}

bool ImageView::beginPageGuideResize(int handle)
{
    if (handle < 0 || handle > 7 || !m_pageGuideVisible) {
        return false;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.width() < 1.0 || page.height() < 1.0) {
        return false;
    }
    m_pageGuideSelected = true;
    m_pageGuideDragHandle = handle;
    m_pageGuideDragStartRect = page;
    return true;
}

QRectF ImageView::pageGuideRectFromHandleDrag(const QPointF &scenePos,
                                              Qt::KeyboardModifiers mods) const
{
    // Match Workspace image scale handles:
    //   default = opposite edge/corner fixed
    //   Ctrl    = scale about centre
    //   Shift   = lock starting aspect (corners; with or without Ctrl)
    const QRectF r = m_pageGuideDragStartRect;
    const int h = m_pageGuideDragHandle;
    // 0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L
    const bool fromCenter = mods & Qt::ControlModifier;
    const bool lockAspect = mods & Qt::ShiftModifier;
    const bool corner = (h == 0 || h == 2 || h == 4 || h == 6);
    const QPointF c = r.center();

    qreal left = r.left();
    qreal top = r.top();
    qreal right = r.right();
    qreal bottom = r.bottom();

    if (fromCenter) {
        // Distance from centre to pointer defines half-size on the active axes.
        const qreal halfW = qAbs(scenePos.x() - c.x());
        const qreal halfH = qAbs(scenePos.y() - c.y());
        switch (h) {
        case 0: case 2: case 4: case 6: // corners
            left = c.x() - halfW;
            right = c.x() + halfW;
            top = c.y() - halfH;
            bottom = c.y() + halfH;
            break;
        case 1: case 5: // top / bottom — vertical only
            top = c.y() - halfH;
            bottom = c.y() + halfH;
            break;
        case 3: case 7: // right / left — horizontal only
            left = c.x() - halfW;
            right = c.x() + halfW;
            break;
        default:
            break;
        }
    } else {
        switch (h) {
        case 0: // TL
            left = scenePos.x();
            top = scenePos.y();
            break;
        case 1: // T
            top = scenePos.y();
            break;
        case 2: // TR
            right = scenePos.x();
            top = scenePos.y();
            break;
        case 3: // R
            right = scenePos.x();
            break;
        case 4: // BR
            right = scenePos.x();
            bottom = scenePos.y();
            break;
        case 5: // B
            bottom = scenePos.y();
            break;
        case 6: // BL
            left = scenePos.x();
            bottom = scenePos.y();
            break;
        case 7: // L
            left = scenePos.x();
            break;
        default:
            break;
        }
    }

    if (lockAspect && corner) {
        const qreal aspect = r.width() / qMax(1e-6, r.height());
        qreal w = right - left;
        qreal hh = bottom - top;
        if (qAbs(w) / qMax(1e-6, qAbs(hh)) > aspect) {
            const qreal newH = qAbs(w) / aspect;
            if (fromCenter) {
                top = c.y() - newH * 0.5;
                bottom = c.y() + newH * 0.5;
            } else if (h == 0 || h == 2) {
                top = bottom - std::copysign(newH, bottom - top);
            } else {
                bottom = top + std::copysign(newH, bottom - top);
            }
        } else {
            const qreal newW = qAbs(hh) * aspect;
            if (fromCenter) {
                left = c.x() - newW * 0.5;
                right = c.x() + newW * 0.5;
            } else if (h == 0 || h == 6) {
                left = right - std::copysign(newW, right - left);
            } else {
                right = left + std::copysign(newW, right - left);
            }
        }
    }

    QRectF next(QPointF(left, top), QPointF(right, bottom));
    next = next.normalized();
    constexpr qreal kMin = 32.0;
    if (next.width() < kMin) {
        if (fromCenter) {
            next = QRectF(c.x() - kMin * 0.5, next.top(), kMin, next.height());
        } else if (h == 0 || h == 6 || h == 7) {
            next.setLeft(next.right() - kMin);
        } else {
            next.setWidth(kMin);
        }
    }
    if (next.height() < kMin) {
        if (fromCenter) {
            next = QRectF(next.left(), c.y() - kMin * 0.5, next.width(), kMin);
        } else if (h == 0 || h == 1 || h == 2) {
            next.setTop(next.bottom() - kMin);
        } else {
            next.setHeight(kMin);
        }
    }
    return next;
}

void ImageView::updatePageGuideResize(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (m_pageGuideDragHandle < 0) {
        return;
    }
    const QRectF next = pageGuideRectFromHandleDrag(scenePos, mods);
    m_pageGuideRect = next;
    m_pageGuideSize = next.size();
    updateWorkspaceSceneRect();
    viewport()->update();
    emit statusChanged();
}

void ImageView::endPageGuideResize()
{
    m_pageGuideDragHandle = -1;
    viewport()->update();
}


























QSet<int> ImageView::workspaceSessionIndices() const
{
    QSet<int> out;
    for (ImageItem *item : m_items) {
        if (item->sessionIndex() >= 0) {
            out.insert(item->sessionIndex());
        }
    }
    return out;
}


QList<int> ImageView::selectedSessionIndices() const
{
    QList<int> out;
    for (ImageItem *item : m_items) {
        if (item->isSelected() && item->sessionIndex() >= 0) {
            out.append(item->sessionIndex());
        }
    }
    return out;
}

void ImageView::selectAllCanvasItems()
{
    if (!m_scene || isImageMode() || m_items.isEmpty()) {
        return;
    }
    m_scene->blockSignals(true);
    for (ImageItem *item : m_items) {
        if (item) {
            if (isGalleryMode()
                && !(item->flags() & QGraphicsItem::ItemIsSelectable)) {
                item->setGallerySelectable(true);
            }
            item->setSelected(true);
        }
    }
    m_scene->blockSignals(false);
    if (isGalleryMode()) {
        for (ImageItem *item : m_items) {
            if (item) {
                item->invalidateDeviceCache();
            }
        }
        if (viewport()) {
            viewport()->update();
        }
    }
    if (!m_items.isEmpty()) {
        m_gallery.setSelectionAnchor(m_items.first());
    }
    emit canvasSelectionChanged();
    emit statusChanged();
}

void ImageView::clearCanvasSelection()
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    emit canvasSelectionChanged();
    emit statusChanged();
}






QList<ImageItem *> ImageView::transformTargets() const
{
    QList<ImageItem *> out;
    if (!m_scene) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            out.append(item);
        }
    }
    if (!out.isEmpty()) {
        return out;
    }
    if (isImageMode() || m_items.size() == 1) {
        if (!m_items.isEmpty()) {
            out.append(m_items.first());
        }
    }
    return out;
}



QSizeF ImageView::nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    // Logical size — never soft display pixmap dimensions.
    return QSizeF(item->imageSize());
}













QRectF ImageView::contentExportBounds() const
{
    QRectF bounds;
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QRectF r = item->contentSceneRect();
        if (!r.isValid() || r.isEmpty()) {
            continue;
        }
        bounds = bounds.isValid() ? bounds.united(r) : r;
    }
    if (!bounds.isValid() || bounds.isEmpty()) {
        if (m_scene) {
            bounds = m_scene->itemsBoundingRect();
        }
    }
    if (bounds.isValid() && !bounds.isEmpty()) {
        bounds.adjust(-4, -4, 4, 4);
    }
    return bounds;
}

QImage ImageView::renderExportImage(const QSize &pixelSize, const QRectF &sourceSceneRect,
                                    bool transparentBackground) const
{
    if (!m_scene || !pixelSize.isValid() || pixelSize.width() < 1 || pixelSize.height() < 1
        || !sourceSceneRect.isValid() || sourceSceneRect.isEmpty()) {
        return {};
    }
    QImage img(pixelSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Target rect with aspect preserved (same as QGraphicsScene::render KeepAspectRatio).
    const QRectF target(QPointF(0, 0), QSizeF(pixelSize));
    QRectF fitted = target;
    {
        const qreal sx = target.width() / sourceSceneRect.width();
        const qreal sy = target.height() / sourceSceneRect.height();
        const qreal s = qMin(sx, sy);
        const qreal tw = sourceSceneRect.width() * s;
        const qreal th = sourceSceneRect.height() * s;
        fitted = QRectF(target.center().x() - tw / 2.0,
                        target.center().y() - th / 2.0, tw, th);
    }

    if (!transparentBackground) {
        // Paint Workspace / app canvas background in scene space, then map to pixels.
        painter.save();
        QTransform xform;
        xform.translate(fitted.left(), fitted.top());
        xform.scale(fitted.width() / sourceSceneRect.width(),
                    fitted.height() / sourceSceneRect.height());
        xform.translate(-sourceSceneRect.left(), -sourceSceneRect.top());
        painter.setTransform(xform);
        const qreal exportScale = fitted.width() / sourceSceneRect.width();
        // paintCanvasBackground is non-const (tile cache); export is const — cast.
        const_cast<ImageView *>(this)->paintCanvasBackground(
            &painter, sourceSceneRect, exportScale);
        painter.restore();
    }

    // Scene items only (no scene background brush).
    const QBrush oldBrush = m_scene->backgroundBrush();
    m_scene->setBackgroundBrush(Qt::NoBrush);
    m_scene->render(&painter, fitted, sourceSceneRect, Qt::IgnoreAspectRatio);
    m_scene->setBackgroundBrush(oldBrush);
    return img;
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
            : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
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
        : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(sid, item->path(), appearance);
        }
    }
}

void ImageView::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_colorAdjustCommitSid = sid;
    m_colorAdjustCommitPath = path;
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->start();
    } else {
        flushColorAdjustCommit();
    }
}

void ImageView::flushColorAdjustCommit()
{
    const SessionImageId sid = m_colorAdjustCommitSid;
    const QString path = m_colorAdjustCommitPath;
    m_colorAdjustCommitSid = kInvalidSessionImageId;
    m_colorAdjustCommitPath.clear();
    if (sid == kInvalidSessionImageId && path.isEmpty()) {
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
    if (sid != kInvalidSessionImageId && m_appearance.contains(sid)) {
        want = m_appearance.value(sid);
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
        sid = m_currentSessionId;
    }
    WorkspaceItemState slot = (sid != kInvalidSessionImageId && m_appearance.contains(sid))
        ? m_appearance.value(sid)
        : captureState(item);
    if (sid != kInvalidSessionImageId) {
        slot.sessionId = sid;
        slot.path = item->path();
        slot.colorAdjust = adj;
        m_appearance.set(sid, slot);
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

ColorAdjustments ImageView::targetColorAdjustments() const
{
    ImageItem *item = targetItem();
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    if (!item) {
        return {};
    }
    return item->colorAdjustments();
}
