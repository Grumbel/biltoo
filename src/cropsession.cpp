// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cropsession.h"
#include "imageitem.h"
#include "sessionappearance.h"

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
