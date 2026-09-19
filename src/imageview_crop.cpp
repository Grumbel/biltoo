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
    // Prefer thumtoo Full; fall back to pool ImageLoader::load.
    if (CropSession::tryScheduleThumtooFullRaster(path)) {
        return;
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


void ImageView::toggleCropMode()
{
    setCropMode(!m_crop.active());
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
