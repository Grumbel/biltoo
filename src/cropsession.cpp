// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cropsession.h"
#include "cropgeometry.h"
#include "contentxform.h"
#include "placementlinear.h"
#include "imageitem.h"
#include "sessionappearance.h"
#include "imagecache.h"
#include "thumtoocache.h"

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

QImage CropSession::pickApplyHost(ImageItem *item, const QString &path, bool *fromCache)
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

bool CropSession::canKeepDisplayForEnter(const ImageItem *item,
                                         const ContentXform::Value &wantX,
                                         const WorkspaceItemState &contentOnly,
                                         bool hadPriorCrop, bool needGeomBake)
{
    if (!item || hadPriorCrop || !item->hasDisplayPixels() || item->sessionHasCrop()) {
        return false;
    }
    if (item->displayPixelLongEdge() < ContentXform::kGuiMaterializeMaxEdge) {
        return false;
    }
    const bool appliedOk = item->hasAppliedContentXform()
        && !item->appliedContentXform().hasCrop
        && ContentXform::equal(item->appliedContentXform(), wantX);
    const bool liveGradeOk = !item->hasAppliedContentXform()
        && !needGeomBake
        && item->colorAdjustments().matches(contentOnly.colorAdjust);
    return appliedOk || liveGradeOk;
}

void CropSession::clearItemFreePlacementForDraft(ImageItem *item)
{
    if (!item) {
        return;
    }
    item->setItemRotation(0.0);
    item->setItemShear(0.0);
    item->setItemHFlip(false);
    item->setItemVFlip(false);
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
    item->setItemRotation(stashedPlacementRotation);
    item->setItemShear(stashedPlacementShear);
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
    stashPlacement(item->itemRotation(), item->itemShear());
    if (hasStashedPlacement()) {
        item->setItemRotation(0.0);
        item->setItemShear(0.0);
    }
}

void CropSession::releaseTargetTileLod()
{
    if (targetItem) {
        targetItem->setTileLodSuppressed(false);
    }
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
    item->setPos(enterPos());
    const qreal sx = enterScaleX();
    const qreal sy = enterScaleY() > 0.0 ? enterScaleY() : sx;
    item->setItemScale(sx, sy);
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
