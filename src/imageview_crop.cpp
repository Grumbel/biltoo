// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop enter/apply/leave: docs/CROP_MODE.md (SessionImageId store, full-frame
// draft, clear FullSource before soft crop attach, filmstrip bake emit).

#include "imageview.h"
#include "cropgeometry.h"
#include "placementlinear.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "sessionappearance.h"
#include "contentxform.h"

#include <QGuiApplication>
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"
#include "contentxform.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QTransform>
#include <QUndoCommand>
#include <QUndoStack>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>

namespace {

CropGeometry::CropButtonLayout cropChromeButtons(bool cropActive, const QRectF &cropView,
                                                 const QRect &viewportRect)
{
    CropGeometry::CropButtonLayout empty;
    if (!cropActive) {
        return empty;
    }
    return CropGeometry::cropButtonLayout(cropView, viewportRect);
}

} // namespace

/** Prefer ImageCache; fall back to item display only when not a prior crop bake. */
QImage pickCropApplyHost(ImageItem *item, const QString &path, bool *fromCache)
{
    QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (fromCache) {
        *fromCache = !host.isNull();
    }
    if (!host.isNull()) {
        return host;
    }
    if (item && item->hasAppliedContentXform()
        && item->appliedContentXform().hasCrop) {
        return {};
    }
    if (!item) {
        return {};
    }
    host = item->sourceImage();
    if (host.isNull()) {
        host = item->previewImage();
    }
    return host;
}


ImageItem *ImageView::cropSessionBoundItem() const
{
    // Bound subject for the active crop session (IDENTITY.md).
    if (m_crop.target()) {
        return m_crop.target();
    }
    if (m_crop.hasTargetId()) {
        return findItemBySessionId(m_crop.targetIdValue());
    }
    return nullptr;
}

ImageItem *ImageView::cropTargetItem() const
{
    // Crop session is bound to one subject for its entire lifetime. Never
    // re-resolve via selection or primaryItem() — that applied the draft to
    // unrelated tiles when selection changed mid-crop (IDENTITY.md).
    if (m_crop.active()) {
        return cropSessionBoundItem();
    }
    if (ImageItem *t = targetItem()) {
        // Soft ladder tiles have preview/source with m_previewPixels; pixmap is
        // often empty (paint uses m_source). hasDecodedPixels() alone was
        // "Crop / No image" for every soft-only Workspace/Gallery selection.
        if (t->hasDisplayPixels()) {
            return t;
        }
    }
    // Image mode only: sole canvas item. Gallery/Workspace need an explicit
    // single selection (hasSingleCropTarget) — never primaryItem() fan-out.
    if (isImageMode()) {
        return primaryItem();
    }
    return nullptr;
}


void ImageView::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QRectF cr = item->contentRect();
    if (!m_crop.hasValidRect()) {
        m_crop.setRect(cr);
        m_crop.setRotation(0.0);
        return;
    }
    m_crop.setRect(m_crop.normalizedRect());
    if (m_crop.isAllowExpand()) {
        QRectF r = m_crop.currentRect();
        if (r.width() < 1.0) {
            r.setWidth(1.0);
        }
        if (r.height() < 1.0) {
            r.setHeight(1.0);
        }
        m_crop.setRect(r);
        return;
    }
    m_crop.setRect(CropGeometry::constrainToContent(m_crop.currentRect(), m_crop.currentRotation(), cr, 1.0));
}

void ImageView::alignCropFrameCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item || !m_crop.hasValidRect()) {
        return;
    }
    const QPointF delta = PlacementLinear::scenePosDeltaToAlign(
        item->mapToScene(m_crop.draftCenterLocal()), sceneAnchor);
    if (!qIsFinite(delta.x()) || !qIsFinite(delta.y())) {
        return;
    }
    item->setPos(item->pos() + delta);
}

void ImageView::alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item) {
        return;
    }
    // Pixmap is centred on the item origin (offset -w/2,-h/2).
    const QPointF current = item->mapToScene(QPointF(0.0, 0.0));
    const QPointF delta = PlacementLinear::scenePosDeltaToAlign(current, sceneAnchor);
    if (!qIsFinite(delta.x()) || !qIsFinite(delta.y())) {
        return;
    }
    item->setPos(item->pos() + delta);
}

bool ImageView::enterCropModeFromUi()
{
    // Gallery packing cannot host crop UI — MainWindow opens Image mode instead.
    if (isGalleryMode()) {
        return false;
    }
    // Image or Workspace: one explicit subject only.
    if (!hasSingleCropTarget()) {
        flashHud(tr("Crop"), tr("Select a single image"));
        return false;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !item->hasDisplayPixels()) {
        flashHud(tr("Crop"), tr("No image"));
        return false;
    }
    cancelZoomRegion();
    // Lock identity + enter snapshot + unrotate placement (IDENTITY.md).
    // m_crop.active() stays false until after the first draft attach.
    QImage enterSrc = item->sourceImage().copy();
    if (enterSrc.isNull()) {
        enterSrc = item->previewImage().copy();
    }
    WorkspaceItemState enterSt = captureState(item);
    enterSt.hasCrop = item->sessionHasCrop();
    enterSt.cropRect = item->sessionCropRect();
    m_crop.beginEnterSession(item, enterSrc, enterSt,
                             !enterSrc.isNull() || item->hasDisplayPixels());
    item->setTileLodSuppressed(true);
    if (m_pathRaster && !m_crop.draftPathRef().isEmpty()) {
        m_pathRaster->cancel(m_crop.draftPathRef());
    }
    // Workspace: remember displayed image centre so the crop frame can stay fixed.
    const QPointF workspaceAnchorScene = item->mapToScene(QPointF(0.0, 0.0));
    // One paint after full-frame draft is ready (no intermediate crop-on-old-box).
    if (viewport()) {
        viewport()->setUpdatesEnabled(false);
    }
    if (!prepareCropModeFullImage(item)) {
        if (viewport()) {
            viewport()->setUpdatesEnabled(true);
        }
        // prepare may have set mode for fitItem then failed — restore placement
        // before abortEnter clears the stash.
        item->setTileLodSuppressed(false);
        m_crop.restoreStashedPlacement(item);
        m_crop.abortEnter();
        flashHud(tr("Crop"), tr("Could not load full image"));
        return false;
    }
    if (isWorkspaceMode()) {
        // If there was no stored crop angle but the tile was free-rotated,
        // seed the draft rotation so the frame matches the prior pose while
        // the item stays axis-aligned for editing.
        if (m_crop.seedRotationFromStashedPlacement(CropGeometry::kFreeRotationEps)) {
            ensureCropRectValid();
        }
        alignCropFrameCenterToScene(item, workspaceAnchorScene);
        updateWorkspaceSceneRect();
    }
    // Mode already active (activateModeAfterDraft in prepare).
    flashHud(tr("Crop mode"),
             tr("Apply commits · Esc cancels"));
    emit cropModeChanged(true);
    emit statusChanged();
    if (viewport()) {
        viewport()->setUpdatesEnabled(true);
        viewport()->update();
    }
    return true;
}

void ImageView::setCropMode(bool on)
{
    if (on == m_crop.active()) {
        return;
    }
    if (on) {
        enterCropModeFromUi();
        return;
    }
    // Turning crop off from the toolbar commits the draft (auto-apply).
    leaveCropModeInternal(true);
}



bool ImageView::resolveCropEnterAppearance(ImageItem *item, WorkspaceItemState *app) const
{
    // Prior crop + content flags for *this* session image only — never path map alone.
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : m_sessionId.currentIdValue();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            *app = *it;
            return true;
        }
    }
    if (item->sessionHasCrop()) {
        app->hasCrop = true;
        app->cropRect = item->sessionCropRect();
        app->contentHFlip = item->contentHFlip();
        app->contentVFlip = item->contentVFlip();
        return true;
    }
    // Last resort for unbound single-instance tiles.
    if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
        *app = *st;
        return true;
    }
    return false;
}

bool ImageView::isCropDraftLockedItem(const ImageItem *item) const
{
    if (!item) {
        return false;
    }
    // Pointer/id lock on CropSession, then path lock (draftPath / targetId resolve).
    return m_crop.locksItem(item) || isCropDraftLockedPath(item->path());
}

bool ImageView::isCropDraftLockedPath(const QString &path) const
{
    if (m_crop.locksPath(path) || m_crop.isDraftSampleFrozenForPath(path)) {
        return true;
    }
    // Host resolves targetId → current item path when draftPath is empty.
    if (!m_crop.isDraftSampleFrozen() || path.isEmpty()) {
        return false;
    }
    if (m_crop.hasTargetId()) {
        if (ImageItem *byId = findItemBySessionId(m_crop.targetIdValue())) {
            if (byId->path() == path) {
                return true;
            }
        }
    }
    return false;
}

void ImageView::installFullImageForCrop(ImageItem *item, const QImage &full,
                                        const WorkspaceItemState *app, bool haveApp,
                                        bool unorientedSource)
{
    if (!item || full.isNull()) {
        return;
    }
    // Crop drafts live in contentRect / intrinsic space (SIZE.md). Soft or
    // PreferCache samples must never redefine that box — only native/probed
    // logical size.
    const QString path = item->path();
    // Freeze quality climb for this path — draft sample must not thrash.
    if (m_pathRaster && !path.isEmpty()) {
        m_pathRaster->cancel(path);
    }
    if (sampleCoversNativeLogical(path, full)) {
        rememberSizeFromDecode(path, full);
        QSize logical = logicalSizeForPath(path);
        if (!isPositiveSize(logical) || isProvisionalImageSize(path)) {
            rememberImageSize(path, full.size());
        }
    }
    // Host cache is undecoded-appearance *source* only (never crop-baked display).
    if (!path.isEmpty() && unorientedSource && sampleCoversNativeLogical(path, full)) {
        ImageCache::put(path, full);
    }

    // Content flips/turns only — crop is drafted on the full post-orient frame.
    WorkspaceItemState contentOnly;
    if (haveApp && app) {
        contentOnly = SessionAppearance::withoutCrop(*app);
    }
    const ContentXform::Value wantX = ContentXform::Value::fromState(contentOnly);
    const bool hadPriorCrop = haveApp && app && app->hasCrop && !app->cropRect.isEmpty();
    const bool needGeomBake = contentOnly.contentHFlip || contentOnly.contentVFlip
        || contentOnly.contentQuarterTurns != 0;
    const bool needColor = !contentOnly.colorAdjust.isIdentity();

    // Full-frame already on the item (no crop bake): keep those pixels.
    // Do not rebuild a lower-res graded stand-in — that invites soft↔full thrash.
    // Accept either matching applied xform, or live grade (no applied yet) when
    // the painted sample is already large enough.
    const bool appliedOk = item->hasAppliedContentXform()
        && !item->appliedContentXform().hasCrop
        && ContentXform::equal(item->appliedContentXform(), wantX);
    const bool liveGradeOk = !item->hasAppliedContentXform()
        && !needGeomBake
        && item->colorAdjustments().brightness == contentOnly.colorAdjust.brightness
        && item->colorAdjustments().contrast == contentOnly.colorAdjust.contrast
        && item->colorAdjustments().saturation == contentOnly.colorAdjust.saturation
        && item->colorAdjustments().hue == contentOnly.colorAdjust.hue
        && item->colorAdjustments().invert == contentOnly.colorAdjust.invert
        && qFuzzyCompare(item->colorAdjustments().gamma, contentOnly.colorAdjust.gamma);
    if (!hadPriorCrop && item->hasDisplayPixels()
        && !item->sessionHasCrop()
        && (appliedOk || liveGradeOk)
        && item->displayPixelLongEdge() >= ContentXform::kGuiMaterializeMaxEdge) {
        item->setItemRotation(0.0);
        item->setItemShear(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
        item->setSessionCrop(false, QRect());
        item->setColorAdjustmentsRecord(contentOnly.colorAdjust);
        item->setAppliedContentXform(wantX);
        applyContentLayoutSize(item, contentOnly);
        m_crop.setShowingFullImage(true);
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
            qWarning().noquote()
                << QStringLiteral("[crop] enter-full KEEP display edge=%1 path=%2")
                       .arg(item->displayPixelLongEdge())
                       .arg(path);
        }
        return;
    }

    item->setItemRotation(0.0);
    item->setItemShear(0.0);
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    // Drop prior crop bake so SoftPreview full-frame stand-in is accepted.
    // Otherwise hasDecodedPixels() rejects soft install and the draft stays
    // on the already-cropped pixmap (second crop cannot see the original).
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
        qWarning().noquote()
            << QStringLiteral(
                   "[crop] enter-full path=%1 imageSize=%2x%3 hasDecoded=%4 "
                   "appliedCrop=%5 hostEdge=%6")
                   .arg(path)
                   .arg(item->imageSize().width()).arg(item->imageSize().height())
                   .arg(item->hasDecodedPixels() ? 1 : 0)
                   .arg(hadPriorCrop ? 1 : 0)
                   .arg(ImageCache::longEdge(full));
    }
    item->clearDecodedPixels();
    item->clearAppliedContentXform();

    // Interactive crop draft: orient-only full frame (never prior crop bake).
    // Identity (no orient/colour bake): attach the host as FullSource at native
    // resolution. Clamping multi-MP identity to SoftPreview≤2048 was wrong —
    // it manufactured a low-res draft that then fought host upgrades (soft↔full).
    // Geom bake on GUI only ≤ kGuiMaterializeMaxEdge (ASSERT_NOT_GUI_THREAD).
    // Colour-only: grade on host (or ≤2048 stand-in if host is huge) once.
    QImage sample = full;
    SessionAppearance::PixelKind kind = SessionAppearance::PixelKind::FullSource;
    constexpr int kColorOnlyDraftMaxEdge = 2048;
    if (needGeomBake
        && ImageCache::longEdge(sample) > ContentXform::kGuiMaterializeMaxEdge) {
        sample = ImageCache::clampToMaxEdge(
            sample, ContentXform::kGuiMaterializeMaxEdge);
        kind = SessionAppearance::PixelKind::SoftPreview;
    } else if (!needGeomBake && needColor
               && ImageCache::longEdge(sample) > kColorOnlyDraftMaxEdge) {
        sample = ImageCache::clampToMaxEdge(sample, kColorOnlyDraftMaxEdge);
        kind = SessionAppearance::PixelKind::SoftPreview;
    }
    // else identity: keep host size + FullSource (no artificial Soft demotion)

    QImage display;
    if (unorientedSource && needGeomBake) {
        display = SessionAppearance::materializeDisplay(sample, contentOnly, kind);
        if (display.isNull()) {
            display = sample;
        }
    } else if (unorientedSource && needColor) {
        display = applyColorAdjustments(sample, contentOnly.colorAdjust);
        if (display.isNull()) {
            display = sample;
        }
    } else {
        display = sample;
    }
    if (display.isNull()) {
        display = sample;
    }
    attachDisplaySample(item, display, contentOnly, kind);
    // Geometry: file-native orient size only (never soft pixels, never crop box).
    applyContentLayoutSize(item, contentOnly);
    item->setSessionCrop(false, QRect());
    item->setAppliedContentXform(wantX);

    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
        qWarning().noquote()
            << QStringLiteral(
                   "[crop] enter-full done imageSize=%1x%2 display=%3x%4 "
                   "appliedCrop=%5 contentTurns=%6")
                   .arg(item->imageSize().width()).arg(item->imageSize().height())
                   .arg(display.width()).arg(display.height())
                   .arg(item->sessionHasCrop() ? 1 : 0)
                   .arg(contentOnly.contentQuarterTurns);
    }
    m_crop.setShowingFullImage(true);
}

void ImageView::initCropRectFromPriorAppearance(ImageItem *item, const WorkspaceItemState &app,
                                                 bool haveApp)
{
    if (!item) {
        return;
    }
    m_crop.initRectFromPriorAppearance(item->contentRect(), item->offset(),
                                       item->imageSize(),
                                       haveApp ? &app : nullptr, haveApp);
}

bool ImageView::prepareCropModeFullImage(ImageItem *item)
{
    if (!item) {
        return false;
    }
    const QString path = item->path();
    m_crop.clearAwaitingFull();

    WorkspaceItemState app;
    const bool haveApp = resolveCropEnterAppearance(item, &app);
    const bool hadCrop = haveApp && app.hasCrop && !app.cropRect.isEmpty();

    // Unoriented ImageCache host preferred. Never use item display as the crop
    // base when a prior crop exists — that bake is already cropped, so a second
    // crop would edit the wrong frame and shrink further.
    QImage full = path.isEmpty() ? QImage() : ImageCache::get(path);
    bool hostOk = !full.isNull();
    bool unorientedSource = hostOk;

    if (!hostOk && !hadCrop) {
        full = item->sourceImage();
        if (full.isNull()) {
            full = item->previewImage();
        }
        unorientedSource = false;
    }
    if (full.isNull()) {
        // Prior crop and no host: force a load; do not enter on the bake.
        if (hadCrop && !path.isEmpty()) {
            requestCropFullRaster(path);
            m_crop.setAwaitingFull(path);
            flashHud(tr("Crop"), tr("Loading full image…"));
        }
        flashHud(tr("Crop"), tr("Image not cached yet — try again"));
        return false;
    }

    // Workspace: lock scene footprint before intrinsic changes on install.
    const QRectF beforeScene = item->mapRectToScene(item->contentRect());
    const qreal footW0 = beforeScene.width();
    const qreal footH0 = beforeScene.height();
    const QPointF center0 = beforeScene.center();
    installFullImageForCrop(item, full, haveApp ? &app : nullptr, haveApp, unorientedSource);
    // Workspace: do NOT rescale to fit full frame into the previous crop
    // footprint. That drove item scale toward ~1% on second crop (Zoom UI)
    // and made Apply inherit a near-zero scale. Placement scale is placement —
    // crop only changes intrinsic. Keep centre so the draft does not jump.
    if (isWorkspaceMode() && footW0 > 1.0 && footH0 > 1.0) {
        alignItemCenterToScene(item, center0);
    }
    initCropRectFromPriorAppearance(item, app, haveApp);

    // Crop chrome + fitItem only after pixels and contentRect match the draft.
    m_crop.activateModeAfterDraft();
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }

    m_crop.clearAwaitingFull();
    return true;
}

void ImageView::requestCropFullRaster(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Always try scheduleFullPixels (API macros only defined in TUs that
    // include thumtoo/client.hpp — not this file).
    if (ThumtooCache::isAvailable()) {
        int edge = 8192;
        const QSize native = ThumtooCache::cachedSize(path);
        if (native.isValid() && native.width() > 0 && native.height() > 0) {
            edge = ContentXform::clampLongEdge(ContentXform::longEdge(native),
                                           ImageCache::kDisplayMaxEdge);
        }
        if (ThumtooCache::scheduleFullPixels(path, edge)) {
            return;
        }
        if (ThumtooCache::isPixelsPending(path, edge)) {
            return;
        }
    }
    // scheduleFull skipped/unavailable: pool ImageLoader::load.
    const quint64 gen = m_loadGate.generation();
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, gen]() {
        const QImage decoded = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, path, decoded, gen]() {
                if (ImageView *const host = guard.data()) {
                    if (gen != host->m_loadGate.generation()) {
                        return;
                    }
                    if (!decoded.isNull()) {
                        ImageCache::put(path, decoded);
                    }
                    host->maybeUpgradeCropFullRaster(path, decoded);
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageView::maybeUpgradeCropFullRaster(const QString &path, const QImage &image)
{
    if (!m_crop.acceptsFullRasterUpgrade(path)) {
        return;
    }
    if (image.isNull()) {
        return;
    }
    ImageItem *item = m_crop.target();
    if (!item || item->path() != path) {
        m_crop.clearAwaitingFull();
        return;
    }
    if (!sampleCoversNativeLogical(path, image)
        && ImageCache::longEdge(image) <= item->displayPixelLongEdge()) {
        // Smaller or equal sample — keep waiting for a better delivery.
        return;
    }

    // Cache the native raster for Apply accuracy, but do not reinstall onto the
    // live crop item. Swapping multi-MP pixels mid-draft made crop feel like it
    // "loads full first" and stalled interaction; draft stays on the sample
    // that was present at enter. applyCropCommit prefers cache when ready.
    if (!path.isEmpty()) {
        ImageCache::put(path, image);
    }
    m_crop.clearAwaitingFull();
    flashHud(tr("Crop"), tr("Full image ready"));
}

void ImageView::restoreSessionCropAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState app;
    bool have = false;
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : m_sessionId.currentIdValue();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = *it;
            have = true;
        }
    }
    if (!have && item->sessionHasCrop()) {
        app.hasCrop = true;
        app.cropRect = item->sessionCropRect();
        app.contentHFlip = item->contentHFlip();
        app.contentVFlip = item->contentVFlip();
        have = true;
    }
    if (!have) {
        if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
            app = *st;
            have = true;
        } else {
            return;
        }
    }
    const QString path = item->path();
    const QImage full = fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    if (isImageMode()) {
        item->setItemRotation(0.0);
        item->setItemShear(0.0);
    } else {
        item->setItemRotation(app.rotation);
        item->setItemShear(app.shear);
    }
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    if (!full.isNull()) {
        if (!tryRematerializeFromHost(item, app)) {
            installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
            if (!ContentXform::equal(
                    item->hasAppliedContentXform() ? item->appliedContentXform()
                                                   : ContentXform::Value{},
                    ContentXform::Value::fromState(app))) {
                rematerializeItemContent(item, app);
            }
        }
    } else {
        rematerializeItemContent(item, app);
    }
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}

void ImageView::toggleCropMode()
{
    setCropMode(!m_crop.active());
}

void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Undo/redo after-image: pixels are already content-baked — attach only.
    if (!src.isNull()) {
        attachDisplaySample(item, src, state, SessionAppearance::PixelKind::FullSource);
    } else {
        item->setSessionCrop(state.hasCrop, state.cropRect);
        item->setContentHFlip(state.contentHFlip);
        item->setContentVFlip(state.contentVFlip);
        item->setAppliedContentXform(ContentXform::Value::fromState(state));
    }
    applyState(item, state);
    // Seed appearance with the full state (including cropRotation) before
    // commitItemSessionEdit, which rebuilds the slot via captureState.
    {
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentIdValue();
        }
        if (sid != kInvalidSessionImageId) {
            WorkspaceItemState slot = state;
            slot.sessionId = sid;
            slot.path = item->path();
            m_appearance.set(sid, slot);
        } else {
            m_itemStateBook.set(item->path(), state);
        }
    }
    // Appearance persistence is commitItemSessionEdit → m_appearance (by id).
    // Do not write crop state into the path map for bound tiles.
    commitItemSessionEdit(item);
    // Undo back to identity: commit no longer writes identity (avoids wiping
    // good rows on noisy commits), so clear durable state explicitly.
    if (!SessionAppearance::hasContentAppearance(state)) {
        ThumtooCache::clearContentAppearance(item->path());
    }
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}

void ImageView::applyAutoCrop()
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.active()) {
        return;
    }
    QImage src = item->sourceImage();
    if (src.isNull()) {
        src = item->pixmap().toImage();
    }
    if (src.isNull()) {
        return;
    }
    ensureCropRectValid();
    const QRectF cr = item->contentRect();
    if (cr.width() < 1.0 || cr.height() < 1.0) {
        return;
    }

    const QRect search = m_crop.sourceSearchRectFromDraft(cr, src.size());
    if (!search.isValid() || search.isEmpty()) {
        return;
    }

    QRect trimmed;
    if (!ImageLoader::autoTrimRect(src, search, &trimmed)) {
        return;
    }
    // Small pad so text glyphs are not tight against the frame.
    trimmed = CropGeometry::paddedIntersectedRect(trimmed, src.size(), 2);
    if (!trimmed.isValid() || trimmed.isEmpty()) {
        return;
    }

    m_crop.setRectFromSourcePixelTrim(cr, src.size(), trimmed);
    ensureCropRectValid();
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::applyCrop()
{
    // Soft draft is valid — crop is content-space. Do not wait on multi-MP load.
    m_crop.clearAwaitingFull();
    leaveCropModeInternal(true);
}

void ImageView::cancelCrop()
{
    leaveCropModeInternal(false);
}

void ImageView::applyStoredAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    const WorkspaceItemState *app = nullptr;
    WorkspaceItemState fallback;
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        // Seed orient/flip/grade from path XDG when the id slot is still empty
        // (restart / first bind). Crop is never seeded from path (IDENTITY).
        seedSessionAppearanceFromState(sid, item->path());
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = &(*it);
        }
        // Bound with no durable content after seed = full frame.
        // NEVER fall back to the path map — that leaks crop/flip across
        // independent session images that share a file path.
    } else {
        // Path map only when unbound (no session image id).
        if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
            fallback = *st;
            app = &fallback;
        }
    }
    if (!app) {
        return;
    }
    // Geometry content ops bake pixels. Reload full on-disk source when those
    // ops are present so rematerialize/install can run from host more than once.
    const bool needsFullSource = app->hasCrop || app->contentHFlip || app->contentVFlip
        || app->contentQuarterTurns != 0;
    if (needsFullSource) {
        const QImage full = fullRasterForEdit(item->path());
        if (!full.isNull()) {
            installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource,
                                 sid);
            return;
        }
    }
    // Grade-only or reload failed: chrome + grade on current pixels.
    rematerializeItemContent(item, *app);
}

void ImageView::applyContentAppearanceAfterDecode(ImageItem *item)
{
    if (!item) {
        return;
    }
    const WorkspaceItemState *app = nullptr;
    WorkspaceItemState fallback;
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        seedSessionAppearanceFromState(sid, item->path());
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = &(*it);
        }
    } else {
        if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
            fallback = *st;
            app = &fallback;
        }
    }
    if (!app || !SessionAppearance::hasContentAppearance(*app)) {
        return;
    }
    // Caller just installed full on-disk pixels; do not load again.
    rematerializeItemContent(item, *app);
}

void ImageView::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    if (!item) {
        return;
    }
    const QRectF cr = item->contentRect();
    const QRectF local = m_crop.clampLocalCrop(localCrop, cr);
    if (local.isEmpty()) {
        return;
    }
    const QPointF off = item->offset();
    // Crop mode always edits the full on-disk image — store absolute source rect.
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    const int iw = item->imageSize().width();
    const int ih = item->imageSize().height();
    const QRect disp = m_crop.sourceCropFromLocal(
        local, off, iw, ih, item->itemHFlip(), item->itemVFlip());

    // cropSourceSize must be the post-orient full-frame size the draft was
    // edited in — file-native layoutSize without crop — not a soft sample or
    // prior crop intrinsic (that breaks second-enter scaleCropRect).
    QSize cropBasis(iw, ih);
    {
        const QString path = item->path();
        QSize fileNative = logicalSizeForPath(path);
        if (isPositiveSize(fileNative) && fileNative.width() > 1
            && !isProvisionalImageSize(path)) {
            WorkspaceItemState orientOnly;
            // Prefer appearance turns when present.
            SessionImageId sidR = item->sessionId();
            if (sidR == kInvalidSessionImageId && m_crop.hasTargetId()) {
                sidR = m_crop.targetIdValue();
            }
            if (sidR == kInvalidSessionImageId && isImageMode()) {
                sidR = m_sessionId.currentIdValue();
            }
            if (sidR != kInvalidSessionImageId) {
                if (const WorkspaceItemState *it = m_appearance.get(sidR)) {
                    orientOnly.contentQuarterTurns = it->contentQuarterTurns;
                    orientOnly.contentHFlip = it->contentHFlip;
                    orientOnly.contentVFlip = it->contentVFlip;
                }
            } else {
                orientOnly.contentHFlip = item->contentHFlip();
                orientOnly.contentVFlip = item->contentVFlip();
                orientOnly.contentQuarterTurns = item->hasAppliedContentXform()
                    ? item->appliedContentXform().quarterTurns : 0;
            }
            const QSize oriented = ContentXform::layoutSize(fileNative, orientOnly);
            if (isPositiveSize(oriented) && oriented.width() > 1) {
                cropBasis = oriented;
            }
        }
    }

    WorkspaceItemState s = captureState(item);
    // Appearance is keyed by SessionImageId only. Prefer the locked crop target
    // id; never invent one from the navigation cursor while other tiles exist.
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && m_crop.hasTargetId()) {
        sid = m_crop.targetIdValue();
    }
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            // Keep content transforms from the session-image store.
            s.contentQuarterTurns = it->contentQuarterTurns;
            s.contentHFlip = it->contentHFlip;
            s.contentVFlip = it->contentVFlip;
        }
    }
    s.sessionId = sid;
    s.sessionIndex = item->sessionIndex();
    // Full-frame draft clears the session crop (Reset or expanded to entire image).
    const bool fullFrame =
        qAbs(local.left() - cr.left()) < 0.5
        && qAbs(local.top() - cr.top()) < 0.5
        && qAbs(local.width() - cr.width()) < 0.5
        && qAbs(local.height() - cr.height()) < 0.5;
    if (fullFrame && m_crop.isNearZeroRotation(CropGeometry::kFreeRotationEps)) {
        s = SessionAppearance::withoutCrop(s);
        s.cropSourceSize = QSize();
        s.cropRotation = 0.0;
    } else {
        s.hasCrop = true;
        s.cropRect = disp;
        s.cropSourceSize = cropBasis;
        s.cropRotation = m_crop.currentRotation();
        if (cropBasis != QSize(iw, ih)
            && qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
            qWarning().noquote()
                << QStringLiteral(
                       "[crop] record basis=%1x%2 imageSize=%3x%4 rect=%5x%6+%7x%8")
                       .arg(cropBasis.width()).arg(cropBasis.height())
                       .arg(iw).arg(ih)
                       .arg(disp.x()).arg(disp.y()).arg(disp.width()).arg(disp.height());
        }
    }
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    if (sid != kInvalidSessionImageId) {
        m_appearance.set(sid, s);
    } else {
        // Unbound only: path map is the sole store.
        m_itemStateBook.set(item->path(), s);
    }
}

void ImageView::pushCropAppearanceUndo(ImageItem *item, const QString &text)
{
    if (!m_undoStack || !item || !m_crop.isEnterValid()) {
        return;
    }
    class CropCommand : public QUndoCommand {
    public:
        CropCommand(ImageView *view, ImageItem *item,
                    const QImage &beforeSrc, const QImage &afterSrc,
                    const WorkspaceItemState &beforeSt,
                    const WorkspaceItemState &afterSt,
                    const QString &text)
            : m_view(view)
            , m_item(item)
            , m_beforeSrc(beforeSrc)
            , m_afterSrc(afterSrc)
            , m_beforeSt(beforeSt)
            , m_afterSt(afterSt)
        {
            setText(text);
        }
        void undo() override { apply(m_beforeSrc, m_beforeSt); }
        void redo() override { apply(m_afterSrc, m_afterSt); }
    private:
        void apply(const QImage &src, const WorkspaceItemState &st)
        {
            if (!m_view || !m_item) {
                return;
            }
            m_view->applyCropAppearance(m_item, src, st);
        }
        ImageView *m_view;
        ImageItem *m_item;
        QImage m_beforeSrc;
        QImage m_afterSrc;
        WorkspaceItemState m_beforeSt;
        WorkspaceItemState m_afterSt;
    };
    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    // captureState pulls cropRotation from appearance
    // (recordSessionCrop + commitItemSessionEdit).
    m_undoStack->push(new CropCommand(
        this, item, m_crop.enterSourceRef(), item->sourceImage().copy(),
        m_crop.enterStateRef(), afterSt, text));
}

bool ImageView::applyCropCommit(ImageItem *item)
{
    // Returns true when Workspace placement rotation should keep the crop-frame
    // angle (non-full-frame commit).
    ensureCropRectValid();

    const QRectF full = item->contentRect();
    const bool fullFrame = m_crop.isFullFrameDraft(full);
    // Record content-space crop while the draft frame is still valid.
    recordSessionCrop(item, m_crop.draftRectOr(full));
    if (!fullFrame) {
        // --- Workspace footprint math (verify) ---
        // During crop mode the item is axis-aligned (placement rotation stashed).
        // Content units: m_crop.rect is in item content space (same as contentRect).
        // Scene size of the draft selection:
        //   footW = m_crop.rect.width()  * itemScaleX
        //   footH = m_crop.rect.height() * itemScaleY
        // After Apply we set intrinsic to (cropW, cropH) in the *same* content
        // units and keep the same scale → scene size unchanged.
        const qreal sx0 = item->itemScaleX();
        const qreal sy0 = item->itemScaleY() > 0.0 ? item->itemScaleY() : sx0;
        qreal cropW = 0.0;
        qreal cropH = 0.0;
        qreal footW = 0.0;
        qreal footH = 0.0;
        m_crop.draftFootprint(sx0, sy0, &cropW, &cropH, &footW, &footH);
        const QPointF cropSceneCenter = item->mapToScene(m_crop.draftCenterLocal());

        const QString path = item->path();
        bool hostFromCache = false;
        QImage host = pickCropApplyHost(item, path, &hostFromCache);
        if (host.isNull()) {
            if (!hostFromCache && item->hasAppliedContentXform()
                && item->appliedContentXform().hasCrop) {
                flashHud(tr("Crop"), tr("Full image not ready — try again"));
            } else {
                flashHud(tr("Crop"), tr("No pixels to crop"));
            }
            return false;
        }

        WorkspaceItemState st;
        SessionImageId sid = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : m_crop.targetIdValue();
        if (sid == kInvalidSessionImageId) {
            sid = m_sessionId.currentIdValue();
        }
        if (sid != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = m_appearance.get(sid)) {
                st = *app;
            }
        }
        if (!st.hasCrop) {
            st = captureState(item);
            st.hasCrop = true;
            st.cropRect = m_crop.integerCropForOffset(item->offset());
            st.cropSourceSize = item->imageSize();
            st.cropRotation = m_crop.currentRotation();
            if (sid != kInvalidSessionImageId) {
                m_appearance.set(sid, st);
            }
        }

        // materializeDisplay: prefer unoriented ImageCache host + full want.
        // Draft item pixels may be orient- or crop-baked — crop-only bake then.
        WorkspaceItemState bake = st;
        if (!hostFromCache) {
            bake.contentQuarterTurns = 0;
            bake.contentHFlip = false;
            bake.contentVFlip = false;
        }
        QImage sample = host;
        const bool multiMp =
            ImageCache::longEdge(sample) > ContentXform::kGuiMaterializeMaxEdge;
        if (multiMp) {
            sample = ImageCache::clampToMaxEdge(
                sample, ContentXform::kGuiMaterializeMaxEdge);
        }
        const QImage display = SessionAppearance::materializeDisplay(
            sample, bake,
            multiMp ? SessionAppearance::PixelKind::SoftPreview
                    : SessionAppearance::PixelKind::FullSource);
        if (display.isNull()) {
            flashHud(tr("Crop"), tr("Crop bake failed"));
            return false;
        }

        // Placement scale is never written by crop (Workspace Zoom stays put).
        // Geometry = layoutSize(fileNative, st) via applyContentLayoutSize only.
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
            qWarning().noquote()
                << QStringLiteral(
                       "[crop] Apply path=%1 host=%2x%3 cache=%4 display=%5x%6 "
                       "cropDraft=%7x%8 foot=%9x%10 scaleKeep=%11x%12 "
                       "imageSizeBefore=%13x%14")
                       .arg(path)
                       .arg(host.width()).arg(host.height())
                       .arg(hostFromCache ? 1 : 0)
                       .arg(display.width()).arg(display.height())
                       .arg(cropW).arg(cropH)
                       .arg(footW).arg(footH)
                       .arg(sx0).arg(sy0)
                       .arg(item->imageSize().width()).arg(item->imageSize().height());
        }

        // Single attach path (layoutSize only — never soft size as intrinsic).
        // Enter may have installed FullSource host; SoftPreview is ignored when
        // m_source is set (setPreviewImage no-op). Clear first so the crop bake
        // replaces full-frame pixels — otherwise canvas stretches full into the
        // crop box and filmstrip gets img=full (cropApply with uncropped pixels).
        //
        // Hold viewport paints across clear → layout → pixels → fit so the view
        // never composites crop pixels into the pre-crop contentRect (or the
        // reverse). fitItem still runs under m_crop.active(); it must not treat Apply
        // as draft (see fitItem cropDraft).
        const bool holdPaint = viewport() && viewport()->updatesEnabled();
        if (holdPaint) {
            viewport()->setUpdatesEnabled(false);
        }
        item->clearDecodedPixels();
        // Geometry before pixels: empty item with crop intrinsic, then bake.
        applyContentLayoutSize(item, st);
        {
            const QSize isz = item->imageSize();
            if (isz.width() <= 1 || isz.height() <= 1) {
                qCritical("applyCropCommit: layoutSize after crop is %dx%d (draft %gx%g path=%s)",
                          isz.width(), isz.height(), cropW, cropH, qPrintable(path));
                // Last resort: draft content units (still better than 1x1).
                item->setIntrinsicSize(ContentXform::roundedSizeAtLeast1(cropW, cropH));
            }
        }
        const auto pixelKind = multiMp ? SessionAppearance::PixelKind::SoftPreview
                                       : SessionAppearance::PixelKind::FullSource;
        attachDisplaySample(item, display, st, pixelKind);
        // Restore enter placement scale if something else mutated it during draft.
        if (m_crop.enterScaleX() > 1e-6) {
            const qreal sx = m_crop.enterScaleX();
            const qreal sy = m_crop.enterScaleY() > 1e-6 ? m_crop.enterScaleY() : sx;
            item->setItemScale(sx, sy);
        }
        alignItemCenterToScene(item, cropSceneCenter);

        // Multi-MP: soft stand-in now; pure full rematerialize after leave.
        // scheduleAsyncHostRematerialize is blocked while crop freeze is on —
        // queue here and flush from clearCropModeState after unfreeze.
        if (hostFromCache && multiMp) {
            m_crop.queuePendingFullRematerialize(path, sid, st);
        }

        if (isWorkspaceMode()) {
            item->setItemRotation(m_crop.currentRotation());
            updateWorkspaceSceneRect();
        } else if (isImageMode()) {
            m_framing.armFit();
            fitItem(item, currentFitAspectMode());
        } else if (isGalleryMode()) {
            applyLayout(GalleryPackReason::ContentChange);
        }
        if (holdPaint) {
            viewport()->setUpdatesEnabled(true);
            viewport()->update();
        }

        commitItemSessionEdit(item);

        if (sid != kInvalidSessionImageId) {
            // Prefer the crop bake we just materialized — not displayImage(), which
            // can still be pre-crop if soft attach was rejected.
            QImage appearance = display;
            if (appearance.isNull()) {
                appearance = sessionAppearanceImage(item);
            }
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, path, appearance);
                emit sessionCropApplied(sid, path, appearance, /*hasCrop=*/true);
            }
        }
        pushCropAppearanceUndo(item, tr("Crop"));
        flashHud(tr("Cropped"),
                 QStringLiteral("%1×%2")
                     .arg(item->imageSize().width())
                     .arg(item->imageSize().height()));
        return isWorkspaceMode();
    }

    // Reset / full frame: keep full pixels; clear session crop metadata.
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        // Drop the enter-time crop-frame offset; restore pre-crop pose.
        if (m_crop.isEnterValid()) {
            m_crop.restoreEnterPlacementPose(item);
        }
        updateWorkspaceSceneRect();
    } else if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
    commitItemSessionEdit(item);
    {
        SessionImageId sid = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : m_crop.targetIdValue();
        if (sid == kInvalidSessionImageId) {
            sid = m_sessionId.currentIdValue();
        }
        if (sid != kInvalidSessionImageId) {
            QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                // fromCropApply path on filmstrip: clear crop sticky + badge.
                emit sessionCropApplied(sid, item->path(), appearance, /*hasCrop=*/false);
            }
        }
    }
    if (m_crop.isEnterValid()
        && (m_crop.enterHadCrop()
            || m_crop.enterSourceDiffersFrom(item->sourceImage().size()))) {
        pushCropAppearanceUndo(item, tr("Crop reset"));
    }
    flashHud(tr("Crop reset"), tr("Full image"));
    return false;
}

void ImageView::cancelCropShowingFullImage(ImageItem *item)
{
    // Esc / toggle off: put the previous session crop back on the canvas.
    restoreSessionCropAppearance(item);
    if (isWorkspaceMode() && m_crop.isEnterValid()) {
        m_crop.restoreEnterPlacementPose(item);
    }
}

void ImageView::clearCropModeState()
{
    // Unsuppress LOD before binding is cleared.
    m_crop.releaseTargetTileLod();
    if (!m_crop.target()) {
        if (ImageItem *byId = cropSessionBoundItem()) {
            byId->setTileLodSuppressed(false);
        }
    }
    // Apply may have queued a full bake while freeze was still on.
    QString pendingPath;
    SessionImageId pendingSid = kInvalidSessionImageId;
    WorkspaceItemState pendingWant;
    const bool pendingFull =
        m_crop.takePendingFullRematerialize(&pendingPath, &pendingSid, &pendingWant);
    m_crop.clear();
    emit cropModeChanged(false);
    emit statusChanged();
    viewport()->unsetCursor();
    viewport()->update();
    if (pendingFull && !pendingPath.isEmpty()) {
        scheduleAsyncHostRematerialize(pendingPath, pendingSid, pendingWant);
    }
}

void ImageView::leaveCropModeInternal(bool apply)
{
    if (!m_crop.active()) {
        return;
    }
    ImageItem *item = cropTargetItem();
    // When a Workspace crop is committed, placement rotation follows the crop
    // frame angle (straightened pixels + matching pose). Cancel / full-frame
    // restore the pre-crop placement instead.
    bool preserveCropFrameRotation = false;
    if (apply && item) {
        preserveCropFrameRotation = applyCropCommit(item);
    } else if (item && m_crop.isShowingFullImage()) {
        cancelCropShowingFullImage(item);
    }
    // Restore pre-crop placement rotation unless Apply already set it from the
    // crop frame (Workspace non-full-frame commit).
    if (item && !preserveCropFrameRotation) {
        m_crop.restoreStashedPlacement(item);
    }
    clearCropModeState();
}

QPolygonF ImageView::cropPolygonItemLocal() const
{
    return m_crop.polygonLocal();
}


QPolygonF ImageView::cropPolygonView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.hasValidRect()) {
        return {};
    }
    const QPolygonF local = cropPolygonItemLocal();
    QPolygonF viewPoly;
    viewPoly.reserve(local.size());
    for (const QPointF &pt : local) {
        viewPoly << QPointF(mapFromScene(item->mapToScene(pt)));
    }
    return viewPoly;
}

QRectF ImageView::cropRectView() const
{
    return cropPolygonView().boundingRect().normalized();
}

QRect ImageView::cropExpandButtonView() const
{
    const CropGeometry::CropButtonLayout L = cropChromeButtons(m_crop.active(), cropRectView(), viewport() ? viewport()->rect() : QRect());
    return L.valid ? L.expand : QRect();
}

QRect ImageView::cropAutoButtonView() const
{
    const CropGeometry::CropButtonLayout L = cropChromeButtons(m_crop.active(), cropRectView(), viewport() ? viewport()->rect() : QRect());
    return L.valid ? L.autoBtn : QRect();
}

QRect ImageView::cropResetButtonView() const
{
    const CropGeometry::CropButtonLayout L = cropChromeButtons(m_crop.active(), cropRectView(), viewport() ? viewport()->rect() : QRect());
    return L.valid ? L.reset : QRect();
}

QRect ImageView::cropCancelButtonView() const
{
    const CropGeometry::CropButtonLayout L = cropChromeButtons(m_crop.active(), cropRectView(), viewport() ? viewport()->rect() : QRect());
    return L.valid ? L.cancel : QRect();
}

QRect ImageView::cropCloseButtonView() const
{
    const CropGeometry::CropButtonLayout L = cropChromeButtons(m_crop.active(), cropRectView(), viewport() ? viewport()->rect() : QRect());
    return L.valid ? L.apply : QRect();
}



void ImageView::paintCropDimOutside(QPainter &painter, const QPolygonF &cropViewPoly)
{
    if (!viewport()) {
        return;
    }
    CropGeometry::paintDimOutside(painter, viewport()->rect(), cropViewPoly);
}

void ImageView::paintCropFrame(QPainter &painter, const QPolygonF &cropViewPoly)
{
    CropGeometry::paintFrame(painter, cropViewPoly);
}

void ImageView::paintCropResizeHandles(QPainter &painter, const QPolygonF &cropViewPoly)
{
    CropGeometry::paintResizeHandles(painter, cropViewPoly,
        [this](CropHandle h) { return m_crop.isHandleHot(h); });
}

void ImageView::paintCropRotateKnobs(QPainter &painter, const QPolygonF &cropViewPoly)
{
    const bool hot = (m_crop.currentHoverHandle() == CropHandle::Rotate
                      || m_crop.currentActiveHandle() == CropHandle::Rotate);
    CropGeometry::paintRotateKnobs(painter, cropViewPoly, hot);
}

void ImageView::paintCropMoveGrip(QPainter &painter, const QPolygonF &cropViewPoly)
{
    const bool hot = (m_crop.currentHoverHandle() == CropHandle::Move
                      || m_crop.currentActiveHandle() == CropHandle::Move);
    CropGeometry::paintMoveGrip(painter, cropViewPoly, hot);
}

void ImageView::drawCropTextButton(QPainter &painter, const QRect &btn, CropHandle kind,
                                   const QString &label, CropGeometry::CropBtnRole role, bool toggled)
{
    CropGeometry::paintTextButton(painter, btn, m_crop.currentHoverHandle() == kind, label,
                                  role, toggled);
}

void ImageView::paintCropActionButtons(QPainter &painter)
{
    // Controls: outside below crop when possible, inside if off-screen.
    // Same design language as Workspace chrome (HANDLES.md):
    //   toggle  = rounded square / stronger on-state
    //   action  = dark + accent ring
    //   neutral = grey (Cancel)
    //   commit  = filled accent (Apply)
    // Local QPoint names must not hide QObject::tr — use ImageView::tr.
    drawCropTextButton(painter, cropExpandButtonView(), CropHandle::ExpandToggle,
                       ImageView::tr("Expand"), CropGeometry::CropBtnRole::Toggle, m_crop.isAllowExpand());
    drawCropTextButton(painter, cropAutoButtonView(), CropHandle::Auto, ImageView::tr("Auto"),
                       CropGeometry::CropBtnRole::Action);
    drawCropTextButton(painter, cropResetButtonView(), CropHandle::Reset, ImageView::tr("Reset"),
                       CropGeometry::CropBtnRole::Action);
    drawCropTextButton(painter, cropCancelButtonView(), CropHandle::Cancel, ImageView::tr("Cancel"),
                       CropGeometry::CropBtnRole::Neutral);
    drawCropTextButton(painter, cropCloseButtonView(), CropHandle::Close, ImageView::tr("Apply"),
                       CropGeometry::CropBtnRole::Commit);
}



void ImageView::paintCropSizeBadge(QPainter &painter, const QRect &cropView)
{
    const QSize cropSz = m_crop.draftPixelSize();
    CropGeometry::paintSizeBadge(painter, cropView, cropSz.width(), cropSz.height());
}

void ImageView::paintCropOverlay(QPainter &painter)
{
    if (!m_crop.active()) {
        return;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.hasValidRect()) {
        return;
    }
    ensureCropRectValid();
    const QPolygonF cropViewPoly = cropPolygonView();
    const QRect cropView = cropViewPoly.boundingRect().toRect().normalized();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    paintCropDimOutside(painter, cropViewPoly);
    paintCropFrame(painter, cropViewPoly);
    paintCropResizeHandles(painter, cropViewPoly);
    paintCropRotateKnobs(painter, cropViewPoly);
    paintCropMoveGrip(painter, cropViewPoly);
    paintCropActionButtons(painter);
    paintCropSizeBadge(painter, cropView);

    painter.restore();
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !CropSession::isGeometryHandle(h)) {
        return;
    }
    const QPointF startLocal = item->mapFromScene(mapToScene(viewPos));
    m_crop.beginHandleDrag(h, m_crop.currentRect(), startLocal);
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.isHandleDragging()) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    m_crop.applyActiveHandleDrag(local, item->contentRect(), 4.0,
                                 mods & Qt::ShiftModifier, mods & Qt::ControlModifier);
    viewport()->update();
}

void ImageView::endCropHandleDrag()
{
    if (ImageItem *item = cropTargetItem()) {
        m_crop.endHandleDragClamped(item->contentRect());
    } else {
        m_crop.endHandleDrag();
    }
    viewport()->update();
}

void ImageView::beginCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    if (!item->contentRect().contains(local)) {
        return;
    }
    m_crop.beginRubberDraft(local);
    viewport()->update();
}

void ImageView::updateCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.isRubberbanding()) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const QRectF cr = item->contentRect();
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    m_crop.applyRubberBand(local, cr, mods & Qt::ShiftModifier, mods & Qt::ControlModifier);
    viewport()->update();
}

void ImageView::endCropRubberBand()
{
    m_crop.endRubber();
    if (ImageItem *item = cropTargetItem()) {
        m_crop.ensureRectValid(item->contentRect());
    }
    viewport()->update();
}

CropHandle ImageView::cropHandleAt(const QPoint &viewPos) const
{
    if (!m_crop.active()) {
        return CropHandle::None;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.hasValidRect()) {
        return CropHandle::None;
    }
    const CropGeometry::CropButtonLayout buttons =
        cropChromeButtons(m_crop.active(), cropRectView(),
                          viewport() ? viewport()->rect() : QRect());
    const CropGeometry::CropFrameViewAnchors anchors =
        CropGeometry::frameViewAnchors(cropPolygonView());
    return CropGeometry::hitTestCropChrome(viewPos, buttons, anchors);
}

