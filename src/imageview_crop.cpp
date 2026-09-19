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

/** Disable viewport updates for a critical section; restore and update on scope exit. */
struct ViewportUpdateHold {
    QWidget *viewport = nullptr;
    bool held = false;

    explicit ViewportUpdateHold(QWidget *vp)
        : viewport(vp)
        , held(vp && vp->updatesEnabled())
    {
        if (held) {
            viewport->setUpdatesEnabled(false);
        }
    }

    ~ViewportUpdateHold()
    {
        if (held && viewport) {
            viewport->setUpdatesEnabled(true);
            viewport->update();
        }
    }

    ViewportUpdateHold(const ViewportUpdateHold &) = delete;
    ViewportUpdateHold &operator=(const ViewportUpdateHold &) = delete;
};

} // namespace

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


void ImageView::preserveWorkspaceItemCenter(ImageItem *item, const QPointF &center0,
                                               qreal footW0, qreal footH0)
{
    // Do not rescale full frame into the previous crop footprint.
    if (!item || !isWorkspaceMode() || footW0 <= 1.0 || footH0 <= 1.0) {
        return;
    }
    alignItemCenterToScene(item, center0);
}

void ImageView::fitImageOrUpdateWorkspace(ImageItem *item)
{
    if (!item) {
        return;
    }
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}

void ImageView::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    m_crop.ensureRectValid(item->contentRect());
}

void ImageView::alignCropFrameCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item || !m_crop.hasValidRect()) {
        return;
    }
    CropSession::applyScenePosDelta(
        item,
        PlacementLinear::scenePosDeltaToAlign(
            item->mapToScene(m_crop.draftCenterLocal()), sceneAnchor));
}

void ImageView::alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item) {
        return;
    }
    // Pixmap is centred on the item origin (offset -w/2,-h/2).
    CropSession::applyScenePosDelta(
        item,
        PlacementLinear::scenePosDeltaToAlign(
            item->mapToScene(QPointF(0.0, 0.0)), sceneAnchor));
}

void ImageView::finishWorkspaceCropEnter(ImageItem *item, const QPointF &workspaceAnchorScene)
{
    if (!item) {
        return;
    }
    // If there was no stored crop angle but the tile was free-rotated,
    // seed the draft rotation so the frame matches the prior pose while
    // the item stays axis-aligned for editing.
    if (m_crop.seedRotationFromStashedPlacement(CropGeometry::kFreeRotationEps)) {
        ensureCropRectValid();
    }
    alignCropFrameCenterToScene(item, workspaceAnchorScene);
    updateWorkspaceSceneRect();
}


void ImageView::notifyCropModeEntered()
{
    flashHud(tr("Crop mode"),
             tr("Apply commits · Esc cancels"));
    emit cropModeChanged(true);
    emit statusChanged();
}

void ImageView::beginCropEnterSession(ImageItem *item)
{
    beginCropEnterSession(item);
}

bool ImageView::handleNullEnterFullRaster(const QString &path, bool hadCrop)
{
    if (CropSession::shouldRequestFullOnNullEnter(hadCrop, path)) {
        requestCropFullRaster(path);
        m_crop.setAwaitingFull(path);
        flashHud(tr("Crop"), tr("Loading full image…"));
    }
    flashHud(tr("Crop"), tr("Image not cached yet — try again"));
    return false;
}

bool ImageView::resolveApplyHostAndState(ImageItem *item, QImage *host, bool *hostFromCache,
                                         WorkspaceItemState *st, SessionImageId *sid)
{
    if (!item || !host || !hostFromCache || !st || !sid) {
        return false;
    }
    const QString path = item->path();
    *host = CropSession::pickApplyHost(item, path, hostFromCache);
    const CropSession::ApplyHostStatus hostSt =
        CropSession::classifyApplyHost(*host, *hostFromCache, item);
    if (flashApplyHostFailure(hostSt)) {
        return false;
    }
    *sid = cropRecordSessionId(item);
    ensureApplyCropState(item, *sid, st);
    return true;
}

void ImageView::captureApplyDraftMetrics(ImageItem *item, qreal *cropW, qreal *cropH,
                                         qreal *footW, qreal *footH, QPointF *sceneCenter)
{
    qreal sx0 = 0.0;
    qreal sy0 = 0.0;
    CropSession::itemScalePair(item, &sx0, &sy0);
    m_crop.draftFootprint(sx0, sy0, cropW, cropH, footW, footH);
    if (sceneCenter) {
        *sceneCenter = item->mapToScene(m_crop.draftCenterLocal());
    }
}

bool ImageView::materializeApplyBake(const QImage &host, bool hostFromCache,
                                     const WorkspaceItemState &st,
                                     CropSession::ApplyBakeResult *baked)
{
    if (!baked) {
        return false;
    }
    *baked = CropSession::materializeApplyDisplay(host, hostFromCache, st);
    if (!baked->ok()) {
        flashHud(tr("Crop"), tr("Crop bake failed"));
        return false;
    }
    return true;
}

void ImageView::finalizeCropResetSuccess(ImageItem *item)
{
    commitItemSessionEdit(item);
    emitCropApplyAppearance(cropRecordSessionId(item), item->path(), item, QImage(),
                            /*hasCrop=*/false);
    if (m_crop.shouldPushResetUndo(item->sourceImage().size())) {
        pushCropAppearanceUndo(item, tr("Crop reset"));
    }
    flashHud(tr("Crop reset"), tr("Full image"));
}

void ImageView::finalizeCropApplySuccess(ImageItem *item, SessionImageId sid,
                                         const QString &path, const QImage &display)
{
    commitItemSessionEdit(item);
    emitCropApplyAppearance(sid, path, item, display, /*hasCrop=*/true);
    pushCropAppearanceUndo(item, tr("Crop"));
    flashHud(tr("Cropped"),
             QStringLiteral("%1×%2")
                 .arg(item->imageSize().width())
                 .arg(item->imageSize().height()));
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
    QImage enterSrc = CropSession::pickEnterSnapshotPixels(item);
    WorkspaceItemState enterSt = captureState(item);
    CropSession::seedEnterCropFlags(&enterSt, item);
    m_crop.beginEnterSession(item, enterSrc, enterSt,
                             !enterSrc.isNull() || item->hasDisplayPixels());
    cancelPathRasterForCrop(m_crop.draftPathRef());
    // Workspace: remember displayed image centre so the crop frame can stay fixed.
    const QPointF workspaceAnchorScene = item->mapToScene(QPointF(0.0, 0.0));
    // One paint after full-frame draft is ready (no intermediate crop-on-old-box).
    {
        ViewportUpdateHold paintHold(viewport());
        if (!prepareCropModeFullImage(item)) {
            // prepare may have set mode for fitItem then failed — restore placement
            // before abortEnter clears the stash.
            item->setTileLodSuppressed(false);
            m_crop.abortEnterRestoringPlacement(item);
            flashHud(tr("Crop"), tr("Could not load full image"));
            return false;
        }
        if (isWorkspaceMode()) {
            finishWorkspaceCropEnter(item, workspaceAnchorScene);
        }
        notifyCropModeEntered();
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



bool ImageView::loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const
{
    if (!st || sid == kInvalidSessionImageId) {
        return false;
    }
    if (const WorkspaceItemState *it = m_appearance.get(sid)) {
        *st = *it;
        return true;
    }
    return false;
}

bool ImageView::resolveCropEnterAppearance(ImageItem *item, WorkspaceItemState *app) const
{
    // Prior crop + content flags for *this* session image only — never path map alone.
    const SessionImageId sid = CropSession::resolveSessionIdForItem(
        item, m_sessionId.currentIdValue());
    if (loadSessionAppearance(sid, app)) {
        return true;
    }
    if (CropSession::fillAppearanceFromItemSessionCrop(app, item)) {
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
    QString boundPath;
    if (ImageItem *bound = cropSessionBoundItem()) {
        boundPath = bound->path();
    }
    return m_crop.locksResolvedPath(path, boundPath);
}

void ImageView::cancelPathRasterForCrop(const QString &path)
{
    if (m_pathRaster && !path.isEmpty()) {
        m_pathRaster->cancel(path);
    }
}

void ImageView::rememberCropEnterSizes(const QString &path, const QImage &full)
{
    if (full.isNull() || !sampleCoversNativeLogical(path, full)) {
        return;
    }
    rememberSizeFromDecode(path, full);
    QSize logical = logicalSizeForPath(path);
    if (!isPositiveSize(logical) || isProvisionalImageSize(path)) {
        rememberImageSize(path, full.size());
    }
}


void ImageView::installKeepEnterDisplay(ImageItem *item, const WorkspaceItemState &contentOnly,
                                        const ContentXform::Value &wantX, const QString &path)
{
    CropSession::applyKeepEnterFlags(item, contentOnly, wantX);
    applyContentLayoutSize(item, contentOnly);
    m_crop.markShowingFullImage();
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
        qWarning().noquote()
            << QStringLiteral("[crop] enter-full KEEP display edge=%1 path=%2")
                   .arg(item->displayPixelLongEdge())
                   .arg(path);
    }
}

void ImageView::installDraftEnterDisplay(ImageItem *item,
                                         const CropSession::EnterInstallSample &sample,
                                         const QImage &full, const QString &path)
{
    CropSession::clearItemFreePlacementForDraft(item);
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
        qWarning().noquote()
            << QStringLiteral(
                   "[crop] enter-full path=%1 imageSize=%2x%3 hasDecoded=%4 "
                   "appliedCrop=%5 hostEdge=%6")
                   .arg(path)
                   .arg(item->imageSize().width()).arg(item->imageSize().height())
                   .arg(item->hasDecodedPixels() ? 1 : 0)
                   .arg(sample.hadPriorCrop ? 1 : 0)
                   .arg(ImageCache::longEdge(full));
    }
    CropSession::clearItemPixelsForDraftReinstall(item);
    const WorkspaceItemState &contentOnly = sample.contentOnly;
    const ContentXform::Value &wantX = sample.wantX;
    attachDisplaySample(item, sample.display, contentOnly, sample.kind);
    applyContentLayoutSize(item, contentOnly);
    CropSession::applyEnterDraftFlags(item, wantX);
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
        qWarning().noquote()
            << QStringLiteral(
                   "[crop] enter-full done imageSize=%1x%2 display=%3x%4 "
                   "appliedCrop=%5 contentTurns=%6")
                   .arg(item->imageSize().width()).arg(item->imageSize().height())
                   .arg(sample.display.width()).arg(sample.display.height())
                   .arg(item->sessionHasCrop() ? 1 : 0)
                   .arg(contentOnly.contentQuarterTurns);
    }
    m_crop.markShowingFullImage();
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
    cancelPathRasterForCrop(path);
    rememberCropEnterSizes(path, full);
    // Host cache is undecoded-appearance *source* only (never crop-baked display).
    CropSession::maybePutUnorientedHostCache(
        path, full, unorientedSource, sampleCoversNativeLogical(path, full));

    // Content flips/turns only — crop is drafted on the full post-orient frame.
    const CropSession::EnterInstallSample sample =
        CropSession::prepareEnterInstallSample(full, unorientedSource, app, haveApp);
    const WorkspaceItemState &contentOnly = sample.contentOnly;
    const ContentXform::Value &wantX = sample.wantX;

    // Full-frame already on the item (no crop bake): keep those pixels.
    // Do not rebuild a lower-res graded stand-in — that invites soft↔full thrash.
    if (CropSession::canKeepDisplayForEnter(item, wantX, contentOnly, sample.hadPriorCrop,
                                            sample.needGeomBake)) {
        installKeepEnterDisplay(item, contentOnly, wantX, path);
        return;
    }
    installDraftEnterDisplay(item, sample, full, path);
}

void ImageView::activateCropModeAfterInstall(ImageItem *item)
{
    // Crop chrome + fitItem only after pixels and contentRect match the draft.
    m_crop.activateModeAfterDraft();
    fitImageOrUpdateWorkspace(item);
    m_crop.clearAwaitingFull();
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
    const bool hadCrop = CropSession::appearanceHasCrop(&app, haveApp);

    // Unoriented ImageCache host preferred. Never use item display as the crop
    // base when a prior crop exists — that bake is already cropped, so a second
    // crop would edit the wrong frame and shrink further.
    const CropSession::EnterFullRaster enter = CropSession::pickEnterFullRaster(item, path, hadCrop);
    const QImage &full = enter.image;
    const bool unorientedSource = enter.unoriented;
    if (full.isNull()) {
        return handleNullEnterFullRaster(path, hadCrop);
    }

    // Workspace: lock scene footprint before intrinsic changes on install.
    const QRectF beforeScene = item->mapRectToScene(item->contentRect());
    installFullImageForCrop(item, full, haveApp ? &app : nullptr, haveApp, unorientedSource);
    // Workspace: keep centre so the draft does not jump (scale is placement-only).
    preserveWorkspaceItemCenter(item, beforeScene.center(),
                                beforeScene.width(), beforeScene.height());
    m_crop.initRectFromPriorAppearance(item->contentRect(), item->offset(),
                                       item->imageSize(),
                                       haveApp ? &app : nullptr, haveApp);
    activateCropModeAfterInstall(item);
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
        const int edge = CropSession::fullRasterScheduleEdge(path);
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
    ImageItem *item = m_crop.target();
    if (!item || item->path() != path) {
        if (m_crop.acceptsFullRasterUpgrade(path)) {
            m_crop.clearAwaitingFull();
        }
        return;
    }
    const bool covers = sampleCoversNativeLogical(path, image);
    if (!m_crop.shouldAcceptFullRasterUpgrade(path, image, item, covers)) {
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
    const SessionImageId sid = CropSession::resolveSessionIdForItem(
        item, m_sessionId.currentIdValue());
    if (loadSessionAppearance(sid, &app)) {
        have = true;
    }
    if (!have && CropSession::fillAppearanceFromItemSessionCrop(&app, item)) {
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
    CropSession::applyItemPlacementFromState(item, app, isImageMode());
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
    fitImageOrUpdateWorkspace(item);
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
    if (!m_crop.applyPaddedAutoTrim(cr, src.size(), trimmed)) {
        return;
    }
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


SessionImageId ImageView::cropRecordSessionId(const ImageItem *item) const
{
    return CropSession::sessionIdForRecord(
        item,
        m_crop.hasTargetId() ? m_crop.targetIdValue() : kInvalidSessionImageId,
        isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
}

void ImageView::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    if (!item) {
        return;
    }
    const QRectF cr = item->contentRect();
    const int iw = item->imageSize().width();
    const int ih = item->imageSize().height();
    // Crop mode always edits the full on-disk image — store absolute source rect.
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    const CropSession::RecordGeometry rec = m_crop.computeRecordGeometry(
        localCrop, cr, item->offset(), iw, ih, item->itemHFlip(), item->itemVFlip());
    if (!rec.valid()) {
        return;
    }
    const QRect &disp = rec.sourceRect;

    // cropSourceSize must be the post-orient full-frame size the draft was
    // edited in — file-native layoutSize without crop — not a soft sample or
    // prior crop intrinsic (that breaks second-enter scaleCropRect).
    const SessionImageId sid = cropRecordSessionId(item);
    const WorkspaceItemState *orientApp =
        (sid != kInvalidSessionImageId) ? m_appearance.get(sid) : nullptr;
    const QSize cropBasis = CropSession::cropBasisSize(
        QSize(iw, ih), cropRecordFileNative(item->path()), orientApp, item);

    WorkspaceItemState s = captureState(item);
    // Appearance is keyed by SessionImageId only. Prefer the locked crop target
    // id; never invent one from the navigation cursor while other tiles exist.
    CropSession::mergeOrientFromAppearance(&s, orientApp);
    s.sessionId = sid;
    s.sessionIndex = item->sessionIndex();
    // Full-frame draft clears the session crop (Reset or expanded to entire image).
    m_crop.applyRecordToState(&s, rec, cropBasis);
    if (!rec.clearCrop && cropBasis != QSize(iw, ih)
        && qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")) {
        qWarning().noquote()
            << QStringLiteral(
                   "[crop] record basis=%1x%2 imageSize=%3x%4 rect=%5x%6+%7x%8")
                   .arg(cropBasis.width()).arg(cropBasis.height())
                   .arg(iw).arg(ih)
                   .arg(disp.x()).arg(disp.y()).arg(disp.width()).arg(disp.height());
    }
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    storeCropAppearance(item, sid, s);
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
    CropSession::fillSessionCropFromItem(&afterSt, item);
    // captureState pulls cropRotation from appearance
    // (recordSessionCrop + commitItemSessionEdit).
    m_undoStack->push(new CropCommand(
        this, item, m_crop.enterSourceRef(), item->sourceImage().copy(),
        m_crop.enterStateRef(), afterSt, text));
}

void ImageView::attachCropApplyDisplay(ImageItem *item, const QImage &display,
                                          const WorkspaceItemState &st, bool multiMp,
                                          qreal cropW, qreal cropH, const QString &path,
                                          const QPointF &cropSceneCenter)
{
    // Clear first so the crop bake replaces full-frame pixels — otherwise canvas
    // stretches full into the crop box and filmstrip gets img=full.
    item->clearDecodedPixels();
    // Geometry before pixels: empty item with crop intrinsic, then bake.
    applyContentLayoutSize(item, st);
    CropSession::ensureApplyIntrinsicSize(item, cropW, cropH, path);
    attachDisplaySample(item, display, st, CropSession::applyPixelKind(multiMp));
    m_crop.restoreEnterScale(item);
    alignItemCenterToScene(item, cropSceneCenter);
}

void ImageView::emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                           ImageItem *item, const QImage &preferredDisplay,
                                           bool hasCrop)
{
    if (sid == kInvalidSessionImageId) {
        return;
    }
    // Prefer the crop bake just materialized when provided — not displayImage(),
    // which can still be pre-crop if soft attach was rejected.
    QImage appearance = preferredDisplay;
    if (appearance.isNull() && item) {
        appearance = sessionAppearanceImage(item);
    }
    if (appearance.isNull()) {
        return;
    }
    if (hasCrop) {
        emit sessionAppearanceChanged(sid, path, appearance);
    }
    emit sessionCropApplied(sid, path, appearance, hasCrop);
}

void ImageView::finishCropApplyLayout(ImageItem *item)
{
    if (!item) {
        return;
    }
    if (isWorkspaceMode()) {
        m_crop.applyCommitPlacementRotation(item);
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    } else {
        fitImageOrUpdateWorkspace(item);
    }
}

bool ImageView::flashApplyHostFailure(CropSession::ApplyHostStatus hostSt)
{
    if (hostSt == CropSession::ApplyHostStatus::Ok) {
        return false;
    }
    flashHud(tr("Crop"),
             hostSt == CropSession::ApplyHostStatus::NeedFull
                 ? tr("Full image not ready — try again")
                 : tr("No pixels to crop"));
    return true;
}

void ImageView::ensureApplyCropState(ImageItem *item, SessionImageId sid,
                                     WorkspaceItemState *st)
{
    if (!item || !st) {
        return;
    }
    loadSessionAppearance(sid, st);
    if (!st->hasCrop) {
        *st = captureState(item);
        m_crop.seedApplyCropState(st, item->offset(), item->imageSize());
        if (sid != kInvalidSessionImageId) {
            m_appearance.set(sid, *st);
        }
    }
}

void ImageView::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    if (!item) {
        return;
    }
    if (sid != kInvalidSessionImageId) {
        m_appearance.set(sid, s);
    } else {
        // Unbound only: path map is the sole store.
        m_itemStateBook.set(item->path(), s);
    }
}

QSize ImageView::cropRecordFileNative(const QString &path) const
{
    QSize fileNative = logicalSizeForPath(path);
    if (!isPositiveSize(fileNative) || fileNative.width() <= 1
        || isProvisionalImageSize(path)) {
        return {};
    }
    return fileNative;
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
        qreal cropW = 0.0;
        qreal cropH = 0.0;
        qreal footW = 0.0;
        qreal footH = 0.0;
        QPointF cropSceneCenter;
        captureApplyDraftMetrics(item, &cropW, &cropH, &footW, &footH, &cropSceneCenter);

        const QString path = item->path();
        bool hostFromCache = false;
        QImage host;
        WorkspaceItemState st;
        SessionImageId sid = kInvalidSessionImageId;
        if (!resolveApplyHostAndState(item, &host, &hostFromCache, &st, &sid)) {
            return false;
        }

        CropSession::ApplyBakeResult baked;
        if (!materializeApplyBake(host, hostFromCache, st, &baked)) {
            return false;
        }
        const QImage &display = baked.display;
        const bool multiMp = baked.multiMp;

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
        ViewportUpdateHold paintHold(viewport());
        attachCropApplyDisplay(item, display, st, multiMp, cropW, cropH, path,
                               cropSceneCenter);

        // Multi-MP: soft stand-in now; pure full rematerialize after leave.
        // scheduleAsyncHostRematerialize is blocked while crop freeze is on —
        // queue here and flush from clearCropModeState after unfreeze.
        m_crop.queueFullRematerializeIfSoft(hostFromCache, multiMp, path, sid, st);
        finishCropApplyLayout(item);
        // paintHold restores viewport updates on scope exit

        finalizeCropApplySuccess(item, sid, path, display);
        return isWorkspaceMode();
    }

    // Reset / full frame: keep full pixels; clear session crop metadata.
    if (isWorkspaceMode() && m_crop.isEnterValid()) {
        // Drop the enter-time crop-frame offset; restore pre-crop pose.
        m_crop.restoreEnterPlacementPose(item);
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    } else {
        fitImageOrUpdateWorkspace(item);
    }
    finalizeCropResetSuccess(item);
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
    m_crop.releaseAllTileLod(cropSessionBoundItem());
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
    m_crop.finishLeave(item, preserveCropFrameRotation);
    clearCropModeState();
}

QPolygonF ImageView::cropPolygonView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.hasValidRect()) {
        return {};
    }
    const QPolygonF local = m_crop.polygonLocal();
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

CropGeometry::CropButtonLayout ImageView::cropChromeLayout() const
{
    if (!m_crop.active()) {
        return {};
    }
    return CropGeometry::cropButtonLayout(cropRectView(),
                                          viewport() ? viewport()->rect() : QRect());
}



void ImageView::paintCropChromeButtons(QPainter &painter)
{
    // Controls: outside below crop when possible, inside if off-screen.
    // Same design language as Workspace chrome (HANDLES.md):
    //   toggle  = rounded square / stronger on-state
    //   action  = dark + accent ring
    //   neutral = grey (Cancel)
    //   commit  = filled accent (Apply)
    const auto paintBtn = [this, &painter](const QRect &btn, CropHandle kind,
                                           const QString &label,
                                           CropGeometry::CropBtnRole role, bool toggled = false) {
        CropGeometry::paintTextButton(painter, btn, m_crop.currentHoverHandle() == kind, label,
                                      role, toggled);
    };
    const CropGeometry::CropButtonLayout chrome = cropChromeLayout();
    paintBtn(chrome.expand, CropHandle::ExpandToggle,
             ImageView::tr("Expand"), CropGeometry::CropBtnRole::Toggle, m_crop.isAllowExpand());
    paintBtn(chrome.autoBtn, CropHandle::Auto, ImageView::tr("Auto"),
             CropGeometry::CropBtnRole::Action);
    paintBtn(chrome.reset, CropHandle::Reset, ImageView::tr("Reset"),
             CropGeometry::CropBtnRole::Action);
    paintBtn(chrome.cancel, CropHandle::Cancel, ImageView::tr("Cancel"),
             CropGeometry::CropBtnRole::Neutral);
    paintBtn(chrome.apply, CropHandle::Close, ImageView::tr("Apply"),
             CropGeometry::CropBtnRole::Commit);
}


void ImageView::paintCropFrameDecorations(QPainter &painter, const QPolygonF &cropViewPoly)
{
    if (viewport()) {
        CropGeometry::paintDimOutside(painter, viewport()->rect(), cropViewPoly);
    }
    CropGeometry::paintFrame(painter, cropViewPoly);
    CropGeometry::paintResizeHandles(painter, cropViewPoly,
        [this](CropHandle h) { return m_crop.isHandleHot(h); });
    {
        const bool hot = (m_crop.currentHoverHandle() == CropHandle::Rotate
                          || m_crop.currentActiveHandle() == CropHandle::Rotate);
        CropGeometry::paintRotateKnobs(painter, cropViewPoly, hot);
    }
    {
        const bool hot = (m_crop.currentHoverHandle() == CropHandle::Move
                          || m_crop.currentActiveHandle() == CropHandle::Move);
        CropGeometry::paintMoveGrip(painter, cropViewPoly, hot);
    }
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
    paintCropFrameDecorations(painter, cropViewPoly);
    paintCropChromeButtons(painter);
    {
        const QSize cropSz = m_crop.draftPixelSize();
        CropGeometry::paintSizeBadge(painter, cropView, cropSz.width(), cropSz.height());
    }

    painter.restore();
}

QPointF ImageView::itemLocalFromView(ImageItem *item, const QPoint &viewPos) const
{
    if (!item) {
        return {};
    }
    return item->mapFromScene(mapToScene(viewPos));
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !CropSession::isGeometryHandle(h)) {
        return;
    }
    m_crop.beginHandleDrag(h, m_crop.currentRect(), itemLocalFromView(item, viewPos));
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.isHandleDragging()) {
        return;
    }
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    m_crop.applyActiveHandleDrag(itemLocalFromView(item, viewPos), item->contentRect(),
                                 CropSession::kMinDraftSidePx,
                                 mods & Qt::ShiftModifier, mods & Qt::ControlModifier);
    viewport()->update();
}

void ImageView::endCropHandleDrag()
{
    ImageItem *item = cropTargetItem();
    m_crop.finishHandleDrag(item ? item->contentRect() : QRectF());
    viewport()->update();
}

void ImageView::beginCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QPointF local = itemLocalFromView(item, viewPos);
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
    const QRectF cr = item->contentRect();
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    m_crop.applyRubberBand(itemLocalFromView(item, viewPos), cr,
                           mods & Qt::ShiftModifier, mods & Qt::ControlModifier);
    viewport()->update();
}

void ImageView::endCropRubberBand()
{
    if (ImageItem *item = cropTargetItem()) {
        m_crop.finishRubber(item->contentRect());
    } else {
        m_crop.endRubber();
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
    const CropGeometry::CropFrameViewAnchors anchors =
        CropGeometry::frameViewAnchors(cropPolygonView());
    return CropGeometry::hitTestCropChrome(viewPos, cropChromeLayout(), anchors);
}

