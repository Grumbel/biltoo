// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop enter/apply/leave: docs/CROP_MODE.md (SessionImageId store, full-frame
// draft, clear FullSource before soft crop attach, filmstrip bake emit).

#include "imageview.h"
#include "cropappearancecommand.h"
#include "croppathraster.h"
#include "viewportupdatehold.h"
#include "cropflash.h"
#include "cropdebug.h"
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


ImageItem *ImageView::resolveInactiveCropTarget() const
{
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

ImageItem *ImageView::cropTargetItem() const
{
    // Crop session is bound to one subject for its entire lifetime. Never
    // re-resolve via selection or primaryItem() — that applied the draft to
    // unrelated tiles when selection changed mid-crop (IDENTITY.md).
    if (m_crop.active()) {
        return cropSessionBoundItem();
    }
    return resolveInactiveCropTarget();
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
    const CropFlash::Hud hud = CropFlash::modeEntered();
    flashHud(hud.title, hud.detail);
    emit cropModeChanged(true);
    emit statusChanged();
}

void ImageView::abortCropEnterFailed(ImageItem *item)
{
    // prepare may have set mode for fitItem then failed — restore placement
    // before abortEnter clears the stash.
    if (item) {
        item->setTileLodSuppressed(false);
    }
    m_crop.abortEnterRestoringPlacement(item);
    const CropFlash::Hud hud = CropFlash::loadFailed();
    flashHud(hud.title, hud.detail);
}

void ImageView::beginCropEnterSession(ImageItem *item)
{
    if (!item) {
        return;
    }
    QImage enterSrc = CropSession::pickEnterSnapshotPixels(item);
    WorkspaceItemState enterSt = captureState(item);
    CropSession::seedEnterCropFlags(&enterSt, item);
    m_crop.beginEnterSession(item, enterSrc, enterSt,
                             !enterSrc.isNull() || item->hasDisplayPixels());
    cancelPathRasterForCrop(m_crop.draftPathRef());
}


void ImageView::flashCropLoadingFullHud()
{
    const CropFlash::Hud hud = CropFlash::loadingFull();
    flashHud(hud.title, hud.detail);
}

void ImageView::flashCropNotCachedHud()
{
    const CropFlash::Hud hud = CropFlash::notCached();
    flashHud(hud.title, hud.detail);
}

bool ImageView::handleNullEnterFullRaster(const QString &path, bool hadCrop)
{
    if (CropSession::shouldRequestFullOnNullEnter(hadCrop, path)) {
        requestCropFullRaster(path);
        m_crop.setAwaitingFull(path);
        flashCropLoadingFullHud();
    }
    flashCropNotCachedHud();
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

void ImageView::flashCropBakeFailed()
{
    const CropFlash::Hud hud = CropFlash::bakeFailed();
    flashHud(hud.title, hud.detail);
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
        flashCropBakeFailed();
        return false;
    }
    return true;
}


void ImageView::flashCropResetHud()
{
    const CropFlash::Hud hud = CropFlash::reset();
    flashHud(hud.title, hud.detail);
}

void ImageView::flashCropAppliedHud(ImageItem *item)
{
    if (!item) {
        return;
    }
    const CropFlash::Hud hud =
        CropFlash::applied(item->imageSize().width(), item->imageSize().height());
    flashHud(hud.title, hud.detail);
}

void ImageView::finalizeCropResetSuccess(ImageItem *item)
{
    commitItemSessionEdit(item);
    emitCropApplyAppearance(cropRecordSessionId(item), item->path(), item, QImage(),
                            /*hasCrop=*/false);
    if (m_crop.shouldPushResetUndo(item->sourceImage().size())) {
        pushCropAppearanceUndo(item, CropFlash::undoResetText());
    }
    flashCropResetHud();
}

void ImageView::finalizeCropApplySuccess(ImageItem *item, SessionImageId sid,
                                         const QString &path, const QImage &display)
{
    commitItemSessionEdit(item);
    emitCropApplyAppearance(sid, path, item, display, /*hasCrop=*/true);
    pushCropAppearanceUndo(item, CropFlash::undoCropText());
    flashCropAppliedHud(item);
}



void ImageView::flashCropNeedSingleTargetHud()
{
    const CropFlash::Hud hud = CropFlash::needSingleTarget();
    flashHud(hud.title, hud.detail);
}

void ImageView::flashCropNoImageHud()
{
    const CropFlash::Hud hud = CropFlash::noImage();
    flashHud(hud.title, hud.detail);
}

ImageItem *ImageView::resolveCropEnterTarget()
{
    // Gallery packing cannot host crop UI — MainWindow opens Image mode instead.
    if (isGalleryMode()) {
        return nullptr;
    }
    // Image or Workspace: one explicit subject only.
    if (!hasSingleCropTarget()) {
        flashCropNeedSingleTargetHud();
        return nullptr;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !item->hasDisplayPixels()) {
        flashCropNoImageHud();
        return nullptr;
    }
    return item;
}


QPointF ImageView::workspaceAnchorSceneForItem(ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Workspace: displayed image centre so the crop frame can stay fixed.
    return item->mapToScene(QPointF(0.0, 0.0));
}


bool ImageView::completeCropEnterUnderHold(ImageItem *item,
                                           const QPointF &workspaceAnchorScene)
{
    // One paint after full-frame draft is ready (no intermediate crop-on-old-box).
    ViewportUpdateHold paintHold(viewport());
    if (!prepareCropModeFullImage(item)) {
        abortCropEnterFailed(item);
        return false;
    }
    if (isWorkspaceMode()) {
        finishWorkspaceCropEnter(item, workspaceAnchorScene);
    }
    notifyCropModeEntered();
    return true;
}

bool ImageView::enterCropModeFromUi()
{
    ImageItem *item = resolveCropEnterTarget();
    if (!item) {
        return false;
    }
    cancelZoomRegion();
    // Lock identity + enter snapshot + unrotate placement (IDENTITY.md).
    // m_crop.active() stays false until after the first draft attach.
    beginCropEnterSession(item);
    const QPointF workspaceAnchorScene = workspaceAnchorSceneForItem(item);
    return completeCropEnterUnderHold(item, workspaceAnchorScene);
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
    // Prior crop + content flags for this session image; path map only if unbound.
    return loadRestoreCropAppearance(item, app, nullptr);
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
    CropPathRaster::suspend(m_pathRaster, path);
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



void ImageView::logKeepEnterDisplay(ImageItem *item, const QString &path) const
{
    if (!item) {
        return;
    }
    CropDebug::keepEnterDisplay(item->displayPixelLongEdge(), path);
}

void ImageView::installKeepEnterDisplay(ImageItem *item, const WorkspaceItemState &contentOnly,
                                        const ContentXform::Value &wantX, const QString &path)
{
    CropSession::applyKeepEnterFlags(item, contentOnly, wantX);
    applyContentLayoutSize(item, contentOnly);
    m_crop.markShowingFullImage();
    logKeepEnterDisplay(item, path);
}


void ImageView::logDraftEnterBegin(ImageItem *item, const QString &path,
                                   const CropSession::EnterInstallSample &sample,
                                   const QImage &full) const
{
    if (!item) {
        return;
    }
    CropDebug::draftEnterBegin(path, item->imageSize().width(), item->imageSize().height(),
                               item->hasDecodedPixels(), sample.hadPriorCrop,
                               ImageCache::longEdge(full));
}

void ImageView::logDraftEnterDone(ImageItem *item, const CropSession::EnterInstallSample &sample,
                                  const WorkspaceItemState &contentOnly) const
{
    if (!item) {
        return;
    }
    CropDebug::draftEnterDone(item->imageSize().width(), item->imageSize().height(),
                              sample.display.width(), sample.display.height(),
                              item->sessionHasCrop(), contentOnly.contentQuarterTurns);
}

void ImageView::installDraftEnterDisplay(ImageItem *item,
                                         const CropSession::EnterInstallSample &sample,
                                         const QImage &full, const QString &path)
{
    CropSession::clearItemFreePlacementForDraft(item);
    logDraftEnterBegin(item, path, sample, full);
    CropSession::clearItemPixelsForDraftReinstall(item);
    const WorkspaceItemState &contentOnly = sample.contentOnly;
    const ContentXform::Value &wantX = sample.wantX;
    attachDisplaySample(item, sample.display, contentOnly, sample.kind);
    applyContentLayoutSize(item, contentOnly);
    CropSession::applyEnterDraftFlags(item, wantX);
    logDraftEnterDone(item, sample, contentOnly);
    m_crop.markShowingFullImage();
}

void ImageView::prepareEnterInstallHost(const QString &path, const QImage &full,
                                        bool unorientedSource)
{
    cancelPathRasterForCrop(path);
    rememberCropEnterSizes(path, full);
    CropSession::maybePutUnorientedHostCache(
        path, full, unorientedSource, sampleCoversNativeLogical(path, full));
}


void ImageView::installEnterSampleDisplay(ImageItem *item,
                                          const CropSession::EnterInstallSample &sample,
                                          const QImage &full, const QString &path)
{
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

void ImageView::installFullImageForCrop(ImageItem *item, const QImage &full,
                                        const WorkspaceItemState *app, bool haveApp,
                                        bool unorientedSource)
{
    if (!item || full.isNull()) {
        return;
    }
    const QString path = item->path();
    prepareEnterInstallHost(path, full, unorientedSource);
    const CropSession::EnterInstallSample sample =
        CropSession::prepareEnterInstallSample(full, unorientedSource, app, haveApp);
    installEnterSampleDisplay(item, sample, full, path);
}

void ImageView::activateCropModeAfterInstall(ImageItem *item)
{
    // Crop chrome + fitItem only after pixels and contentRect match the draft.
    m_crop.activateModeAfterDraft();
    fitImageOrUpdateWorkspace(item);
    m_crop.clearAwaitingFull();
}



QRectF ImageView::captureItemContentSceneRect(ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Workspace: lock scene footprint before intrinsic changes on install.
    return item->mapRectToScene(item->contentRect());
}

void ImageView::installAndActivateCropEnter(ImageItem *item, const QImage &full,
                                            const WorkspaceItemState *app, bool haveApp,
                                            bool unorientedSource)
{
    const QRectF beforeScene = captureItemContentSceneRect(item);
    installFullImageForCrop(item, full, app, haveApp, unorientedSource);
    preserveWorkspaceItemCenter(item, beforeScene.center(),
                                beforeScene.width(), beforeScene.height());
    m_crop.initRectFromPriorAppearance(item->contentRect(), item->offset(),
                                       item->imageSize(), app, haveApp);
    activateCropModeAfterInstall(item);
}

bool ImageView::pickEnterFullRasterOrRequest(ImageItem *item, const QString &path,
                                             bool hadCrop,
                                             CropSession::EnterFullRaster *enter)
{
    if (!enter) {
        return false;
    }
    // Unoriented ImageCache host preferred. Never use item display as the crop
    // base when a prior crop exists — that bake is already cropped.
    *enter = CropSession::pickEnterFullRaster(item, path, hadCrop);
    if (enter->image.isNull()) {
        handleNullEnterFullRaster(path, hadCrop);
        return false;
    }
    return true;
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
    CropSession::EnterFullRaster enter;
    if (!pickEnterFullRasterOrRequest(item, path, hadCrop, &enter)) {
        return false;
    }
    installAndActivateCropEnter(item, enter.image, haveApp ? &app : nullptr, haveApp,
                                enter.unoriented);
    return true;
}


void ImageView::onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                            quint64 gen)
{
    if (gen != m_loadGate.generation()) {
        return;
    }
    if (!decoded.isNull()) {
        ImageCache::put(path, decoded);
    }
    maybeUpgradeCropFullRaster(path, decoded);
}

void ImageView::scheduleCropFullRasterFromPool(const QString &path)
{
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
                    host->onPoolCropFullRasterDecoded(path, decoded, gen);
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageView::requestCropFullRaster(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Soft/PreferCache must not race Full encode for the crop subject.
    cancelPathRasterForCrop(path);
    // Prefer thumtoo scheduleFullPixels; fall back to pool ImageLoader::load.
    if (ThumtooCache::isAvailable()) {
        const int edge = CropSession::fullRasterScheduleEdge(path);
        if (ThumtooCache::scheduleFullPixels(path, edge)) {
            return;
        }
        if (ThumtooCache::isPixelsPending(path, edge)) {
            return;
        }
    }
    scheduleCropFullRasterFromPool(path);
}


void ImageView::acceptCropFullRasterReady(const QString &path, const QImage &image)
{
    if (!path.isEmpty() && !image.isNull()) {
        ImageCache::put(path, image);
    }
    m_crop.clearAwaitingFull();
    const CropFlash::Hud readyHud = CropFlash::fullReady();
    flashHud(readyHud.title, readyHud.detail);
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
    // Cache for Apply accuracy; do not reinstall mid-draft (stalls interaction).
    acceptCropFullRasterReady(path, image);
}



bool ImageView::loadPathBookAppearance(ImageItem *item, WorkspaceItemState *app) const
{
    if (!item || !app) {
        return false;
    }
    if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
        *app = *st;
        return true;
    }
    return false;
}

bool ImageView::loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                          SessionImageId *sidOut) const
{
    if (!item || !app) {
        return false;
    }
    const SessionImageId sid = CropSession::resolveSessionIdForItem(
        item, m_sessionId.currentIdValue());
    if (sidOut) {
        *sidOut = sid;
    }
    if (loadSessionAppearance(sid, app)) {
        return true;
    }
    if (CropSession::fillAppearanceFromItemSessionCrop(app, item)) {
        return true;
    }
    return loadPathBookAppearance(item, app);
}



void ImageView::rematerializeIfContentXformMismatch(ImageItem *item,
                                                    const WorkspaceItemState &app)
{
    if (!item) {
        return;
    }
    if (!ContentXform::equal(
            item->hasAppliedContentXform() ? item->appliedContentXform()
                                           : ContentXform::Value{},
            ContentXform::Value::fromState(app))) {
        rematerializeItemContent(item, app);
    }
}

void ImageView::installRestoredCropPixelsFromFull(ImageItem *item, const WorkspaceItemState &app,
                                                  SessionImageId sid, const QImage &full)
{
    if (!tryRematerializeFromHost(item, app)) {
        installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        rematerializeIfContentXformMismatch(item, app);
    }
}

void ImageView::installRestoredCropPixels(ImageItem *item, const WorkspaceItemState &app,
                                          SessionImageId sid, const QImage &full)
{
    if (!item) {
        return;
    }
    CropSession::applyItemPlacementFromState(item, app, isImageMode());
    if (full.isNull()) {
        rematerializeItemContent(item, app);
        return;
    }
    installRestoredCropPixelsFromFull(item, app, sid, full);
}

void ImageView::restoreSessionCropAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState app;
    SessionImageId sid = kInvalidSessionImageId;
    if (!loadRestoreCropAppearance(item, &app, &sid)) {
        return;
    }
    const QString path = item->path();
    const QImage full = fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    installRestoredCropPixels(item, app, sid, full);
    fitImageOrUpdateWorkspace(item);
}

void ImageView::toggleCropMode()
{
    setCropMode(!m_crop.active());
}


void ImageView::storeAppearanceFromState(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    WorkspaceItemState slot = state;
    slot.sessionId = sid;
    slot.path = item->path();
    storeCropAppearance(item, sid, slot);
}

void ImageView::relayoutAfterAppearanceApply(ImageItem *item)
{
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::applyCropAppearancePixels(ImageItem *item, const QImage &src,
                                          const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    if (!src.isNull()) {
        attachDisplaySample(item, src, state, SessionAppearance::PixelKind::FullSource);
        return;
    }
    item->setSessionCrop(state.hasCrop, state.cropRect);
    item->setContentHFlip(state.contentHFlip);
    item->setContentVFlip(state.contentVFlip);
    item->setAppliedContentXform(ContentXform::Value::fromState(state));
}


void ImageView::clearIdentityContentAppearance(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Undo back to identity: commit no longer writes identity (avoids wiping
    // good rows on noisy commits), so clear durable state explicitly.
    if (!SessionAppearance::hasContentAppearance(state)) {
        ThumtooCache::clearContentAppearance(item->path());
    }
}

void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Undo/redo after-image: pixels are already content-baked — attach only.
    applyCropAppearancePixels(item, src, state);
    applyState(item, state);
    // Seed appearance with the full state (including cropRotation) before
    // commitItemSessionEdit, which rebuilds the slot via captureState.
    storeAppearanceFromState(item, state);
    // Appearance persistence is commitItemSessionEdit → m_appearance (by id).
    // Do not write crop state into the path map for bound tiles.
    commitItemSessionEdit(item);
    clearIdentityContentAppearance(item, state);
    relayoutAfterAppearanceApply(item);
}

void ImageView::requestCropViewportUpdate()
{
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::notifyCropViewportStatus()
{
    requestCropViewportUpdate();
    emit statusChanged();
}

QImage ImageView::pickAutoCropSourcePixels(ImageItem *item) const
{
    return CropSession::pickAutoCropSourcePixels(item);
}

bool ImageView::runPaddedAutoTrim(ImageItem *item, const QImage &src)
{
    if (!item || src.isNull()) {
        return false;
    }
    ensureCropRectValid();
    return m_crop.tryPaddedAutoTrim(item->contentRect(), src);
}

void ImageView::applyAutoCrop()
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.active()) {
        return;
    }
    const QImage src = pickAutoCropSourcePixels(item);
    if (src.isNull()) {
        return;
    }
    if (!runPaddedAutoTrim(item, src)) {
        return;
    }
    notifyCropViewportStatus();
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

SessionImageId ImageView::cropRecordSessionId(const ImageItem *item) const
{
    return CropSession::sessionIdForRecord(
        item,
        m_crop.hasTargetId() ? m_crop.targetIdValue() : kInvalidSessionImageId,
        isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
}


void ImageView::logRecordCropDebug(const QSize &cropBasis, const QSize &imageSize,
                                   const QRect &disp) const
{
    CropDebug::recordCrop(cropBasis, imageSize, disp);
}

void ImageView::writeRecordedCropState(ImageItem *item, SessionImageId sid,
                                       const WorkspaceItemState *orientApp,
                                       const CropSession::RecordGeometry &rec,
                                       const QSize &cropBasis, const QRect &disp)
{
    WorkspaceItemState s = captureState(item);
    CropSession::mergeOrientFromAppearance(&s, orientApp);
    s.sessionId = sid;
    s.sessionIndex = item->sessionIndex();
    m_crop.applyRecordToState(&s, rec, cropBasis);
    logRecordCropDebug(cropBasis, item->imageSize(), disp);
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    storeCropAppearance(item, sid, s);
}


bool ImageView::computeSessionCropRecord(ImageItem *item, const QRectF &localCrop,
                                         CropSession::RecordGeometry *rec) const
{
    if (!item || !rec) {
        return false;
    }
    // Crop mode always edits the full on-disk image — store absolute source rect.
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    *rec = m_crop.computeRecordGeometry(
        localCrop, item->contentRect(), item->offset(),
        item->imageSize().width(), item->imageSize().height(),
        item->itemHFlip(), item->itemVFlip());
    return rec->valid();
}

void ImageView::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    if (!item) {
        return;
    }
    CropSession::RecordGeometry rec;
    if (!computeSessionCropRecord(item, localCrop, &rec)) {
        return;
    }
    const SessionImageId sid = cropRecordSessionId(item);
    const WorkspaceItemState *orientApp =
        (sid != kInvalidSessionImageId) ? m_appearance.get(sid) : nullptr;
    const QSize cropBasis = CropSession::cropBasisSize(
        item->imageSize(), cropRecordFileNative(item->path()), orientApp, item);
    writeRecordedCropState(item, sid, orientApp, rec, cropBasis, rec.sourceRect);
}


WorkspaceItemState ImageView::captureCropUndoAfterState(ImageItem *item) const
{
    WorkspaceItemState afterSt = captureState(item);
    CropSession::fillSessionCropFromItem(&afterSt, item);
    // captureState pulls cropRotation from appearance
    // (recordSessionCrop + commitItemSessionEdit).
    return afterSt;
}

void ImageView::pushCropAppearanceUndo(ImageItem *item, const QString &text)
{
    if (!m_undoStack || !item || !m_crop.isEnterValid()) {
        return;
    }
    m_undoStack->push(new CropAppearanceCommand(
        this, item, m_crop.enterSourceRef(), item->sourceImage().copy(),
        m_crop.enterStateRef(), captureCropUndoAfterState(item), text));
}


void ImageView::installApplyDisplayGeometry(ImageItem *item, const WorkspaceItemState &st,
                                            qreal cropW, qreal cropH, const QString &path)
{
    // Clear first so the crop bake replaces full-frame pixels — otherwise canvas
    // stretches full into the crop box and filmstrip gets img=full.
    item->clearDecodedPixels();
    // Geometry before pixels: empty item with crop intrinsic, then bake.
    applyContentLayoutSize(item, st);
    CropSession::ensureApplyIntrinsicSize(item, cropW, cropH, path);
}

void ImageView::attachCropApplyDisplay(ImageItem *item, const QImage &display,
                                          const WorkspaceItemState &st, bool multiMp,
                                          qreal cropW, qreal cropH, const QString &path,
                                          const QPointF &cropSceneCenter)
{
    installApplyDisplayGeometry(item, st, cropW, cropH, path);
    attachDisplaySample(item, display, st, CropSession::applyPixelKind(multiMp));
    m_crop.restoreEnterScale(item);
    alignItemCenterToScene(item, cropSceneCenter);
}


QImage ImageView::pickCropApplyAppearanceImage(ImageItem *item,
                                               const QImage &preferredDisplay) const
{
    // Prefer the crop bake just materialized when provided — not displayImage(),
    // which can still be pre-crop if soft attach was rejected.
    if (!preferredDisplay.isNull()) {
        return preferredDisplay;
    }
    if (item) {
        return sessionAppearanceImage(item);
    }
    return {};
}

void ImageView::emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                           ImageItem *item, const QImage &preferredDisplay,
                                           bool hasCrop)
{
    if (sid == kInvalidSessionImageId) {
        return;
    }
    const QImage appearance = pickCropApplyAppearanceImage(item, preferredDisplay);
    if (appearance.isNull()) {
        return;
    }
    if (hasCrop) {
        emit sessionAppearanceChanged(sid, path, appearance);
    }
    emit sessionCropApplied(sid, path, appearance, hasCrop);
}


void ImageView::relayoutAfterCropLeave(ImageItem *item)
{
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    } else {
        fitImageOrUpdateWorkspace(item);
    }
}

void ImageView::finishCropResetLayout(ImageItem *item)
{
    if (isWorkspaceMode() && m_crop.isEnterValid()) {
        // Drop the enter-time crop-frame offset; restore pre-crop pose.
        m_crop.restoreEnterPlacementPose(item);
    }
    relayoutAfterCropLeave(item);
}

void ImageView::finishCropApplyLayout(ImageItem *item)
{
    if (!item) {
        return;
    }
    if (isWorkspaceMode()) {
        m_crop.applyCommitPlacementRotation(item);
    }
    relayoutAfterCropLeave(item);
}


void ImageView::flashApplyHostStatusHud(CropSession::ApplyHostStatus hostSt)
{
    const CropFlash::Hud hud = CropFlash::applyHostStatus(hostSt);
    flashHud(hud.title, hud.detail);
}

bool ImageView::flashApplyHostFailure(CropSession::ApplyHostStatus hostSt)
{
    if (hostSt == CropSession::ApplyHostStatus::Ok) {
        return false;
    }
    flashApplyHostStatusHud(hostSt);
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


void ImageView::commitCropApplyBake(ImageItem *item, const QImage &display,
                                    const WorkspaceItemState &st, bool multiMp,
                                    qreal cropW, qreal cropH, const QString &path,
                                    const QPointF &cropSceneCenter, bool hostFromCache,
                                    SessionImageId sid)
{
    {
        ViewportUpdateHold paintHold(viewport());
        attachCropApplyDisplay(item, display, st, multiMp, cropW, cropH, path,
                               cropSceneCenter);
        // Multi-MP: soft stand-in now; pure full rematerialize after leave.
        m_crop.queueFullRematerializeIfSoft(hostFromCache, multiMp, path, sid, st);
        finishCropApplyLayout(item);
    }
    finalizeCropApplySuccess(item, sid, path, display);
}


void ImageView::logApplyCropDebug(ImageItem *item, const QString &path, const QImage &host,
                                  bool hostFromCache, const QImage &display,
                                  qreal cropW, qreal cropH, qreal footW, qreal footH) const
{
    if (!item) {
        return;
    }
    CropDebug::applyCrop(path, host.width(), host.height(), hostFromCache,
                         display.width(), display.height(), cropW, cropH, footW, footH,
                         item->imageSize().width(), item->imageSize().height());
}



bool ImageView::bakeAndCommitNonFullApply(ImageItem *item, qreal cropW, qreal cropH,
                                          qreal footW, qreal footH,
                                          const QPointF &cropSceneCenter)
{
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
    logApplyCropDebug(item, path, host, hostFromCache, baked.display, cropW, cropH,
                      footW, footH);
    commitCropApplyBake(item, baked.display, st, baked.multiMp, cropW, cropH, path,
                        cropSceneCenter, hostFromCache, sid);
    return true;
}

bool ImageView::applyCropCommitNonFullFrame(ImageItem *item)
{
    // Workspace footprint: draft selection scene size stays constant after Apply
    // (intrinsic becomes cropW×cropH at the same placement scale).
    qreal cropW = 0.0;
    qreal cropH = 0.0;
    qreal footW = 0.0;
    qreal footH = 0.0;
    QPointF cropSceneCenter;
    captureApplyDraftMetrics(item, &cropW, &cropH, &footW, &footH, &cropSceneCenter);
    if (!bakeAndCommitNonFullApply(item, cropW, cropH, footW, footH, cropSceneCenter)) {
        return false;
    }
    return isWorkspaceMode();
}


bool ImageView::applyCropCommitFullFrame(ImageItem *item)
{
    // Reset / full frame: keep full pixels; clear session crop metadata.
    finishCropResetLayout(item);
    finalizeCropResetSuccess(item);
    return false;
}

bool ImageView::applyCropCommit(ImageItem *item)
{
    // Returns true when Workspace placement rotation should keep the crop-frame
    // angle (non-full-frame commit).
    ensureCropRectValid();
    const QRectF full = item->contentRect();
    recordSessionCrop(item, m_crop.draftRectOr(full));
    if (!m_crop.isFullFrameDraft(full)) {
        return applyCropCommitNonFullFrame(item);
    }
    return applyCropCommitFullFrame(item);
}

void ImageView::restoreEnterPlacementIfWorkspace(ImageItem *item)
{
    if (isWorkspaceMode() && m_crop.isEnterValid()) {
        m_crop.restoreEnterPlacementPose(item);
    }
}

void ImageView::cancelCropShowingFullImage(ImageItem *item)
{
    restoreSessionCropAppearance(item);
    restoreEnterPlacementIfWorkspace(item);
}


void ImageView::flushPendingFullRematerialize(bool pendingFull, const QString &pendingPath,
                                              SessionImageId pendingSid,
                                              const WorkspaceItemState &pendingWant)
{
    // Apply may have queued a full bake while freeze was still on.
    if (pendingFull && !pendingPath.isEmpty()) {
        scheduleAsyncHostRematerialize(pendingPath, pendingSid, pendingWant);
    }
}


void ImageView::notifyCropModeLeftChrome()
{
    emit cropModeChanged(false);
    emit statusChanged();
    if (viewport()) {
        viewport()->unsetCursor();
        viewport()->update();
    }
}

void ImageView::clearCropModeState()
{
    // Stop PreferCache before releasing tile LOD / clearing draft identity.
    QString subjectPath = m_crop.draftPathRef();
    if (subjectPath.isEmpty()) {
        if (ImageItem *bound = cropSessionBoundItem()) {
            subjectPath = bound->path();
        }
    }
    cancelPathRasterForCrop(subjectPath);
    m_crop.releaseAllTileLod(cropSessionBoundItem());
    QString pendingPath;
    SessionImageId pendingSid = kInvalidSessionImageId;
    WorkspaceItemState pendingWant;
    const bool pendingFull =
        m_crop.takePendingFullRematerialize(&pendingPath, &pendingSid, &pendingWant);
    m_crop.clear();
    notifyCropModeLeftChrome();
    flushPendingFullRematerialize(pendingFull, pendingPath, pendingSid, pendingWant);
}


bool ImageView::finalizeCropLeaveItem(ImageItem *item, bool apply)
{
    if (!item) {
        return false;
    }
    if (apply) {
        return applyCropCommit(item);
    }
    if (m_crop.isShowingFullImage()) {
        cancelCropShowingFullImage(item);
    }
    return false;
}

void ImageView::leaveCropModeInternal(bool apply)
{
    if (!m_crop.active()) {
        return;
    }
    ImageItem *item = cropTargetItem();
    // Workspace commit keeps crop-frame placement rotation; cancel/full-frame
    // restore the pre-crop pose via finishLeave.
    const bool preserveCropFrameRotation = finalizeCropLeaveItem(item, apply);
    m_crop.finishLeave(item, preserveCropFrameRotation);
    clearCropModeState();
}


QPolygonF ImageView::mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const
{
    if (!item) {
        return {};
    }
    QPolygonF viewPoly;
    viewPoly.reserve(local.size());
    for (const QPointF &pt : local) {
        viewPoly << QPointF(mapFromScene(item->mapToScene(pt)));
    }
    return viewPoly;
}

QPolygonF ImageView::cropPolygonView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.hasValidRect()) {
        return {};
    }
    return mapItemLocalPolygonToView(item, m_crop.polygonLocal());
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




void ImageView::paintCropChromeButton(QPainter &painter, const QRect &btn, CropHandle kind,
                                      const QString &label, CropGeometry::CropBtnRole role,
                                      bool toggled)
{
    CropGeometry::paintTextButton(painter, btn, m_crop.currentHoverHandle() == kind, label,
                                  role, toggled);
}

QString ImageView::cropChromeButtonLabel(CropHandle kind)
{
    switch (kind) {
    case CropHandle::ExpandToggle:
        return tr("Expand");
    case CropHandle::Auto:
        return tr("Auto");
    case CropHandle::Reset:
        return tr("Reset");
    case CropHandle::Cancel:
        return tr("Cancel");
    case CropHandle::Close:
        return tr("Apply");
    default:
        return {};
    }
}

void ImageView::paintCropChromeButtons(QPainter &painter)
{
    // Controls: outside below crop when possible, inside if off-screen.
    // Same design language as Workspace chrome (HANDLES.md).
    const CropGeometry::CropButtonLayout chrome = cropChromeLayout();
    for (const CropGeometry::ChromePaintItem &item :
         CropGeometry::chromePaintItems(chrome, m_crop.isAllowExpand())) {
        if (item.rect.isEmpty()) {
            continue;
        }
        paintCropChromeButton(painter, item.rect, item.handle,
                              cropChromeButtonLabel(item.handle), item.role,
                              item.toggled);
    }
}



void ImageView::paintCropRotateAndMoveGrips(QPainter &painter, const QPolygonF &cropViewPoly)
{
    const bool rotateHot = (m_crop.currentHoverHandle() == CropHandle::Rotate
                            || m_crop.currentActiveHandle() == CropHandle::Rotate);
    CropGeometry::paintRotateKnobs(painter, cropViewPoly, rotateHot);
    const bool moveHot = (m_crop.currentHoverHandle() == CropHandle::Move
                          || m_crop.currentActiveHandle() == CropHandle::Move);
    CropGeometry::paintMoveGrip(painter, cropViewPoly, moveHot);
}

void ImageView::paintCropFrameDecorations(QPainter &painter, const QPolygonF &cropViewPoly)
{
    if (viewport()) {
        CropGeometry::paintDimOutside(painter, viewport()->rect(), cropViewPoly);
    }
    CropGeometry::paintFrame(painter, cropViewPoly);
    CropGeometry::paintResizeHandles(painter, cropViewPoly,
        [this](CropHandle h) { return m_crop.isHandleHot(h); });
    paintCropRotateAndMoveGrips(painter, cropViewPoly);
}

void ImageView::paintCropSizeBadge(QPainter &painter, const QRect &cropView)
{
    const QSize cropSz = m_crop.draftPixelSize();
    CropGeometry::paintSizeBadge(painter, cropView, cropSz.width(), cropSz.height());
}


void ImageView::paintCropOverlayBody(QPainter &painter, const QPolygonF &cropViewPoly,
                                     const QRect &cropView)
{
    paintCropFrameDecorations(painter, cropViewPoly);
    paintCropChromeButtons(painter);
    paintCropSizeBadge(painter, cropView);
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
    paintCropOverlayBody(painter, cropViewPoly, cropView);
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


void ImageView::cropKeyboardMods(bool *shiftHeld, bool *ctrlHeld)
{
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    if (shiftHeld) {
        *shiftHeld = mods & Qt::ShiftModifier;
    }
    if (ctrlHeld) {
        *ctrlHeld = mods & Qt::ControlModifier;
    }
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.isHandleDragging()) {
        return;
    }
    bool shiftHeld = false;
    bool ctrlHeld = false;
    cropKeyboardMods(&shiftHeld, &ctrlHeld);
    m_crop.applyActiveHandleDrag(itemLocalFromView(item, viewPos), item->contentRect(),
                                 CropSession::kMinDraftSidePx, shiftHeld, ctrlHeld);
    requestCropViewportUpdate();
}

void ImageView::endCropHandleDrag()
{
    ImageItem *item = cropTargetItem();
    m_crop.finishHandleDrag(item ? item->contentRect() : QRectF());
    requestCropViewportUpdate();
}

bool ImageView::contentLocalContains(ImageItem *item, const QPointF &local) const
{
    return item && item->contentRect().contains(local);
}

void ImageView::beginCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QPointF local = itemLocalFromView(item, viewPos);
    if (!contentLocalContains(item, local)) {
        return;
    }
    m_crop.beginRubberDraft(local);
    requestCropViewportUpdate();
}

void ImageView::updateCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.isRubberbanding()) {
        return;
    }
    const QRectF cr = item->contentRect();
    bool shiftHeld = false;
    bool ctrlHeld = false;
    cropKeyboardMods(&shiftHeld, &ctrlHeld);
    m_crop.applyRubberBand(itemLocalFromView(item, viewPos), cr, shiftHeld, ctrlHeld);
    requestCropViewportUpdate();
}

void ImageView::finishCropRubberBand()
{
    if (ImageItem *item = cropTargetItem()) {
        m_crop.finishRubber(item->contentRect());
    } else {
        m_crop.endRubber();
    }
}

void ImageView::endCropRubberBand()
{
    finishCropRubberBand();
    requestCropViewportUpdate();
}

CropHandle ImageView::cropHandleAt(const QPoint &viewPos) const
{
    if (!m_crop.active() || !m_crop.hasValidRect() || !cropTargetItem()) {
        return CropHandle::None;
    }
    const CropGeometry::CropFrameViewAnchors anchors =
        CropGeometry::frameViewAnchors(cropPolygonView());
    return CropGeometry::hitTestCropChrome(viewPos, cropChromeLayout(), anchors);
}

