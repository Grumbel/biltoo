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

ImageItem *ImageView::cropTargetItem() const
{
    // Crop session is bound to one subject for its entire lifetime. Never
    // re-resolve via selection or primaryItem() — that applied the draft to
    // unrelated tiles when selection changed mid-crop (IDENTITY.md).
    if (m_crop.active()) {
        if (m_crop.target()) {
            return m_crop.target();
        }
        if (m_crop.hasTargetId()) {
            if (ImageItem *byId = findItemBySessionId(m_crop.targetIdValue())) {
                return byId;
            }
        }
        return nullptr;
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
    const QPointF current = item->mapToScene(m_crop.currentRect().center());
    if (!qIsFinite(current.x()) || !qIsFinite(current.y())
        || !qIsFinite(sceneAnchor.x()) || !qIsFinite(sceneAnchor.y())) {
        return;
    }
    item->setPos(item->pos() + (sceneAnchor - current));
}

void ImageView::alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item) {
        return;
    }
    // Pixmap is centred on the item origin (offset -w/2,-h/2).
    const QPointF current = item->mapToScene(QPointF(0.0, 0.0));
    if (!qIsFinite(current.x()) || !qIsFinite(current.y())
        || !qIsFinite(sceneAnchor.x()) || !qIsFinite(sceneAnchor.y())) {
        return;
    }
    item->setPos(item->pos() + (sceneAnchor - current));
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
    // Lock identity for the whole crop session (IDENTITY.md).
    // Freeze sample installs immediately — before prepare attaches the draft.
    // m_crop.active() stays false until after the first draft attach (chrome timing);
    // freeze must not wait on m_crop.active() or ladder/async can land in between.
    m_crop.bindTarget(item, item->sessionId(), item->path());
    item->setTileLodSuppressed(true);
    if (m_pathRaster && !m_crop.draftPathRef().isEmpty()) {
        m_pathRaster->cancel(m_crop.draftPathRef());
    }
    // m_crop.active() is set only after the full-frame draft is installed (see
    // prepareCropModeFullImage / end of this function). Setting it earlier
    // painted one frame of crop chrome on the still-cropped bake.
    // Snapshot appearance before full-image reload so Close can be undone.
    QImage enterSrc = item->sourceImage().copy();
    if (enterSrc.isNull()) {
        enterSrc = item->previewImage().copy();
    }
    WorkspaceItemState enterSt = captureState(item);
    enterSt.hasCrop = item->sessionHasCrop();
    enterSt.cropRect = item->sessionCropRect();
    // cropRotation / cropSourceSize come from captureState → appearance.
    // Soft-only tiles have preview only; still a valid enter snapshot.
    m_crop.setEnterSnapshot(enterSrc, enterSt,
                            !enterSrc.isNull() || item->hasDisplayPixels());
    // Crop handles are axis-aligned in item space; free Workspace placement
    // rotation makes rubber-band and edge grips unusable. Unrotate for the
    // crop session and restore on exit.
    // Workspace: remember where the *displayed* image centre sits so the
    // restored crop frame can stay fixed while the full image grows around it.
    const QPointF workspaceAnchorScene = item->mapToScene(QPointF(0.0, 0.0));
    m_crop.stashPlacement(item->itemRotation(), item->itemShear());
    if (m_crop.hasStashedPlacement()) {
        item->setItemRotation(0.0);
        item->setItemShear(0.0);
    }
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
        if (m_crop.hasStashedPlacement()) {
            item->setItemRotation(m_crop.stashedPlacementRotationValue());
            item->setItemShear(m_crop.stashedPlacementShearValue());
        }
        m_crop.abortEnter();
        flashHud(tr("Crop"), tr("Could not load full image"));
        return false;
    }
    if (isWorkspaceMode()) {
        // If there was no stored crop angle but the tile was free-rotated,
        // seed the draft rotation so the frame matches the prior pose while
        // the item stays axis-aligned for editing.
        if (m_crop.isNearZeroRotation(CropGeometry::kFreeRotationEps)
            && qAbs(m_crop.stashedPlacementRotationValue()) > CropGeometry::kFreeRotationEps) {
            m_crop.setRotation(m_crop.stashedPlacementRotationValue());
            m_crop.normalizeRotation();
            ensureCropRectValid();
        }
        alignCropFrameCenterToScene(item, workspaceAnchorScene);
        updateWorkspaceSceneRect();
    }
    // m_crop.active() already true (set in prepare after full-frame install).
    m_crop.clearInteraction();
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
    if (m_crop.locksItem(item)) {
        return true;
    }
    // Host-only: targetId may refer to another live item with the same path.
    if (item && m_crop.isDraftSampleFrozen() && !item->path().isEmpty()
        && isCropDraftLockedPath(item->path())) {
        return true;
    }
    return false;
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
    // Start each crop session without Expand; re-enable below if the stored
    // draft (AABB or rotated corners) extends outside the source.
    m_crop.setAllowExpand(false);

    const QRectF cr = item->contentRect();
    const QRect priorCrop = (haveApp && app.hasCrop) ? app.cropRect : QRect();
    const bool hadCrop = haveApp && app.hasCrop && !priorCrop.isEmpty();

    if (hadCrop) {
        const QSize sz = item->imageSize();
        const QRect bounds(0, 0, sz.width(), sz.height());
        // cropRect is stored in the same space as the post-content-bake image
        // (recordSessionCrop runs with item flips cleared). Scale if the live
        // size differs from the size at record time — same rule as applyCrop.
        const QRect prior = SessionAppearance::scaleCropRect(
            priorCrop.normalized(), app.cropSourceSize, sz);
        m_crop.setRotation(haveApp ? app.cropRotation : 0.0);
        if (prior.width() <= 1 || prior.height() <= 1) {
            qCritical("initCropRect: prior crop scaled to %dx%d (stored %dx%d "
                      "sourceSize %dx%d live imageSize %dx%d) — draft will be 1×1",
                      prior.width(), prior.height(),
                      priorCrop.width(), priorCrop.height(),
                      app.cropSourceSize.width(), app.cropSourceSize.height(),
                      sz.width(), sz.height());
        }
        if (prior.width() >= 1 && prior.height() >= 1) {
            const QPointF off = item->offset();
            // Do not mirror for contentHFlip/VFlip: content bake / materialize already
            // put pixels in content-oriented space and the stored rect is in
            // that space. Re-mirroring shifted the frame on re-entry.
            m_crop.setRect(QRectF(prior.x() + off.x(), prior.y() + off.y(),
                                prior.width(), prior.height()));
            // Expand is not persisted. Detect both axis-aligned overflow and
            // rotated-corner overflow so ensureCropRectValid does not translate
            // a previously applied rotated draft to a new centre.
            if (CropGeometry::priorDraftNeedsExpand(
                    QRectF(prior), QRectF(bounds), m_crop.currentRect(), m_crop.currentRotation(), cr)) {
                m_crop.setAllowExpand(true);
            }
        } else {
            m_crop.setRect(cr);
            m_crop.setRotation(0.0);
        }
    } else {
        m_crop.setRect(cr);
        m_crop.setRotation(0.0);
    }
    ensureCropRectValid();
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
    // m_crop.active() true before fitItem so layout uses orient-only full size.
    m_crop.setMode(true);
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
    if (!m_crop.active() || path.isEmpty() || !m_crop.isAwaitingFullPath(path)) {
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

    // Draft → source pixel rect (content-local maps 1:1 for normal images).
    const qreal sx = qreal(src.width()) / cr.width();
    const qreal sy = qreal(src.height()) / cr.height();
    QRect search(
        int(qFloor((m_crop.currentRect().left() - cr.left()) * sx)),
        int(qFloor((m_crop.currentRect().top() - cr.top()) * sy)),
        int(qCeil(m_crop.currentRect().width() * sx)),
        int(qCeil(m_crop.currentRect().height() * sy)));
    search = search.intersected(QRect(0, 0, src.width(), src.height()));

    QRect trimmed;
    if (!ImageLoader::autoTrimRect(src, search, &trimmed)) {
        return;
    }
    // Small pad so text glyphs are not tight against the frame.
    constexpr int kPad = 2;
    trimmed.adjust(-kPad, -kPad, kPad, kPad);
    trimmed = trimmed.intersected(QRect(0, 0, src.width(), src.height()));
    if (!trimmed.isValid() || trimmed.isEmpty()) {
        return;
    }

    const qreal invSx = ContentXform::invAxisScale(cr.width(), src.width());
    const qreal invSy = ContentXform::invAxisScale(cr.height(), src.height());
    m_crop.setRect(QRectF(cr.left() + trimmed.x() * invSx,
                        cr.top() + trimmed.y() * invSy,
                        trimmed.width() * invSx,
                        trimmed.height() * invSy));
    m_crop.setRotation(0.0);
    m_crop.setAllowExpand(false);
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
    QRectF local = localCrop.normalized();
    if (!m_crop.isAllowExpand()) {
        local = local.intersected(cr);
    }
    if (local.width() < 1.0 || local.height() < 1.0) {
        return;
    }
    const QPointF off = item->offset();
    // Crop mode always edits the full on-disk image — store absolute source rect.
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    const int iw = item->imageSize().width();
    const int ih = item->imageSize().height();
    const QRect disp = CropGeometry::flipAwareSourceCrop(
        CropGeometry::integerCropFromLocal(local, off), iw, ih,
        item->itemHFlip(), item->itemVFlip());

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
    const bool fullFrame =
        !m_crop.hasValidRect()
        || (qAbs(m_crop.currentRect().left() - full.left()) < 0.5
            && qAbs(m_crop.currentRect().top() - full.top()) < 0.5
            && qAbs(m_crop.currentRect().width() - full.width()) < 0.5
            && qAbs(m_crop.currentRect().height() - full.height()) < 0.5);
    // Record content-space crop while the draft frame is still valid.
    recordSessionCrop(item, m_crop.currentRect().isValid() ? m_crop.currentRect() : full);
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
        const qreal cropW = m_crop.currentRect().width();
        const qreal cropH = m_crop.currentRect().height();
        const qreal footW = cropW * sx0;
        const qreal footH = cropH * sy0;
        const QPointF cropSceneCenter = item->mapToScene(m_crop.currentRect().center());

        const QString path = item->path();
        QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
        const bool hostFromCache = !host.isNull();
        if (!hostFromCache) {
            // Draft must be orient-only full frame. If the item already holds a
            // crop bake (second Apply without cache), refuse — cropping the bake
            // double-crops and shrinks Workspace tiles.
            if (item->hasAppliedContentXform()
                && item->appliedContentXform().hasCrop) {
                flashHud(tr("Crop"), tr("Full image not ready — try again"));
                return false;
            }
            host = item->sourceImage();
            if (host.isNull()) {
                host = item->previewImage();
            }
        }
        if (host.isNull()) {
            flashHud(tr("Crop"), tr("No pixels to crop"));
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
            st.cropRect = CropGeometry::integerCropFromLocal(
                QRectF(m_crop.currentRect().left(), m_crop.currentRect().top(), cropW, cropH),
                item->offset());
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
            item->setPos(m_crop.enterPos());
            item->setItemScale(m_crop.enterScaleX(),
                               m_crop.enterScaleY() > 0.0
                                   ? m_crop.enterScaleY()
                                   : m_crop.enterScaleX());
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
        item->setPos(m_crop.enterPos());
        item->setItemScale(m_crop.enterScaleX(),
                           m_crop.enterScaleY() > 0.0
                               ? m_crop.enterScaleY()
                               : m_crop.enterScaleX());
    }
}

void ImageView::clearCropModeState()
{
    // Unsuppress LOD before binding is cleared.
    if (m_crop.target()) {
        m_crop.target()->setTileLodSuppressed(false);
    } else if (m_crop.hasTargetId()) {
        if (ImageItem *byId = findItemBySessionId(m_crop.targetIdValue())) {
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
    if (item && m_crop.hasStashedPlacement() && !preserveCropFrameRotation) {
        item->setItemRotation(m_crop.stashedPlacementRotationValue());
        item->setItemShear(m_crop.stashedPlacementShearValue());
    }
    clearCropModeState();
}

QPolygonF ImageView::cropPolygonItemLocal() const
{
    return CropGeometry::rotatedCorners(m_crop.currentRect().normalized(), m_crop.currentRotation());
}

QRectF ImageView::cropRectView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.hasValidRect()) {
        return QRectF();
    }
    const QPolygonF local = cropPolygonItemLocal();
    const QPolygonF poly = item->mapToScene(local);
    QRectF sceneBounds = poly.boundingRect();
    const QPoint tl = mapFromScene(sceneBounds.topLeft());
    const QPoint br = mapFromScene(sceneBounds.bottomRight());
    return QRectF(tl, br).normalized();
}

QRect ImageView::cropExpandButtonView() const
{
    if (!m_crop.active()) {
        return {};
    }
    const CropGeometry::CropButtonLayout L = CropGeometry::cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.expand : QRect();
}

QRect ImageView::cropAutoButtonView() const
{
    if (!m_crop.active()) {
        return {};
    }
    const CropGeometry::CropButtonLayout L = CropGeometry::cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.autoBtn : QRect();
}

QRect ImageView::cropResetButtonView() const
{
    if (!m_crop.active()) {
        return {};
    }
    const CropGeometry::CropButtonLayout L = CropGeometry::cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.reset : QRect();
}

QRect ImageView::cropCancelButtonView() const
{
    if (!m_crop.active()) {
        return {};
    }
    const CropGeometry::CropButtonLayout L = CropGeometry::cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.cancel : QRect();
}

QRect ImageView::cropCloseButtonView() const
{
    if (!m_crop.active()) {
        return {};
    }
    const CropGeometry::CropButtonLayout L = CropGeometry::cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.apply : QRect();
}



void ImageView::paintCropDimOutside(QPainter &painter, const QPolygonF &cropViewPoly)
{
    // Dim everything outside the (possibly rotated) crop.
    QPainterPath outer;
    outer.addRect(QRectF(viewport()->rect()));
    QPainterPath hole;
    hole.addPolygon(cropViewPoly);
    hole.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawPath(outer.subtracted(hole));
}

void ImageView::paintCropFrame(QPainter &painter, const QPolygonF &cropViewPoly)
{
    // Crop frame — amber family (distinct from single-select blue / group violet).
    painter.setBrush(Qt::NoBrush);
    QPen frame(QColor(255, 190, 40, 240), 0);
    frame.setCosmetic(true);
    frame.setWidthF(1.75);
    painter.setPen(frame);
    painter.drawPolygon(cropViewPoly);
    QPen dash(QColor(40, 30, 10, 180), 0, Qt::DashLine);
    dash.setCosmetic(true);
    dash.setWidthF(1.0);
    painter.setPen(dash);
    painter.drawPolygon(cropViewPoly);
}

void ImageView::paintCropResizeHandles(QPainter &painter, const QPolygonF &cropViewPoly)
{
    // Bold corner + edge bars at rotated corners (poly order: TL, TR, BR, BL).
    const QPointF tl = cropViewPoly.at(0);
    const QPointF tr = cropViewPoly.at(1);
    const QPointF br = cropViewPoly.at(2);
    const QPointF bl = cropViewPoly.at(3);
    const qreal hs = 14.0;
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB,
                          CropHandle h) {
        const bool hot = m_crop.isHandleHot(h);
        auto unit = [](QPointF v) {
            const qreal len = qHypot(v.x(), v.y());
            return len > 1e-6 ? v / len : QPointF(1, 0);
        };
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal arm = hs * (hot ? 1.55 : 1.25);
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? QColor(255, 255, 255) : QColor(255, 190, 40), 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter.setPen(hp);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        if (hot) {
            QPen glow(QColor(255, 190, 40, 200), 0);
            glow.setCosmetic(true);
            glow.setWidthF(thick * 0.55);
            glow.setCapStyle(Qt::RoundCap);
            glow.setJoinStyle(Qt::RoundJoin);
            painter.setPen(glow);
            painter.drawPath(path);
        }
    };
    // along directions follow rotated edges away from the corner.
    drawCorner(tl, tr - tl, bl - tl, CropHandle::TopLeft);
    drawCorner(tr, tl - tr, br - tr, CropHandle::TopRight);
    drawCorner(bl, br - bl, tl - bl, CropHandle::BottomLeft);
    drawCorner(br, bl - br, tr - br, CropHandle::BottomRight);

    auto drawEdgeBar = [&](const QPointF &mid, const QPointF &along, CropHandle h) {
        const bool hot = m_crop.isHandleHot(h);
        auto unit = [](QPointF v) {
            const qreal len = qHypot(v.x(), v.y());
            return len > 1e-6 ? v / len : QPointF(1, 0);
        };
        const QPointF a = unit(along);
        const QPointF perp(-a.y(), a.x());
        const qreal len = hs * (hot ? 2.2 : 1.7);
        const qreal thick = hs * (hot ? 0.42 : 0.30);
        // Outline matches workspace edge bars (accent family only differs by hue).
        QPen hp(hot ? QColor(255, 255, 255) : QColor(180, 130, 20), 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.6 : 1.15);
        painter.setPen(hp);
        painter.setBrush(hot ? QColor(255, 220, 80, 255) : QColor(255, 190, 40, 240));
        QPolygonF bar;
        bar << mid + a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) - perp * (thick / 2)
            << mid + a * (len / 2) - perp * (thick / 2);
        painter.drawPolygon(bar);
        painter.setBrush(Qt::NoBrush);
    };
    drawEdgeBar((tl + tr) / 2.0, tr - tl, CropHandle::Top);
    drawEdgeBar((bl + br) / 2.0, br - bl, CropHandle::Bottom);
    drawEdgeBar((tl + bl) / 2.0, bl - tl, CropHandle::Left);
    drawEdgeBar((tr + br) / 2.0, br - tr, CropHandle::Right);

    }

void ImageView::paintCropRotateKnobs(QPainter &painter, const QPolygonF &cropViewPoly)
{
    // Rotate knobs on each side (outward from edge midpoints).
    const CropGeometry::CropFrameViewAnchors a =
        CropGeometry::frameViewAnchors(cropViewPoly);
    if (!a.valid) {
        return;
    }
    const bool hot = (m_crop.currentHoverHandle() == CropHandle::Rotate
                      || m_crop.currentActiveHandle() == CropHandle::Rotate);
    auto drawRotateKnob = [&](const QPointF &mid, const QPointF &knob) {
        QPen stem(hot ? QColor(255, 255, 255) : QColor(255, 190, 40), 0);
        stem.setCosmetic(true);
        stem.setWidthF(hot ? 1.8 : 1.3);
        painter.setPen(stem);
        painter.drawLine(mid, knob);
        painter.setBrush(hot ? QColor(255, 220, 80) : QColor(255, 190, 40));
        painter.drawEllipse(knob, hot ? 6.0 : 5.0, hot ? 6.0 : 5.0);
        painter.setBrush(Qt::NoBrush);
    };
    drawRotateKnob(a.tm, a.rotTop);
    drawRotateKnob(a.rm, a.rotRight);
    drawRotateKnob(a.bm, a.rotBottom);
    drawRotateKnob(a.lm, a.rotLeft);
}

void ImageView::paintCropMoveGrip(QPainter &painter, const QPolygonF &cropViewPoly)
{
    // Move grip at centre (interior of the crop starts a rubber-band, not Move).
    const CropGeometry::CropFrameViewAnchors a =
        CropGeometry::frameViewAnchors(cropViewPoly);
    if (!a.valid) {
        return;
    }
    const QPointF centre = a.centre;
    const bool hot = (m_crop.currentHoverHandle() == CropHandle::Move
                      || m_crop.currentActiveHandle() == CropHandle::Move);
    const qreal s = hot ? 10.0 : 9.0;
    painter.setPen(QPen(hot ? QColor(255, 255, 255) : QColor(40, 30, 10), hot ? 1.8 : 1.35));
    painter.setBrush(hot ? QColor(255, 220, 80, 255) : QColor(255, 190, 40, 240));
    painter.drawRoundedRect(QRectF(centre.x() - s, centre.y() - s, 2 * s, 2 * s), 3.0, 3.0);
    // Crosshair to signal "move"
    painter.setPen(QPen(QColor(40, 30, 10), 1.35));
    painter.drawLine(QPointF(centre.x() - s + 3, centre.y()),
                     QPointF(centre.x() + s - 3, centre.y()));
    painter.drawLine(QPointF(centre.x(), centre.y() - s + 3),
                     QPointF(centre.x(), centre.y() + s - 3));
    painter.setBrush(Qt::NoBrush);
}

void ImageView::drawCropTextButton(QPainter &painter, const QRect &btn, CropHandle kind,
                                   const QString &label, CropBtnRole role, bool toggled)
{
    if (!btn.isValid()) {
        return;
    }
    const bool hover = (m_crop.currentHoverHandle() == kind);
    const qreal radius = (role == CropBtnRole::Toggle) ? 6.0 : 11.0; // square vs pill
    QColor fill(50, 50, 50, 230);
    QColor border(255, 190, 40);
    QColor text(240, 240, 240);
    qreal borderW = 1.15;
    switch (role) {
    case CropBtnRole::Toggle:
        // Teal/cyan — distinct from amber Apply so Expand does not read as commit.
        if (toggled) {
            fill = hover ? QColor(100, 210, 230, 255) : QColor(60, 175, 200, 245);
            border = QColor(255, 255, 255);
            text = QColor(10, 35, 45);
            borderW = 2.0;
        } else {
            fill = hover ? QColor(30, 70, 85, 230) : QColor(40, 40, 40, 220);
            border = hover ? QColor(255, 255, 255) : QColor(70, 170, 195);
            borderW = hover ? 1.75 : 1.25;
        }
        break;
    case CropBtnRole::Action:
        fill = hover ? QColor(80, 60, 20, 240) : QColor(50, 50, 50, 230);
        border = hover ? QColor(255, 255, 255) : QColor(255, 190, 40);
        borderW = hover ? 1.75 : 1.15;
        break;
    case CropBtnRole::Neutral:
        fill = hover ? QColor(70, 70, 70, 240) : QColor(45, 45, 45, 220);
        border = hover ? QColor(200, 200, 200) : QColor(120, 120, 120);
        text = QColor(220, 220, 220);
        borderW = hover ? 1.5 : 1.0;
        break;
    case CropBtnRole::Commit:
        fill = hover ? QColor(255, 210, 70, 255) : QColor(240, 175, 40, 245);
        border = hover ? QColor(255, 255, 255) : QColor(120, 80, 10);
        text = QColor(40, 25, 5);
        borderW = hover ? 1.75 : 1.25;
        break;
    }
    QPen pen(border);
    pen.setWidthF(borderW);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRoundedRect(btn, radius, radius);
    if (role == CropBtnRole::Toggle && toggled) {
        QPen ring(QColor(255, 255, 255, 200));
        ring.setWidthF(1.1);
        ring.setCosmetic(true);
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(btn.adjusted(3, 3, -3, -3), radius * 0.7, radius * 0.7);
    }
    painter.setPen(text);
    QFont f = painter.font();
    f.setPointSize(CropGeometry::clampButtonPointSize(f.pointSize()));
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(btn, Qt::AlignCenter, label);
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
                       ImageView::tr("Expand"), CropBtnRole::Toggle, m_crop.isAllowExpand());
    drawCropTextButton(painter, cropAutoButtonView(), CropHandle::Auto, ImageView::tr("Auto"),
                       CropBtnRole::Action);
    drawCropTextButton(painter, cropResetButtonView(), CropHandle::Reset, ImageView::tr("Reset"),
                       CropBtnRole::Action);
    drawCropTextButton(painter, cropCancelButtonView(), CropHandle::Cancel, ImageView::tr("Cancel"),
                       CropBtnRole::Neutral);
    drawCropTextButton(painter, cropCloseButtonView(), CropHandle::Close, ImageView::tr("Apply"),
                       CropBtnRole::Commit);
}



void ImageView::paintCropSizeBadge(QPainter &painter, const QRect &cropView)
{
    // Crop size in image pixels (same coordinate space as the draft rect).
    const QSize cropSz = ContentXform::roundedSizeAtLeast1(m_crop.currentRect().width(), m_crop.currentRect().height());
    const int cropW = cropSz.width();
    const int cropH = cropSz.height();
    const QString sizeLabel = QStringLiteral("%1×%2").arg(cropW).arg(cropH);
    {
        QFont f = painter.font();
        f.setPointSize(CropGeometry::clampLabelPointSize(f.pointSize()));
        f.setBold(true);
        painter.setFont(f);
        const QFontMetrics fm(f);
        const int padX = 8;
        const int padY = 4;
        const int tw = fm.horizontalAdvance(sizeLabel);
        const int th = fm.height();
        // Prefer above the crop frame; fall back inside top edge if off-screen.
        // Always inside the crop frame so the label does not sit on the top
        // rotate knob (outside the top edge).
        int lx = cropView.center().x() - (tw + 2 * padX) / 2;
        int ly = cropView.top() + 8;
        if (ly + th + 2 * padY > cropView.bottom() - 8) {
            ly = cropView.center().y() - (th + 2 * padY) / 2;
        }
        const QRect labelBg(lx, ly, tw + 2 * padX, th + 2 * padY);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(labelBg, 4, 4);
        painter.setPen(QColor(255, 220, 120));
        painter.drawText(labelBg, Qt::AlignCenter, sizeLabel);
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
    const QRectF contentScene = item->mapToScene(item->contentRect()).boundingRect();
    const QPolygonF cropLocal = cropPolygonItemLocal();
    const QPolygonF cropScenePoly = item->mapToScene(cropLocal);
    QPolygonF cropViewPoly;
    for (const QPointF &sp : cropScenePoly) {
        cropViewPoly << mapFromScene(sp);
    }
    const QRect contentView = QRect(mapFromScene(contentScene.topLeft()),
                                    mapFromScene(contentScene.bottomRight())).normalized();
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

    Q_UNUSED(contentView);
    painter.restore();
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || h == CropHandle::None || h == CropHandle::Reset || h == CropHandle::Close
        || h == CropHandle::Cancel || h == CropHandle::ExpandToggle
        || h == CropHandle::Auto) {
        return;
    }
    const QPointF startLocal = item->mapFromScene(mapToScene(viewPos));
    m_crop.beginHandleDrag(h, m_crop.currentRect(), startLocal);
    if (h == CropHandle::Rotate) {
        m_crop.setRotateStart(
            m_crop.currentRotation(),
            PlacementLinear::angleAbout(m_crop.currentRect().center(), startLocal));
    }
}

void ImageView::updateCropMoveDrag(const QPointF &local, const QRectF &cr)
{
    const QPointF delta = local - m_crop.dragStartLocalRef();
    QRectF r = m_crop.dragStartRectRef().translated(delta);
    if (!m_crop.isAllowExpand()) {
        // Move must never shrink the draft (axis-aligned intersect used to clip
        // size at the image edge). Only slide so corners stay inside — same as
        // the rotated-frame path via translateCropInside.
        r = r.normalized();
        r = CropGeometry::translateInside(r, m_crop.currentRotation(), cr);
    }
    m_crop.setRect(r);
    viewport()->update();
}

void ImageView::updateCropRotateDrag(const QPointF &local, const QRectF &cr, qreal minSide)
{
    // Ctrl → 45° (includes 90°); Shift (alone or with Ctrl) → 15°.
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    m_crop.setRotation(CropGeometry::rotationFromDrag(
        local, m_crop.dragStartRectRef().center(), m_crop.rotateStartRotationValue(),
        m_crop.rotateStartAngleValue(), mods & Qt::ShiftModifier, mods & Qt::ControlModifier));
    if (!m_crop.isAllowExpand()) {
        m_crop.setRect(CropGeometry::constrainToContent(m_crop.dragStartRectRef(), m_crop.currentRotation(), cr,
                                            minSide));
    }
    viewport()->update();
}

void ImageView::updateCropResizeDrag(const QPointF &local, const QRectF &cr, const QRectF &limits,
                                     qreal minSide)
{
    // Resize in crop-local axes, then map the new centre through crop rotation
    // so edges stay under the grips when the frame is rotated.
    const bool fromCenter =
        QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    const bool forceSquare =
        QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    QRectF r = CropGeometry::resizeDraftRect(
        m_crop.currentActiveHandle(), local, m_crop.dragStartRectRef(), m_crop.currentRotation(), minSide,
        fromCenter, forceSquare);

    if (m_crop.isAllowExpand()) {
        r = r.intersected(limits);
        if (r.width() < minSide) {
            r.setWidth(minSide);
        }
        if (r.height() < minSide) {
            r.setHeight(minSide);
        }
        m_crop.setRect(r);
    } else {
        m_crop.setRect(CropGeometry::constrainToContent(r, m_crop.currentRotation(), cr, minSide));
    }
    viewport()->update();
}



void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.isHandleDragging()) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const QRectF cr = item->contentRect();
    const QRectF limits = m_crop.isAllowExpand()
        ? cr.adjusted(-cr.width() * 4, -cr.height() * 4, cr.width() * 4, cr.height() * 4)
        : cr;
    const qreal minSide = 4.0;

    if (m_crop.currentActiveHandle() == CropHandle::Move) {
        updateCropMoveDrag(local, cr);
        return;
    }
    if (m_crop.currentActiveHandle() == CropHandle::Rotate) {
        updateCropRotateDrag(local, cr, minSide);
        return;
    }
    updateCropResizeDrag(local, cr, limits, minSide);
}

void ImageView::endCropHandleDrag()
{
    m_crop.endHandleDrag();
    ensureCropRectValid();
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
    m_crop.beginRubber(local);
    m_crop.setRect(QRectF(local, QSizeF(0, 0)));
    m_crop.setRotation(0.0); // new rubber-band is axis-aligned
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
    QRectF r = CropGeometry::rubberBandRect(
        m_crop.rubberOriginLocalRef(), local,
        mods & Qt::ShiftModifier, mods & Qt::ControlModifier);
    if (r.width() < 1.0) {
        r.setWidth(1.0);
    }
    if (r.height() < 1.0) {
        r.setHeight(1.0);
    }
    m_crop.setRect(m_crop.isAllowExpand() ? r : r.intersected(cr));
    viewport()->update();
}

void ImageView::endCropRubberBand()
{
    m_crop.endRubber();
    ensureCropRectValid();
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
        CropGeometry::cropButtonLayout(cropRectView(), viewport()->rect());
    // Map rotated crop corners through item → scene → view.
    const QPolygonF localPoly = cropPolygonItemLocal();
    QPolygonF viewPoly;
    viewPoly.reserve(4);
    for (const QPointF &local : localPoly) {
        viewPoly << QPointF(mapFromScene(item->mapToScene(local)));
    }
    const CropGeometry::CropFrameViewAnchors anchors =
        CropGeometry::frameViewAnchors(viewPoly);
    return CropGeometry::hitTestCropChrome(viewPos, buttons, anchors);
}

