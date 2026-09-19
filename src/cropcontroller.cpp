// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cropcontroller.h"

#include "imageview.h"
#include "imageitem.h"
#include "croppathraster.h"
#include "cropflash.h"
#include "placementlinear.h"
#include "imageitem.h"
#include "cropappearancecommand.h"
#include "viewportupdatehold.h"
#include "cropdebug.h"
#include "sessionappearance.h"
#include <QUndoStack>
#include "cropgeometry.h"
#include "imagecache.h"
#include "contentxform.h"
#include <QGuiApplication>
#include <QPainter>
#include "imageloader.h"
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>

#include <QPainter>
#include <QGuiApplication>
#include <QtMath>

CropController::CropController(ImageView *view)
    : m_view(view)
{
}

// --- from src/imageview_crop.cpp ---

// Crop targets, draft locks, PathRaster cancel, align/leave layout, auto-trim.
// Enter → crop_enter; Apply → crop_apply; Full → crop_raster;
// paint → crop_paint; input → crop_input.


ImageItem *CropController::cropSessionBoundItem() const
{
    // Bound subject for the active crop session (IDENTITY.md).
    if (session().target()) {
        return session().target();
    }
    if (session().hasTargetId()) {
        return m_view->findItemBySessionId(session().targetIdValue());
    }
    return nullptr;
}


ImageItem *CropController::cropTargetItem() const
{
    // Crop session is bound to one subject for its entire lifetime. Never
    // retarget from selection while the draft is active.
    if (session().active() || session().isEnterValid()) {
        if (ImageItem *bound = cropSessionBoundItem()) {
            return bound;
        }
    }
    if (ImageItem *t = m_view->targetItem()) {
        // Soft ladder tiles have preview/source with m_previewPixels; pixmap is
        // often empty (paint uses m_source). hasDecodedPixels() alone was
        // "Crop / No image" for every soft-only Workspace/Gallery selection.
        if (t->hasDisplayPixels()) {
            return t;
        }
    }
    // Image mode only: sole canvas item. Gallery/Workspace need an explicit
    // single selection (hasSingleCropTarget) — never primaryItem() fan-out.
    if (m_view->isImageMode()) {
        return primaryItem();
    }
    return nullptr;
}



bool CropController::isCropDraftLockedItem(const ImageItem *item) const
{
    if (!item) {
        return false;
    }
    // Pointer/id lock on CropSession, then path lock (draftPath / targetId resolve).
    return session().locksItem(item) || isCropDraftLockedPath(item->path());
}

bool CropController::isCropDraftLockedPath(const QString &path) const
{
    QString boundPath;
    if (ImageItem *bound = cropSessionBoundItem()) {
        boundPath = bound->path();
    }
    return session().locksResolvedPath(path, boundPath);
}

void CropController::cancelPathRasterForCrop(const QString &path)
{
    CropPathRaster::suspend(m_view->pathRasterForCoordinator(), path);
}

void CropController::fitImageOrUpdateWorkspace(ImageItem *item)
{
    if (!item) {
        return;
    }
    if (m_view->isImageMode()) {
        m_view->hostFraming().armFit();
        m_view->fitItem(item, currentFitAspectMode());
    } else if (m_view->isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}

void CropController::relayoutAfterCropLeave(ImageItem *item)
{
    if (m_view->isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    } else {
        fitImageOrUpdateWorkspace(item);
    }
}

void CropController::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    session().ensureRectValid(item->contentRect());
}

void CropController::alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
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

void CropController::toggleCropMode()
{
    setCropMode(!session().active());
}


void CropController::requestCropViewportUpdate()
{
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void CropController::applyAutoCrop()
{
    ImageItem *item = cropTargetItem();
    if (!item || !session().active()) {
        return;
    }
    const QImage src = CropSession::pickAutoCropSourcePixels(item);
    if (src.isNull()) {
        return;
    }
    ensureCropRectValid();
    if (!session().tryPaddedAutoTrim(item->contentRect(), src)) {
        return;
    }
    requestCropViewportUpdate();
    emit m_view->statusChanged();
}

void CropController::flashCropHud(const CropFlash::Hud &hud)
{
    flashHud(hud.title, hud.detail);
}

// --- from src/imageview_crop_apply.cpp ---

// Crop Apply / leave commit: bake, record, undo, rematerialize flush.



void CropController::applyCrop()
{
    // Soft draft is valid — crop is content-space. Do not wait on multi-MP load.
    session().clearAwaitingFull();
    leaveCropModeInternal(true);
}

void CropController::cancelCrop()
{
    leaveCropModeInternal(false);
}

SessionImageId CropController::cropRecordSessionId(const ImageItem *item) const
{
    return CropSession::sessionIdForRecord(
        item,
        session().hasTargetId() ? session().targetIdValue() : kInvalidSessionImageId,
        m_view->isImageMode() ? m_view->hostSessionId().currentIdValue() : kInvalidSessionImageId);
}


void CropController::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    if (!item) {
        return;
    }
    // Crop mode always edits the full on-disk image — store absolute source rect.
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    CropSession::RecordGeometry rec = session().computeRecordGeometry(
        localCrop, item->contentRect(), item->offset(),
        item->imageSize().width(), item->imageSize().height(),
        item->itemHFlip(), item->itemVFlip());
    if (!rec.valid()) {
        return;
    }
    const SessionImageId sid = cropRecordSessionId(item);
    const WorkspaceItemState *orientApp =
        (sid != kInvalidSessionImageId) ? m_view->hostAppearance().get(sid) : nullptr;
    QSize fileNative = m_view->logicalSizeForPath(item->path());
    if (!isPositiveSize(fileNative) || fileNative.width() <= 1
        || m_view->isProvisionalImageSize(item->path())) {
        fileNative = {};
    }
    const QSize cropBasis = CropSession::cropBasisSize(
        item->imageSize(), fileNative, orientApp, item);
    WorkspaceItemState s = m_view->captureState(item);
    CropSession::mergeOrientFromAppearance(&s, orientApp);
    s.sessionId = sid;
    s.sessionIndex = item->sessionIndex();
    session().applyRecordToState(&s, rec, cropBasis);
    CropDebug::recordCrop(cropBasis, item->imageSize(), rec.sourceRect);
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    m_view->storeCropAppearance(item, sid, s);
}


void CropController::pushCropAppearanceUndo(ImageItem *item, const QString &text)
{
    if (!m_view->undoStack() || !item || !session().isEnterValid()) {
        return;
    }
    WorkspaceItemState afterSt = m_view->captureState(item);
    CropSession::fillSessionCropFromItem(&afterSt, item);
    // captureState pulls cropRotation from appearance
    // (recordSessionCrop + commitItemSessionEdit).
    m_view->undoStack()->push(new CropAppearanceCommand(
        m_view, item, session().enterSourceRef(), item->sourceImage().copy(),
        session().enterStateRef(), afterSt, text));
}


bool CropController::applyCropCommit(ImageItem *item)
{
    // Returns true when Workspace placement rotation should keep the crop-frame
    // angle (non-full-frame commit).
    ensureCropRectValid();
    const QRectF full = item->contentRect();
    recordSessionCrop(item, session().draftRectOr(full));
    if (!session().isFullFrameDraft(full)) {
        // Workspace footprint: draft selection scene size stays constant after Apply
        // (intrinsic becomes cropW×cropH at the same placement scale).
        qreal cropW = 0.0;
        qreal cropH = 0.0;
        qreal footW = 0.0;
        qreal footH = 0.0;
        QPointF cropSceneCenter;
        qreal sx0 = 0.0;
        qreal sy0 = 0.0;
        CropSession::itemScalePair(item, &sx0, &sy0);
        session().draftFootprint(sx0, sy0, &cropW, &cropH, &footW, &footH);
        cropSceneCenter = item->mapToScene(session().draftCenterLocal());

        const QString path = item->path();
        bool hostFromCache = false;
        QImage host = CropSession::pickApplyHost(item, path, &hostFromCache);
        const CropSession::ApplyHostStatus hostSt =
            CropSession::classifyApplyHost(host, hostFromCache, item);
        if (hostSt != CropSession::ApplyHostStatus::Ok) {
            flashCropHud(CropFlash::applyHostStatus(hostSt));
            return false;
        }
        SessionImageId sid = cropRecordSessionId(item);
        WorkspaceItemState st;
        loadSessionAppearance(sid, &st);
        if (!st.hasCrop) {
            st = m_view->captureState(item);
            session().seedApplyCropState(&st, item->offset(), item->imageSize());
            if (sid != kInvalidSessionImageId) {
                m_view->hostAppearance().set(sid, st);
            }
        }
        CropSession::ApplyBakeResult baked =
            CropSession::materializeApplyDisplay(host, hostFromCache, st);
        if (!baked.ok()) {
            flashCropHud(CropFlash::bakeFailed());
            return false;
        }
        CropDebug::applyCrop(path, host.width(), host.height(), hostFromCache,
                             baked.display.width(), baked.display.height(), cropW, cropH, footW,
                             footH, item->imageSize().width(), item->imageSize().height());
        {
            ViewportUpdateHold paintHold(m_view->viewport());
            // Clear first so the crop bake replaces full-frame pixels — otherwise canvas
            // stretches full into the crop box and filmstrip gets img=full.
            item->clearDecodedPixels();
            // Geometry before pixels: empty item with crop intrinsic, then bake.
            applyContentLayoutSize(item, st);
            CropSession::ensureApplyIntrinsicSize(item, cropW, cropH, path);
            attachDisplaySample(item, baked.display, st,
                                CropSession::applyPixelKind(baked.multiMp));
            session().restoreEnterScale(item);
            alignItemCenterToScene(item, cropSceneCenter);
            // Multi-MP: soft stand-in now; pure full rematerialize after leave.
            session().queueFullRematerializeIfSoft(hostFromCache, baked.multiMp, path, sid, st);
            if (m_view->isWorkspaceMode()) {
                session().applyCommitPlacementRotation(item);
            }
            relayoutAfterCropLeave(item);
        }
        m_view->commitItemSessionEdit(item);
        m_view->emitCropApplyAppearance(sid, path, item, baked.display, /*hasCrop=*/true);
        pushCropAppearanceUndo(item, CropFlash::undoCropText());
        flashCropHud(CropFlash::applied(item->imageSize().width(), item->imageSize().height()));
        return m_view->isWorkspaceMode();
    }
    // Reset / full frame: keep full pixels; clear session crop metadata.
    if (m_view->isWorkspaceMode() && session().isEnterValid()) {
        // Drop the enter-time crop-frame offset; restore pre-crop pose.
        session().restoreEnterPlacementPose(item);
    }
    relayoutAfterCropLeave(item);
    m_view->commitItemSessionEdit(item);
    m_view->emitCropApplyAppearance(cropRecordSessionId(item), item->path(), item, QImage(),
                            /*hasCrop=*/false);
    if (session().shouldPushResetUndo(item->sourceImage().size())) {
        pushCropAppearanceUndo(item, CropFlash::undoResetText());
    }
    flashCropHud(CropFlash::reset());
    return false;
}


void CropController::leaveCropModeInternal(bool apply)
{
    if (!session().active()) {
        return;
    }
    ImageItem *item = cropTargetItem();
    // Workspace commit keeps crop-frame placement rotation; cancel/full-frame
    // restore the pre-crop pose via finishLeave.
    bool preserveCropFrameRotation = false;
    if (item) {
        if (apply) {
            preserveCropFrameRotation = applyCropCommit(item);
        } else if (session().isShowingFullImage()) {
            restoreSessionCropAppearance(item);
            if (m_view->isWorkspaceMode() && session().isEnterValid()) {
                session().restoreEnterPlacementPose(item);
            }
        }
    }
    session().finishLeave(item, preserveCropFrameRotation);
    // Stop PreferCache before releasing tile LOD / clearing draft identity.
    QString subjectPath = session().draftPathRef();
    if (subjectPath.isEmpty()) {
        if (ImageItem *bound = cropSessionBoundItem()) {
            subjectPath = bound->path();
        }
    }
    cancelPathRasterForCrop(subjectPath);
    session().releaseAllTileLod(cropSessionBoundItem());
    QString pendingPath;
    SessionImageId pendingSid = kInvalidSessionImageId;
    WorkspaceItemState pendingWant;
    const bool pendingFull =
        session().takePendingFullRematerialize(&pendingPath, &pendingSid, &pendingWant);
    session().clear();
    emit m_view->cropModeChanged(false);
    emit m_view->statusChanged();
    if (m_view->viewport()) {
        m_view->viewport()->unsetCursor();
        m_view->viewport()->update();
    }
    // Apply may have queued a full bake while freeze was still on.
    if (pendingFull && !pendingPath.isEmpty()) {
        scheduleAsyncHostRematerialize(pendingPath, pendingSid, pendingWant);
    }
}

// --- from src/imageview_crop_enter.cpp ---

// Crop enter: bind target, install full-frame draft, activate chrome.
// docs/CROP_MODE.md / IDENTITY.md



bool CropController::enterCropModeFromUi()
{
    // Gallery packing cannot host crop UI — MainWindow opens Image mode instead.
    if (m_view->isGalleryMode()) {
        return false;
    }
    // Image or Workspace: one explicit subject only.
    if (!hasSingleCropTarget()) {
        flashCropHud(CropFlash::needSingleTarget());
        return false;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !item->hasDisplayPixels()) {
        flashCropHud(CropFlash::noImage());
        return false;
    }
    cancelZoomRegion();
    // Lock identity + enter snapshot + unrotate placement (IDENTITY.md).
    // session().active() stays false until after the first draft attach.
    {
        QImage enterSrc = CropSession::pickEnterSnapshotPixels(item);
        WorkspaceItemState enterSt = m_view->captureState(item);
        CropSession::seedEnterCropFlags(&enterSt, item);
        session().beginEnterSession(item, enterSrc, enterSt,
                                 !enterSrc.isNull() || item->hasDisplayPixels());
        cancelPathRasterForCrop(session().draftPathRef());
    }
    // Workspace: displayed image centre so the crop frame can stay fixed.
    const QPointF workspaceAnchorScene = item->mapToScene(QPointF(0.0, 0.0));

    // One paint after full-frame draft is ready (no intermediate crop-on-old-box).
    ViewportUpdateHold paintHold(m_view->viewport());
    if (!prepareCropModeFullImage(item)) {
        // prepare may have set mode for fitItem then failed — restore placement
        // before abortEnter clears the stash.
        item->setTileLodSuppressed(false);
        session().abortEnterRestoringPlacement(item);
        flashCropHud(CropFlash::loadFailed());
        return false;
    }
    if (m_view->isWorkspaceMode()) {
        // If there was no stored crop angle but the tile was free-rotated,
        // seed the draft rotation so the frame matches the prior pose while
        // the item stays axis-aligned for editing.
        if (session().seedRotationFromStashedPlacement(CropGeometry::kFreeRotationEps)) {
            ensureCropRectValid();
        }
        if (session().hasValidRect()) {
            CropSession::applyScenePosDelta(
                item,
                PlacementLinear::scenePosDeltaToAlign(
                    item->mapToScene(session().draftCenterLocal()), workspaceAnchorScene));
        }
        updateWorkspaceSceneRect();
    }
    flashCropHud(CropFlash::modeEntered());
    emit m_view->cropModeChanged(true);
    emit m_view->statusChanged();
    return true;
}

void CropController::setCropMode(bool on)
{
    if (on == session().active()) {
        return;
    }
    if (on) {
        enterCropModeFromUi();
        return;
    }
    // Turning crop off from the toolbar commits the draft (auto-apply).
    leaveCropModeInternal(true);
}
bool CropController::prepareCropModeFullImage(ImageItem *item)
{
    if (!item) {
        return false;
    }
    const QString path = item->path();
    session().clearAwaitingFull();

    WorkspaceItemState app;
    const bool haveApp = m_view->loadRestoreCropAppearance(item, &app, nullptr);
    const bool hadCrop = CropSession::appearanceHasCrop(&app, haveApp);
    // Unoriented ImageCache host preferred. Never use item display as the crop
    // base when a prior crop exists — that bake is already cropped.
    CropSession::EnterFullRaster enter = CropSession::pickEnterFullRaster(item, path, hadCrop);
    if (enter.image.isNull()) {
        if (CropSession::shouldRequestFullOnNullEnter(hadCrop, path)) {
            requestCropFullRaster(path);
            session().setAwaitingFull(path);
            flashCropHud(CropFlash::loadingFull());
        }
        flashCropHud(CropFlash::notCached());
        return false;
    }

    const QImage &full = enter.image;
    const bool unorientedSource = enter.unoriented;
    const WorkspaceItemState *appPtr = haveApp ? &app : nullptr;

    // Workspace: lock scene footprint before intrinsic changes on install.
    const QRectF beforeScene = item->mapRectToScene(item->contentRect());

    // --- install full-frame draft pixels ---
    cancelPathRasterForCrop(path);
    if (!full.isNull() && m_view->sampleCoversNativeLogical(path, full)) {
        m_view->rememberSizeFromDecode(path, full);
        QSize logical = m_view->logicalSizeForPath(path);
        if (!isPositiveSize(logical) || m_view->isProvisionalImageSize(path)) {
            m_view->rememberImageSize(path, full.size());
        }
    }
    CropSession::maybePutUnorientedHostCache(
        path, full, unorientedSource, m_view->sampleCoversNativeLogical(path, full));
    const CropSession::EnterInstallSample sample =
        CropSession::prepareEnterInstallSample(full, unorientedSource, appPtr, haveApp);

    const WorkspaceItemState &contentOnly = sample.contentOnly;
    const ContentXform::Value &wantX = sample.wantX;
    // Full-frame already on the item (no crop bake): keep those pixels.
    // Do not rebuild a lower-res graded stand-in — that invites soft↔full thrash.
    if (CropSession::canKeepDisplayForEnter(item, wantX, contentOnly, sample.hadPriorCrop,
                                            sample.needGeomBake)) {
        CropSession::applyKeepEnterFlags(item, contentOnly, wantX);
        applyContentLayoutSize(item, contentOnly);
        session().markShowingFullImage();
        CropDebug::keepEnterDisplay(item->displayPixelLongEdge(), path);
    } else {
        CropSession::clearItemFreePlacementForDraft(item);
        CropDebug::draftEnterBegin(path, item->imageSize().width(), item->imageSize().height(),
                                   item->hasDecodedPixels(), sample.hadPriorCrop,
                                   ImageCache::longEdge(full));
        CropSession::clearItemPixelsForDraftReinstall(item);
        attachDisplaySample(item, sample.display, contentOnly, sample.kind);
        applyContentLayoutSize(item, contentOnly);
        CropSession::applyEnterDraftFlags(item, wantX);
        CropDebug::draftEnterDone(item->imageSize().width(), item->imageSize().height(),
                                  sample.display.width(), sample.display.height(),
                                  item->sessionHasCrop(), contentOnly.contentQuarterTurns);
        session().markShowingFullImage();
    }

    if (m_view->isWorkspaceMode() && beforeScene.width() > 1.0 && beforeScene.height() > 1.0) {
        alignItemCenterToScene(item, beforeScene.center());
    }
    session().initRectFromPriorAppearance(item->contentRect(), item->offset(),
                                       item->imageSize(), appPtr, haveApp);
    // Crop chrome + fitItem only after pixels and contentRect match the draft.
    session().activateModeAfterDraft();
    fitImageOrUpdateWorkspace(item);
    session().clearAwaitingFull();
    return true;
}

// --- from src/imageview_crop_input.cpp ---

// Crop input: view mapping, handle drag, rubber-band, chrome hit-test.



QPolygonF CropController::mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const
{
    if (!item) {
        return {};
    }
    QPolygonF viewPoly;
    viewPoly.reserve(local.size());
    for (const QPointF &pt : local) {
        viewPoly << QPointF(m_view->mapFromScene(item->mapToScene(pt)));
    }
    return viewPoly;
}

QPolygonF CropController::cropPolygonView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !session().hasValidRect()) {
        return {};
    }
    return mapItemLocalPolygonToView(item, session().polygonLocal());
}

QRectF CropController::cropRectView() const
{
    return cropPolygonView().boundingRect().normalized();
}

CropGeometry::CropButtonLayout CropController::cropChromeLayout() const
{
    if (!session().active()) {
        return {};
    }
    return CropGeometry::cropButtonLayout(cropRectView(),
                                          m_view->viewport() ? m_view->viewport()->rect() : QRect());
}




QPointF CropController::itemLocalFromView(ImageItem *item, const QPoint &viewPos) const
{
    if (!item) {
        return {};
    }
    return item->mapFromScene(m_view->mapToScene(viewPos));
}

void CropController::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !CropSession::isGeometryHandle(h)) {
        return;
    }
    session().beginHandleDrag(h, session().currentRect(), itemLocalFromView(item, viewPos));
}


void CropController::cropKeyboardMods(bool *shiftHeld, bool *ctrlHeld)
{
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    if (shiftHeld) {
        *shiftHeld = mods & Qt::ShiftModifier;
    }
    if (ctrlHeld) {
        *ctrlHeld = mods & Qt::ControlModifier;
    }
}

void CropController::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !session().isHandleDragging()) {
        return;
    }
    bool shiftHeld = false;
    bool ctrlHeld = false;
    cropKeyboardMods(&shiftHeld, &ctrlHeld);
    session().applyActiveHandleDrag(itemLocalFromView(item, viewPos), item->contentRect(),
                                 CropSession::kMinDraftSidePx, shiftHeld, ctrlHeld);
    requestCropViewportUpdate();
}

void CropController::endCropHandleDrag()
{
    ImageItem *item = cropTargetItem();
    session().finishHandleDrag(item ? item->contentRect() : QRectF());
    requestCropViewportUpdate();
}

bool CropController::contentLocalContains(ImageItem *item, const QPointF &local) const
{
    return item && item->contentRect().contains(local);
}

void CropController::beginCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QPointF local = itemLocalFromView(item, viewPos);
    if (!contentLocalContains(item, local)) {
        return;
    }
    session().beginRubberDraft(local);
    requestCropViewportUpdate();
}

void CropController::updateCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !session().isRubberbanding()) {
        return;
    }
    const QRectF cr = item->contentRect();
    bool shiftHeld = false;
    bool ctrlHeld = false;
    cropKeyboardMods(&shiftHeld, &ctrlHeld);
    session().applyRubberBand(itemLocalFromView(item, viewPos), cr, shiftHeld, ctrlHeld);
    requestCropViewportUpdate();
}

void CropController::finishCropRubberBand()
{
    if (ImageItem *item = cropTargetItem()) {
        session().finishRubber(item->contentRect());
    } else {
        session().endRubber();
    }
}

void CropController::endCropRubberBand()
{
    finishCropRubberBand();
    requestCropViewportUpdate();
}

CropHandle CropController::cropHandleAt(const QPoint &viewPos) const
{
    if (!session().active() || !session().hasValidRect() || !cropTargetItem()) {
        return CropHandle::None;
    }
    const CropGeometry::CropFrameViewAnchors anchors =
        CropGeometry::frameViewAnchors(cropPolygonView());
    return CropGeometry::hitTestCropChrome(viewPos, cropChromeLayout(), anchors);
}

// --- from src/imageview_crop_paint.cpp ---

// Crop chrome / overlay paint (HANDLES.md). Geometry in CropGeometry.



void CropController::paintCropOverlay(QPainter &painter)
{
    if (!session().active()) {
        return;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !session().hasValidRect()) {
        return;
    }
    ensureCropRectValid();
    const QPolygonF cropViewPoly = cropPolygonView();
    const QRect cropView = cropViewPoly.boundingRect().toRect().normalized();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_view->viewport()) {
        CropGeometry::paintDimOutside(painter, m_view->viewport()->rect(), cropViewPoly);
    }
    CropGeometry::paintFrame(painter, cropViewPoly);
    CropGeometry::paintResizeHandles(painter, cropViewPoly,
        [this](CropHandle h) { return session().isHandleHot(h); });

    const bool rotateHot = (session().currentHoverHandle() == CropHandle::Rotate
                            || session().currentActiveHandle() == CropHandle::Rotate);
    CropGeometry::paintRotateKnobs(painter, cropViewPoly, rotateHot);
    const bool moveHot = (session().currentHoverHandle() == CropHandle::Move
                          || session().currentActiveHandle() == CropHandle::Move);
    CropGeometry::paintMoveGrip(painter, cropViewPoly, moveHot);

    // Controls: outside below crop when possible, inside if off-screen.
    // Same design language as Workspace chrome (HANDLES.md).
    const CropGeometry::CropButtonLayout chrome = cropChromeLayout();
    for (const CropGeometry::ChromePaintItem &chromeItem :
         CropGeometry::chromePaintItems(chrome, session().isAllowExpand())) {
        if (chromeItem.rect.isEmpty()) {
            continue;
        }
        QString label;
        switch (chromeItem.handle) {
        case CropHandle::ExpandToggle:
            label = tr("Expand");
            break;
        case CropHandle::Auto:
            label = tr("Auto");
            break;
        case CropHandle::Reset:
            label = tr("Reset");
            break;
        case CropHandle::Cancel:
            label = tr("Cancel");
            break;
        case CropHandle::Close:
            label = tr("Apply");
            break;
        default:
            break;
        }
        CropGeometry::paintTextButton(
            painter, chromeItem.rect,
            session().currentHoverHandle() == chromeItem.handle, label, chromeItem.role,
            chromeItem.toggled);
    }

    const QSize cropSz = session().draftPixelSize();
    CropGeometry::paintSizeBadge(painter, cropView, cropSz.width(), cropSz.height());

    painter.restore();
}

// --- from src/imageview_crop_raster.cpp ---

// Crop Full raster load: Thumtoo scheduleFullPixels or pool ImageLoader::load.
// PathRaster suspend before request so PreferCache cannot race Full.



void CropController::onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                            quint64 gen)
{
    if (gen != m_view->hostLoadGate().generation()) {
        return;
    }
    if (!decoded.isNull()) {
        ImageCache::put(path, decoded);
    }
    maybeUpgradeCropFullRaster(path, decoded);
}

void CropController::requestCropFullRaster(const QString &path)
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
    const quint64 gen = m_view->hostLoadGate().generation();
    const QPointer<ImageView> guard(m_view);
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

void CropController::maybeUpgradeCropFullRaster(const QString &path, const QImage &image)
{
    ImageItem *item = session().target();
    if (!item || item->path() != path) {
        if (session().acceptsFullRasterUpgrade(path)) {
            session().clearAwaitingFull();
        }
        return;
    }
    const bool covers = m_view->sampleCoversNativeLogical(path, image);
    if (!session().shouldAcceptFullRasterUpgrade(path, image, item, covers)) {
        return;
    }
    // Cache for Apply accuracy; do not reinstall mid-draft (stalls interaction).
    if (!path.isEmpty() && !image.isNull()) {
        ImageCache::put(path, image);
    }
    session().clearAwaitingFull();
    flashCropHud(CropFlash::fullReady());
}