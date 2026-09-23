// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crop/cropsession.h"
#include "crop/cropgeometry.h"
#include "contentxform.h"
#include "placementlinear.h"
#include "imageitem.h"
#include "itemcomponents.h"
#include "session/sessionappearance.h"
#include "display/imagecache.h"
#include "thumtoocache.h"
#include "imageloader.h"
#include "coloradjust.h"

CropSession::EnterFullRaster CropSession::pickEnterFullRaster(ImageItem *item,
                                                              const QString &path,
                                                              bool hadPriorCrop)
{
    EnterFullRaster out;
    if (!path.isEmpty()) {
        out.image = ImageCache::get(path);
        if (!out.image.isNull()) {
            out.unoriented = true;
            return out;
        }
    }
    if (!hadPriorCrop && item) {
        out.image = item->sourceImage();
        if (out.image.isNull()) {
            out.image = item->previewImage();
        }
        out.unoriented = false;
    }
    return out;
}

QImage CropSession::pickApplyHost(ImageItem *item, const QString &path, bool *fromCache,
                                  bool hasApplied, const ContentXform::Value &applied)
{
    QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (fromCache) {
        *fromCache = !host.isNull();
    }
    if (!host.isNull()) {
        return host;
    }
    if (item && hasApplied && applied.hasCrop) {
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

bool CropSession::canKeepDisplayForEnter(const ImageItem *item,
                                         const ContentXform::Value &wantX,
                                         const WorkspaceItemState &contentOnly,
                                         bool hadPriorCrop, bool needGeomBake,
                                         bool hasApplied,
                                         const ContentXform::Value &applied,
                                         const ColorAdjustments &liveColor)
{
    if (!item || hadPriorCrop || !item->hasDisplayPixels()
        || (hasApplied && applied.hasCrop)) {
        return false;
    }
    if (item->displayPixelLongEdge() < ContentXform::kGuiMaterializeMaxEdge) {
        return false;
    }
    const bool appliedOk = hasApplied
        && !applied.hasCrop
        && ContentXform::equal(applied, wantX);
    const bool liveGradeOk = !hasApplied
        && !needGeomBake
        && liveColor.matches(contentOnly.colorAdjust);
    return appliedOk || liveGradeOk;
}

void CropSession::clearItemFreePlacementForDraft(ImageItem *item)
{
    if (!item) {
        return;
    }
    ItemComponents::Placement pl = item->placement();
    pl.rotation = 0.0;
    pl.shear = 0.0;
    pl.hFlip = false;
    pl.vFlip = false;
    item->applyPlacement(pl);
}

int CropSession::fullRasterScheduleEdge(const QString &path)
{
    int edge = 8192;
    if (path.isEmpty()) {
        return edge;
    }
    const QSize native = ThumtooCache::cachedSize(path);
    if (native.isValid() && native.width() > 0 && native.height() > 0) {
        edge = ContentXform::clampLongEdge(ContentXform::longEdge(native),
                                          ImageCache::kDisplayMaxEdge);
    }
    return edge;
}

bool CropSession::tryScheduleThumtooFullRaster(const QString &path)
{
    if (path.isEmpty() || !ThumtooCache::isAvailable()) {
        return false;
    }
    const int edge = fullRasterScheduleEdge(path);
    if (ThumtooCache::scheduleFullPixels(path, edge)) {
        return true;
    }
    return ThumtooCache::isPixelsPending(path, edge);
}

CropSession::ApplyBakeResult CropSession::materializeApplyDisplay(const QImage &host,
                                                                  bool hostFromCache,
                                                                  WorkspaceItemState st)
{
    ApplyBakeResult out;
    out.bake = st;
    if (host.isNull()) {
        return out;
    }
    // materializeDisplay: prefer unoriented ImageCache host + full want.
    // Draft item pixels may be orient- or crop-baked — crop-only bake then.
    if (!hostFromCache) {
        out.bake.contentQuarterTurns = 0;
        out.bake.contentHFlip = false;
        out.bake.contentVFlip = false;
    }
    QImage sample = host;
    out.multiMp = ImageCache::longEdge(sample) > ContentXform::kGuiMaterializeMaxEdge;
    if (out.multiMp) {
        sample = ImageCache::clampToMaxEdge(sample, ContentXform::kGuiMaterializeMaxEdge);
    }
    out.display = SessionAppearance::materializeDisplay(
        sample, out.bake,
        out.multiMp ? SessionAppearance::PixelKind::SoftPreview
                    : SessionAppearance::PixelKind::FullSource);
    return out;
}

CropSession::EnterInstallSample CropSession::prepareEnterInstallSample(
    const QImage &full, bool unorientedSource,
    const WorkspaceItemState *app, bool haveApp)
{
    EnterInstallSample out;
    if (haveApp && app) {
        out.contentOnly = SessionAppearance::withoutCrop(*app);
    }
    out.wantX = ContentXform::Value::fromState(out.contentOnly);
    out.hadPriorCrop = haveApp && app && app->hasCrop && !app->cropRect.isEmpty();
    out.needGeomBake = out.contentOnly.contentHFlip || out.contentOnly.contentVFlip
        || out.contentOnly.contentQuarterTurns != 0;
    out.needColor = !out.contentOnly.colorAdjust.isIdentity();

    QImage sample = full;
    out.kind = SessionAppearance::PixelKind::FullSource;
    constexpr int kColorOnlyDraftMaxEdge = 2048;
    if (out.needGeomBake
        && ImageCache::longEdge(sample) > ContentXform::kGuiMaterializeMaxEdge) {
        sample = ImageCache::clampToMaxEdge(
            sample, ContentXform::kGuiMaterializeMaxEdge);
        out.kind = SessionAppearance::PixelKind::SoftPreview;
    } else if (!out.needGeomBake && out.needColor
               && ImageCache::longEdge(sample) > kColorOnlyDraftMaxEdge) {
        sample = ImageCache::clampToMaxEdge(sample, kColorOnlyDraftMaxEdge);
        out.kind = SessionAppearance::PixelKind::SoftPreview;
    }

    if (unorientedSource && out.needGeomBake) {
        out.display = SessionAppearance::materializeDisplay(sample, out.contentOnly, out.kind);
        if (out.display.isNull()) {
            out.display = sample;
        }
    } else if (unorientedSource && out.needColor) {
        out.display = applyColorAdjustments(sample, out.contentOnly.colorAdjust);
        if (out.display.isNull()) {
            out.display = sample;
        }
    } else {
        out.display = sample;
    }
    if (out.display.isNull()) {
        out.display = sample;
    }
    return out;
}


void CropSession::mergeOrientFromAppearance(WorkspaceItemState *s,
                                               const WorkspaceItemState *orient)
{
    if (!s || !orient) {
        return;
    }
    s->contentQuarterTurns = orient->contentQuarterTurns;
    s->contentHFlip = orient->contentHFlip;
    s->contentVFlip = orient->contentVFlip;
}

void CropSession::itemScalePair(const ImageItem *item, qreal *sx, qreal *sy)
{
    if (!item || !sx || !sy) {
        return;
    }
    const ItemComponents::Placement pl = item->placement();
    *sx = pl.scale;
    *sy = pl.scaleY > 0.0 ? pl.scaleY : *sx;
}

SessionImageId CropSession::sessionIdForRecord(const ImageItem *item,
                                               SessionImageId boundTargetId,
                                               SessionImageId imageModeCurrentId)
{
    if (!item) {
        return kInvalidSessionImageId;
    }
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId) {
        sid = boundTargetId;
    }
    if (sid == kInvalidSessionImageId) {
        sid = imageModeCurrentId;
    }
    return sid;
}

bool CropSession::isAxisAlignedFullFrame(const QRectF &local, const QRectF &contentRect,
                                         qreal eps)
{
    return qAbs(local.left() - contentRect.left()) < eps
        && qAbs(local.top() - contentRect.top()) < eps
        && qAbs(local.width() - contentRect.width()) < eps
        && qAbs(local.height() - contentRect.height()) < eps;
}

CropSession::RecordGeometry CropSession::computeRecordGeometry(
    const QRectF &localCrop, const QRectF &contentRect, const QPointF &itemOffset,
    int imageW, int imageH, bool hFlip, bool vFlip) const
{
    RecordGeometry out;
    out.localClamped = clampLocalCrop(localCrop, contentRect);
    if (out.localClamped.isEmpty()) {
        return out;
    }
    out.sourceRect = sourceCropFromLocal(
        out.localClamped, itemOffset, imageW, imageH, hFlip, vFlip);
    out.clearCrop = isAxisAlignedFullFrame(out.localClamped, contentRect)
        && isNearZeroRotation(CropGeometry::kFreeRotationEps);
    return out;
}

void CropSession::applyScenePosDelta(ImageItem *item, const QPointF &delta)
{
    if (!item || !qIsFinite(delta.x()) || !qIsFinite(delta.y())) {
        return;
    }
    ItemComponents::Placement pl = item->placement();
    pl.pos += delta;
    item->applyPlacement(pl);
}

void CropSession::seedApplyCropState(WorkspaceItemState *st, const QPointF &itemOffset,
                                     const QSize &imageSize) const
{
    if (!st) {
        return;
    }
    st->hasCrop = true;
    st->cropRect = integerCropForOffset(itemOffset);
    st->cropSourceSize = imageSize;
    st->cropRotation = currentRotation();
}

void CropSession::restoreEnterScale(ImageItem *item) const
{
    if (!item || enterScaleX() <= 1e-6) {
        return;
    }
    const qreal sx = enterScaleX();
    const qreal sy = enterScaleY() > 1e-6 ? enterScaleY() : sx;
    ItemComponents::Placement pl = item->placement();
    pl.scale = sx;
    pl.scaleY = sy;
    item->applyPlacement(pl);
}

QSize CropSession::cropBasisSize(const QSize &imageSize, const QSize &fileNative,
                                 const WorkspaceItemState *orientFromAppearance,
                                 bool hasApplied, const ContentXform::Value &applied)
{
    QSize cropBasis = imageSize;
    if (fileNative.width() <= 1 || fileNative.height() <= 0) {
        return cropBasis;
    }
    WorkspaceItemState orientOnly;
    if (orientFromAppearance) {
        orientOnly.contentQuarterTurns = orientFromAppearance->contentQuarterTurns;
        orientOnly.contentHFlip = orientFromAppearance->contentHFlip;
        orientOnly.contentVFlip = orientFromAppearance->contentVFlip;
    } else if (hasApplied) {
        orientOnly.contentHFlip = applied.hFlip;
        orientOnly.contentVFlip = applied.vFlip;
        orientOnly.contentQuarterTurns =
            ContentXform::normalizeQuarterTurns(applied.quarterTurns);
    }
    const QSize oriented = ContentXform::layoutSize(fileNative, orientOnly);
    if (oriented.width() > 1 && oriented.height() > 0) {
        return oriented;
    }
    return cropBasis;
}

void CropSession::applyRecordToState(WorkspaceItemState *s, const RecordGeometry &rec,
                                     const QSize &cropBasis) const
{
    if (!s) {
        return;
    }
    if (rec.clearCrop) {
        *s = SessionAppearance::withoutCrop(*s);
        s->cropSourceSize = QSize();
        s->cropRotation = 0.0;
        return;
    }
    s->hasCrop = true;
    s->cropRect = rec.sourceRect;
    s->cropSourceSize = cropBasis;
    s->cropRotation = currentRotation();
}

bool CropSession::applyPaddedAutoTrim(const QRectF &contentRect, const QSize &srcSize,
                                      const QRect &trimmed, int padPx)
{
    QRect t = CropGeometry::paddedIntersectedRect(trimmed, srcSize, padPx);
    if (!t.isValid() || t.isEmpty()) {
        return false;
    }
    setRectFromSourcePixelTrim(contentRect, srcSize, t);
    ensureRectValid(contentRect);
    return true;
}

QImage CropSession::pickAutoCropSourcePixels(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    QImage src = item->sourceImage();
    if (src.isNull()) {
        src = item->pixmap().toImage();
    }
    return src;
}

bool CropSession::tryPaddedAutoTrim(const QRectF &contentRect, const QImage &src, int padPx)
{
    if (src.isNull() || contentRect.width() < 1.0 || contentRect.height() < 1.0) {
        return false;
    }
    const QRect search = sourceSearchRectFromDraft(contentRect, src.size());
    if (!search.isValid() || search.isEmpty()) {
        return false;
    }
    QRect trimmed;
    if (!ImageLoader::autoTrimRect(src, search, &trimmed)) {
        return false;
    }
    return applyPaddedAutoTrim(contentRect, src.size(), trimmed, padPx);
}

void CropSession::queueFullRematerializeIfSoft(bool hostFromCache, bool multiMp,
                                              const QString &path, SessionImageId sid,
                                              const WorkspaceItemState &st)
{
    if (hostFromCache && multiMp) {
        queuePendingFullRematerialize(path, sid, st);
    }
}

QImage CropSession::pickEnterSnapshotPixels(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    QImage src = item->sourceImage().copy();
    if (src.isNull()) {
        src = item->previewImage().copy();
    }
    return src;
}

bool CropSession::shouldPushResetUndo(const QSize &currentSourceSize) const
{
    return isEnterValid()
        && (enterHadCrop() || enterSourceDiffersFrom(currentSourceSize));
}

bool CropSession::maybePutUnorientedHostCache(const QString &path, const QImage &full,
                                              bool unorientedSource, bool coversNative)
{
    if (path.isEmpty() || !unorientedSource || !coversNative || full.isNull()) {
        return false;
    }
    ImageCache::put(path, full);
    return true;
}

void CropSession::finishRubber(const QRectF &contentRect)
{
    endRubber();
    ensureRectValid(contentRect);
}

bool CropSession::shouldAcceptFullRasterUpgrade(const QString &path, const QImage &image,
                                                const ImageItem *item, bool coversNative) const
{
    if (!acceptsFullRasterUpgrade(path) || image.isNull()) {
        return false;
    }
    if (!item || item->path() != path) {
        return false;
    }
    if (!coversNative
        && ImageCache::longEdge(image) <= item->displayPixelLongEdge()) {
        return false;
    }
    return true;
}

void CropSession::applyCommitPlacementRotation(ImageItem *item) const
{
    if (!item) {
        return;
    }
    ItemComponents::Placement pl = item->placement();
    pl.rotation = currentRotation();
    item->applyPlacement(pl);
}

QSize CropSession::ensureApplyIntrinsicSize(ImageItem *item, qreal cropW, qreal cropH,
                                            const QString &pathForLog)
{
    if (!item) {
        return {};
    }
    QSize isz = item->imageSize();
    if (isz.width() <= 1 || isz.height() <= 1) {
        qCritical("applyCropCommit: layoutSize after crop is %dx%d (draft %gx%g path=%s)",
                  isz.width(), isz.height(), cropW, cropH, qPrintable(pathForLog));
        isz = ContentXform::roundedSizeAtLeast1(cropW, cropH);
        item->setIntrinsicSize(isz);
    }
    return isz;
}

bool CropSession::appearanceHasCrop(const WorkspaceItemState *app, bool haveApp)
{
    return haveApp && app && app->hasCrop && !app->cropRect.isEmpty();
}

CropSession::ApplyHostStatus CropSession::classifyApplyHost(const QImage &host,
                                                            bool hostFromCache,
                                                            bool hasApplied,
                                                            const ContentXform::Value &applied)
{
    if (!host.isNull()) {
        return ApplyHostStatus::Ok;
    }
    if (!hostFromCache && hasApplied && applied.hasCrop) {
        return ApplyHostStatus::NeedFull;
    }
    return ApplyHostStatus::NoPixels;
}


SessionAppearance::PixelKind CropSession::applyPixelKind(bool multiMp)
{
    return multiMp ? SessionAppearance::PixelKind::SoftPreview
                   : SessionAppearance::PixelKind::FullSource;
}


bool CropSession::shouldRequestFullOnNullEnter(bool hadPriorCrop, const QString &path)
{
    return hadPriorCrop && !path.isEmpty();
}

void CropSession::finishHandleDrag(const QRectF &contentRect)
{
    if (contentRect.isValid() && !contentRect.isEmpty()) {
        endHandleDragClamped(contentRect);
    } else {
        endHandleDrag();
    }
}


void CropSession::applyItemPlacementFromState(ImageItem *item,
                                              const WorkspaceItemState &app,
                                              bool imageModeZeroPose)
{
    if (!item) {
        return;
    }
    ItemComponents::Placement pl = item->placement();
    if (imageModeZeroPose) {
        pl.rotation = 0.0;
        pl.shear = 0.0;
    } else {
        pl.rotation = app.rotation;
        pl.shear = app.shear;
    }
    pl.hFlip = false;
    pl.vFlip = false;
    item->applyPlacement(pl);
}

bool CropSession::locksPath(const QString &path) const
{
    if (!draftSampleFrozen || path.isEmpty()) {
        return false;
    }
    if (!draftPath.isEmpty() && path == draftPath) {
        return true;
    }
    if (targetItem && targetItem->path() == path) {
        return true;
    }
    return false;
}

bool CropSession::locksItem(const ImageItem *item) const
{
    if (!draftSampleFrozen || !item) {
        return false;
    }
    if (targetItem && item == targetItem) {
        return true;
    }
    if (targetId != kInvalidSessionImageId
        && item->sessionId() == targetId) {
        return true;
    }
    if (!item->path().isEmpty() && locksPath(item->path())) {
        return true;
    }
    return false;
}

void CropSession::ensureRectValid(const QRectF &contentRect)
{
    if (!hasValidRect()) {
        setRect(contentRect);
        setRotation(0.0);
        return;
    }
    setRect(normalizedRect());
    if (isAllowExpand()) {
        QRectF r = currentRect();
        if (r.width() < 1.0) {
            r.setWidth(1.0);
        }
        if (r.height() < 1.0) {
            r.setHeight(1.0);
        }
        setRect(r);
        return;
    }
    setRect(CropGeometry::constrainToContent(
        currentRect(), currentRotation(), contentRect, 1.0));
}

QPolygonF CropSession::polygonLocal() const
{
    return CropGeometry::rotatedCorners(
        currentRect().normalized(), currentRotation());
}

void CropSession::initRectFromPriorAppearance(const QRectF &contentRect,
                                              const QPointF &itemOffset,
                                              const QSize &imageSize,
                                              const WorkspaceItemState *app,
                                              bool haveApp)
{
    setAllowExpand(false);

    const QRect priorCrop = (haveApp && app) ? app->cropRect : QRect();
    const bool hadCrop = haveApp && app && app->hasCrop && !priorCrop.isEmpty();

    if (hadCrop) {
        const QRect bounds(0, 0, imageSize.width(), imageSize.height());
        const QRect prior = SessionAppearance::scaleCropRect(
            priorCrop.normalized(), app->cropSourceSize, imageSize);
        setRotation(app ? app->cropRotation : 0.0);
        if (prior.width() <= 1 || prior.height() <= 1) {
            qCritical("initCropRect: prior crop scaled to %dx%d (stored %dx%d "
                      "sourceSize %dx%d live imageSize %dx%d) — draft will be 1×1",
                      prior.width(), prior.height(),
                      priorCrop.width(), priorCrop.height(),
                      app->cropSourceSize.width(), app->cropSourceSize.height(),
                      imageSize.width(), imageSize.height());
        }
        if (prior.width() >= 1 && prior.height() >= 1) {
            setRect(QRectF(prior.x() + itemOffset.x(), prior.y() + itemOffset.y(),
                           prior.width(), prior.height()));
            if (CropGeometry::priorDraftNeedsExpand(
                    QRectF(prior), QRectF(bounds), currentRect(), currentRotation(),
                    contentRect)) {
                setAllowExpand(true);
            }
        } else {
            setRect(contentRect);
            setRotation(0.0);
        }
    } else {
        setRect(contentRect);
        setRotation(0.0);
    }
    ensureRectValid(contentRect);
}

void CropSession::restoreStashedPlacement(ImageItem *item) const
{
    if (!item || !hasStashedPlacement()) {
        return;
    }
    ItemComponents::Placement pl = item->placement();
    pl.rotation = stashedPlacementRotation;
    pl.shear = stashedPlacementShear;
    item->applyPlacement(pl);
}

void CropSession::setRectFromSourcePixelTrim(const QRectF &contentRect,
                                             const QSize &srcSize,
                                             const QRect &trimmed)
{
    if (srcSize.width() < 1 || srcSize.height() < 1 || contentRect.width() < 1.0
        || contentRect.height() < 1.0 || !trimmed.isValid() || trimmed.isEmpty()) {
        return;
    }
    const qreal invSx = contentRect.width() / qreal(srcSize.width());
    const qreal invSy = contentRect.height() / qreal(srcSize.height());
    setRect(QRectF(contentRect.left() + trimmed.x() * invSx,
                   contentRect.top() + trimmed.y() * invSy,
                   trimmed.width() * invSx,
                   trimmed.height() * invSy));
    setRotation(0.0);
    setAllowExpand(false);
}

void CropSession::beginEnterSession(ImageItem *item, const QImage &enterSrc,
                                    const WorkspaceItemState &enterSt,
                                    bool snapshotValid)
{
    if (!item) {
        return;
    }
    bindTarget(item, item->sessionId(), item->path());
    setEnterSnapshot(enterSrc, enterSt, snapshotValid);
    {
        const ItemComponents::Placement pl = item->placement();
        stashPlacement(pl.rotation, pl.shear);
    }
    if (hasStashedPlacement()) {
        ItemComponents::Placement pl = item->placement();
        pl.rotation = 0.0;
        pl.shear = 0.0;
        item->applyPlacement(pl);
    }
    // Tile LOD suppress is owned by CropController via DisplayPipelineController
    // (Stage 2: CropSession does not touch item tile mutators).
}


QRect CropSession::sourceSearchRectFromDraft(const QRectF &contentRect,
                                             const QSize &srcSize) const
{
    if (srcSize.width() < 1 || srcSize.height() < 1
        || contentRect.width() < 1.0 || contentRect.height() < 1.0
        || !hasValidRect()) {
        return {};
    }
    const qreal sx = qreal(srcSize.width()) / contentRect.width();
    const qreal sy = qreal(srcSize.height()) / contentRect.height();
    const QRectF d = currentRect();
    QRect search(
        int(qFloor((d.left() - contentRect.left()) * sx)),
        int(qFloor((d.top() - contentRect.top()) * sy)),
        int(qCeil(d.width() * sx)),
        int(qCeil(d.height() * sy)));
    return search.intersected(QRect(0, 0, srcSize.width(), srcSize.height()));
}

bool CropSession::seedRotationFromStashedPlacement(qreal freeRotationEps)
{
    if (!isNearZeroRotation(freeRotationEps)) {
        return false;
    }
    if (qAbs(stashedPlacementRotation) <= freeRotationEps) {
        return false;
    }
    setRotation(stashedPlacementRotation);
    normalizeRotation();
    return true;
}

void CropSession::restoreEnterPlacementPose(ImageItem *item) const
{
    if (!item || !isEnterValid()) {
        return;
    }
    ItemComponents::Placement pl = item->placement();
    pl.pos = enterPos();
    pl.scale = enterScaleX();
    pl.scaleY = enterScaleY() > 0.0 ? enterScaleY() : pl.scale;
    item->applyPlacement(pl);
}

QRectF CropSession::expandLimits(const QRectF &contentRect) const
{
    if (!isAllowExpand()) {
        return contentRect;
    }
    return contentRect.adjusted(-contentRect.width() * 4, -contentRect.height() * 4,
                                contentRect.width() * 4, contentRect.height() * 4);
}

void CropSession::applyMoveDrag(const QPointF &local, const QRectF &contentRect)
{
    const QPointF delta = local - dragStartLocal;
    QRectF r = dragStartRect.translated(delta);
    if (!isAllowExpand()) {
        r = r.normalized();
        r = CropGeometry::translateInside(r, currentRotation(), contentRect);
    }
    setRect(r);
}

void CropSession::applyRotateDrag(const QPointF &local, const QRectF &contentRect, qreal minSide,
                                  bool shiftSnap, bool ctrlSnap)
{
    setRotation(CropGeometry::rotationFromDrag(
        local, dragStartRect.center(), rotateStartRotation, rotateStartAngle,
        shiftSnap, ctrlSnap));
    if (!isAllowExpand()) {
        setRect(CropGeometry::constrainToContent(
            dragStartRect, currentRotation(), contentRect, minSide));
    }
}

void CropSession::applyResizeDrag(const QPointF &local, const QRectF &contentRect,
                                  const QRectF &limits, qreal minSide,
                                  bool fromCenter, bool forceSquare)
{
    QRectF r = CropGeometry::resizeDraftRect(
        activeHandle, local, dragStartRect, currentRotation(), minSide,
        fromCenter, forceSquare);
    if (isAllowExpand()) {
        r = r.intersected(limits);
        if (r.width() < minSide) {
            r.setWidth(minSide);
        }
        if (r.height() < minSide) {
            r.setHeight(minSide);
        }
        setRect(r);
    } else {
        setRect(CropGeometry::constrainToContent(r, currentRotation(), contentRect, minSide));
    }
}

QRect CropSession::integerCropForOffset(const QPointF &itemOffset) const
{
    return CropGeometry::integerCropFromLocal(currentRect(), itemOffset);
}

void CropSession::beginRubberDraft(const QPointF &originLocal)
{
    beginRubber(originLocal);
    setRect(QRectF(originLocal, QSizeF(0, 0)));
    setRotation(0.0);
}

void CropSession::applyRubberBand(const QPointF &local, const QRectF &contentRect,
                                  bool shiftSnap, bool ctrlFromCenter)
{
    QRectF r = CropGeometry::rubberBandRect(
        rubberOriginLocal, local, shiftSnap, ctrlFromCenter);
    if (r.width() < 1.0) {
        r.setWidth(1.0);
    }
    if (r.height() < 1.0) {
        r.setHeight(1.0);
    }
    setRect(isAllowExpand() ? r : r.intersected(contentRect));
}

void CropSession::resetDraftToContent(const QRectF &contentRect)
{
    setRect(contentRect);
    setRotation(0.0);
    ensureRectValid(contentRect);
}

void CropSession::beginHandleDrag(CropHandle h, const QRectF &startRect, const QPointF &startLocal)
{
    activeHandle = h;
    dragStartRect = startRect;
    dragStartLocal = startLocal;
    if (h == CropHandle::Rotate) {
        setRotateStart(currentRotation(),
                       PlacementLinear::angleAbout(startRect.center(), startLocal));
    }
}

QRectF CropSession::clampLocalCrop(const QRectF &local, const QRectF &contentRect) const
{
    QRectF r = local.normalized();
    if (!isAllowExpand()) {
        r = r.intersected(contentRect);
    }
    if (r.width() < 1.0 || r.height() < 1.0) {
        return {};
    }
    return r;
}

QRect CropSession::sourceCropFromLocal(const QRectF &local, const QPointF &itemOffset,
                                       int imageW, int imageH, bool hFlip, bool vFlip) const
{
    return CropGeometry::flipAwareSourceCrop(
        CropGeometry::integerCropFromLocal(local, itemOffset),
        imageW, imageH, hFlip, vFlip);
}

void CropSession::applyActiveHandleDrag(const QPointF &local, const QRectF &contentRect,
                                        qreal minSide, bool shiftSnap, bool ctrlSnap)
{
    if (activeHandle == CropHandle::Move) {
        applyMoveDrag(local, contentRect);
        return;
    }
    if (activeHandle == CropHandle::Rotate) {
        applyRotateDrag(local, contentRect, minSide, shiftSnap, ctrlSnap);
        return;
    }
    applyResizeDrag(local, contentRect, expandLimits(contentRect), minSide, ctrlSnap, shiftSnap);
}

QSize CropSession::draftPixelSize() const
{
    return ContentXform::roundedSizeAtLeast1(currentRect().width(), currentRect().height());
}
