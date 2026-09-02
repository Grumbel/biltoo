// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include <QGuiApplication>
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QTransform>
#include <QUndoCommand>
#include <QUndoStack>

namespace {
// Crop chrome under the frame: Expand alone on the left; Reset/Cancel/Apply
// right-aligned to the crop edges (same idea as edge resize handles). Larger
// buttons so labels fit rounded corners; y clears the bottom rotate knobs.
struct CropButtonLayout {
    QRect expand;
    QRect reset;
    QRect cancel;
    QRect apply;
    bool valid = false;
};

CropButtonLayout cropButtonLayout(const QRectF &cropView, const QRect &viewportRect)
{
    CropButtonLayout L;
    if (!cropView.isValid() || !viewportRect.isValid()) {
        return L;
    }
    constexpr int kW = 70;
    constexpr int kH = 28;
    constexpr int kGap = 6;
    // Bottom rotate knobs sit ~22px outside the edge; clear them plus air.
    constexpr int kOutsideGap = 32;
    constexpr int kInsideInset = 10;
    constexpr int kMargin = 6;
    constexpr int kGroupGapMin = 18; // min air between Expand and the right group

    const int rightGroupW = kW * 3 + kGap * 2;

    auto pickY = [&](int spanLeft, int spanW) -> int {
        const int yOutside = qRound(cropView.bottom()) + kOutsideGap;
        const int yInside = qRound(cropView.bottom()) - kH - kInsideInset;
        auto fullyVisible = [&](int y) {
            return viewportRect.contains(QRect(spanLeft, y, spanW, kH));
        };
        if (fullyVisible(yOutside)) {
            return yOutside;
        }
        if (fullyVisible(yInside)) {
            return yInside;
        }
        return qBound(viewportRect.top() + kMargin,
                      yOutside,
                      viewportRect.bottom() - kMargin - kH);
    };

    // Right group: align Apply to crop right edge.
    int right = qRound(cropView.right()) - kW;
    right = qBound(viewportRect.left() + kMargin + rightGroupW - kW,
                   right,
                   viewportRect.right() - kMargin - kW);
    const int cancelX = right - kGap - kW;
    const int resetX = cancelX - kGap - kW;
    const int yRight = pickY(resetX, rightGroupW);

    // Left: Expand alone, aligned to crop left edge.
    int expandX = qRound(cropView.left());
    expandX = qBound(viewportRect.left() + kMargin,
                     expandX,
                     viewportRect.right() - kMargin - kW);
    if (expandX + kW + kGroupGapMin > resetX) {
        expandX = qMax(viewportRect.left() + kMargin,
                       resetX - kGroupGapMin - kW);
    }
    const int yLeft = pickY(expandX, kW);
    // Prefer a shared baseline when both bands land near the same y.
    const int yExpand = (qAbs(yLeft - yRight) <= 2) ? yRight : yLeft;
    const int yGroup = yRight;

    L.expand = QRect(expandX, yExpand, kW, kH);
    L.reset = QRect(resetX, yGroup, kW, kH);
    L.cancel = QRect(cancelX, yGroup, kW, kH);
    L.apply = QRect(right, yGroup, kW, kH);
    L.valid = true;
    return L;
}
} // namespace


ImageItem *ImageView::cropTargetItem() const
{
    if (isGalleryMode()) {
        return nullptr;
    }
    if (ImageItem *t = targetItem()) {
        if (t->hasDecodedPixels() || !t->pixmap().isNull()) {
            return t;
        }
    }
    return primaryItem();
}


namespace {

QPolygonF rotatedCropCorners(const QRectF &rect, qreal degrees)
{
    const QPointF c = rect.center();
    QTransform tr;
    tr.translate(c.x(), c.y());
    tr.rotate(degrees);
    tr.translate(-c.x(), -c.y());
    QPolygonF poly;
    poly << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
    return tr.map(poly);
}

bool pointInsideBounds(const QPointF &p, const QRectF &bounds)
{
    return p.x() >= bounds.left() && p.x() <= bounds.right()
        && p.y() >= bounds.top() && p.y() <= bounds.bottom();
}

bool cropCornersInside(const QRectF &rect, qreal degrees, const QRectF &bounds)
{
    const QPolygonF poly = rotatedCropCorners(rect, degrees);
    for (const QPointF &pt : poly) {
        if (!pointInsideBounds(pt, bounds)) {
            return false;
        }
    }
    return true;
}

QRectF translateCropInside(QRectF rect, qreal degrees, const QRectF &bounds)
{
    for (int pass = 0; pass < 6; ++pass) {
        const QPolygonF poly = rotatedCropCorners(rect, degrees);
        qreal dx = 0.0;
        qreal dy = 0.0;
        for (const QPointF &pt : poly) {
            if (pt.x() < bounds.left()) {
                dx = qMax(dx, bounds.left() - pt.x());
            } else if (pt.x() > bounds.right()) {
                dx = qMin(dx, bounds.right() - pt.x());
            }
            if (pt.y() < bounds.top()) {
                dy = qMax(dy, bounds.top() - pt.y());
            } else if (pt.y() > bounds.bottom()) {
                dy = qMin(dy, bounds.bottom() - pt.y());
            }
        }
        if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy)) {
            break;
        }
        rect.translate(dx, dy);
    }
    return rect;
}

QRectF shrinkCropInside(QRectF rect, qreal degrees, const QRectF &bounds, qreal minSide)
{
    if (cropCornersInside(rect, degrees, bounds)) {
        return rect;
    }
    const QPointF c = rect.center();
    qreal lo = 0.0;
    qreal hi = 1.0;
    QRectF best(c.x() - minSide / 2.0, c.y() - minSide / 2.0, minSide, minSide);
    for (int i = 0; i < 18; ++i) {
        const qreal mid = (lo + hi) * 0.5;
        QRectF r(0, 0, rect.width() * mid, rect.height() * mid);
        r.moveCenter(c);
        if (r.width() < minSide || r.height() < minSide) {
            hi = mid;
            continue;
        }
        if (cropCornersInside(r, degrees, bounds)) {
            best = r;
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return best;
}

QRectF constrainCropToContent(QRectF rect, qreal degrees, const QRectF &bounds,
                              qreal minSide)
{
    rect = rect.normalized();
    if (rect.width() < minSide) {
        rect.setWidth(minSide);
    }
    if (rect.height() < minSide) {
        rect.setHeight(minSide);
    }
    if (qAbs(degrees) < 0.05) {
        rect = rect.intersected(bounds);
        if (rect.width() < minSide) {
            rect.setWidth(minSide);
        }
        if (rect.height() < minSide) {
            rect.setHeight(minSide);
        }
        if (rect.right() > bounds.right()) {
            rect.moveRight(bounds.right());
        }
        if (rect.bottom() > bounds.bottom()) {
            rect.moveBottom(bounds.bottom());
        }
        if (rect.left() < bounds.left()) {
            rect.moveLeft(bounds.left());
        }
        if (rect.top() < bounds.top()) {
            rect.moveTop(bounds.top());
        }
        return rect;
    }
    rect = translateCropInside(rect, degrees, bounds);
    if (!cropCornersInside(rect, degrees, bounds)) {
        rect = shrinkCropInside(rect, degrees, bounds, minSide);
        rect = translateCropInside(rect, degrees, bounds);
    }
    return rect;
}

} // namespace

void ImageView::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QRectF cr = item->contentRect();
    if (!m_cropRect.isValid() || m_cropRect.isEmpty()) {
        m_cropRect = cr;
        m_cropRotation = 0.0;
        return;
    }
    m_cropRect = m_cropRect.normalized();
    if (m_cropAllowExpand) {
        if (m_cropRect.width() < 1.0) {
            m_cropRect.setWidth(1.0);
        }
        if (m_cropRect.height() < 1.0) {
            m_cropRect.setHeight(1.0);
        }
        return;
    }
    m_cropRect = constrainCropToContent(m_cropRect, m_cropRotation, cr, 1.0);
}

void ImageView::alignCropFrameCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item || !m_cropRect.isValid()) {
        return;
    }
    const QPointF current = item->mapToScene(m_cropRect.center());
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

void ImageView::setCropMode(bool on)
{
    if (on == m_cropMode) {
        return;
    }
    if (on) {
        if (isGalleryMode()) {
            return;
        }
        // Crop edits one image only; multi-select must not silently pick the first.
        if (!hasSingleCropTarget()) {
            flashHud(tr("Crop"), tr("Select a single image"));
            return;
        }
        ImageItem *item = cropTargetItem();
        if (!item || (!item->hasDecodedPixels() && item->pixmap().isNull())) {
            flashHud(tr("Crop"), tr("No image"));
            return;
        }
        cancelZoomRegion();
        // Snapshot appearance before full-image reload so Close can be undone.
        m_cropEnterSource = item->sourceImage().copy();
        m_cropEnterState = captureState(item);
        m_cropEnterState.hasCrop = item->sessionHasCrop();
        m_cropEnterState.cropRect = item->sessionCropRect();
        // cropRotation / cropSourceSize come from captureState → appearance.
        m_cropEnterValid = !m_cropEnterSource.isNull();
        // Crop handles are axis-aligned in item space; free Workspace placement
        // rotation makes rubber-band and edge grips unusable. Unrotate for the
        // crop session and restore on exit.
        // Workspace: remember where the *displayed* image centre sits so the
        // restored crop frame can stay fixed while the full image grows around it.
        const QPointF workspaceAnchorScene = item->mapToScene(QPointF(0.0, 0.0));
        m_cropStashedPlacementRotation = item->itemRotation();
        m_cropHadStashedPlacement = qAbs(m_cropStashedPlacementRotation) > 0.05;
        if (m_cropHadStashedPlacement) {
            item->setItemRotation(0.0);
        }
        if (!prepareCropModeFullImage(item)) {
            m_cropEnterValid = false;
            m_cropEnterSource = QImage();
            if (m_cropHadStashedPlacement) {
                item->setItemRotation(m_cropStashedPlacementRotation);
            }
            m_cropHadStashedPlacement = false;
            flashHud(tr("Crop"), tr("Could not load full image"));
            return;
        }
        if (isWorkspaceMode()) {
            // If there was no stored crop angle but the tile was free-rotated,
            // seed the draft rotation so the frame matches the prior pose while
            // the item stays axis-aligned for editing.
            if (qAbs(m_cropRotation) < 0.05
                && qAbs(m_cropStashedPlacementRotation) > 0.05) {
                m_cropRotation = m_cropStashedPlacementRotation;
                while (m_cropRotation > 180.0) {
                    m_cropRotation -= 360.0;
                }
                while (m_cropRotation <= -180.0) {
                    m_cropRotation += 360.0;
                }
                ensureCropRectValid();
            }
            alignCropFrameCenterToScene(item, workspaceAnchorScene);
            updateWorkspaceSceneRect();
        }
        m_cropMode = true;
        m_cropActiveHandle = CropHandle::None;
        m_cropHoverHandle = CropHandle::None;
        m_cropRubberBanding = false;
        flashHud(tr("Crop mode"),
                 tr("Apply commits · Esc cancels"));
        emit cropModeChanged(true);
        emit statusChanged();
        viewport()->update();
        return;
    }
    // Turning crop off from the toolbar commits the draft (auto-apply).
    leaveCropModeInternal(true);
}

bool ImageView::prepareCropModeFullImage(ImageItem *item)
{
    if (!item) {
        return false;
    }
    const QString path = item->path();
    // Always edit against the full on-disk image so the crop region can grow.
    const QImage full = ImageLoader::load(path);
    if (full.isNull()) {
        return false;
    }

    // Prior crop + content flags for *this* session image only — never path map alone.
    WorkspaceItemState app;
    bool haveApp = false;
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : m_currentSessionId;
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = *it;
            haveApp = true;
        }
    }
    if (!haveApp && item->sessionHasCrop()) {
        app.hasCrop = true;
        app.cropRect = item->sessionCropRect();
        app.contentHFlip = item->contentHFlip();
        app.contentVFlip = item->contentVFlip();
        haveApp = true;
    }
    if (!haveApp) {
        // Last resort for unbound single-instance tiles.
        const auto it = m_itemStates.constFind(path);
        if (it != m_itemStates.cend()) {
            app = *it;
            haveApp = true;
        }
    }

    const QRect priorCrop = (haveApp && app.hasCrop) ? app.cropRect : QRect();
    const bool hadCrop = haveApp && app.hasCrop && !priorCrop.isEmpty();

    item->setSourceImage(full);
    // Always axis-aligned while cropping (placement was stashed in setCropMode).
    item->setItemRotation(0.0);
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    // Re-apply content flips/quarter turns for this session image on the full frame
    // (crop draft is drawn in that space). Do not bake the crop yet.
    if (haveApp) {
        WorkspaceItemState contentOnly = app;
        contentOnly.hasCrop = false;
        contentOnly.cropRect = QRect();
        applyContentBakes(item, contentOnly);
    }
    m_cropShowingFullImage = true;

    // Start each crop session without Expand; re-enable below if the stored
    // draft (AABB or rotated corners) extends outside the source.
    m_cropAllowExpand = false;

    const QRectF cr = item->contentRect();
    if (hadCrop) {
        const QSize sz = item->imageSize();
        const QRect bounds(0, 0, sz.width(), sz.height());
        // cropRect is stored in the same space as the post-content-bake image
        // (recordSessionCrop runs with item flips cleared). Scale if the live
        // size differs from the size at record time — same rule as applyCrop.
        const QRect prior = SessionAppearance::scaleCropRect(
            priorCrop.normalized(), app.cropSourceSize, sz);
        m_cropRotation = haveApp ? app.cropRotation : 0.0;
        if (prior.width() >= 1 && prior.height() >= 1) {
            const QPointF off = item->offset();
            // Do not mirror for contentHFlip/VFlip: applyContentBakes already
            // put pixels in content-oriented space and the stored rect is in
            // that space. Re-mirroring shifted the frame on re-entry.
            m_cropRect = QRectF(prior.x() + off.x(), prior.y() + off.y(),
                                prior.width(), prior.height());
            // Expand is not persisted. Detect both axis-aligned overflow and
            // rotated-corner overflow so ensureCropRectValid does not translate
            // a previously applied rotated draft to a new centre.
            const bool aabbOutside =
                prior.left() < bounds.left() || prior.top() < bounds.top()
                || prior.right() > bounds.right()
                || prior.bottom() > bounds.bottom();
            const bool rotatedOutside =
                qAbs(m_cropRotation) > 0.05
                && !cropCornersInside(m_cropRect, m_cropRotation, cr);
            if (aabbOutside || rotatedOutside) {
                m_cropAllowExpand = true;
            }
        } else {
            m_cropRect = cr;
            m_cropRotation = 0.0;
        }
    } else {
        m_cropRect = cr;
        m_cropRotation = 0.0;
    }
    ensureCropRectValid();

    if (isImageMode()) {
        m_fitMode = true;
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    return true;
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
        : m_currentSessionId;
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
        const auto it = m_itemStates.constFind(item->path());
        if (it == m_itemStates.cend()) {
            return;
        }
        app = *it;
        have = true;
    }
    const QImage full = ImageLoader::load(item->path());
    if (!full.isNull()) {
        item->setSourceImage(full);
    }
    if (isImageMode()) {
        item->setItemRotation(0.0);
    } else {
        item->setItemRotation(app.rotation);
    }
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    applySessionCrop(item, app);
    applyContentBakes(item, app);
    item->setSessionCrop(app.hasCrop, app.cropRect);
    if (isImageMode()) {
        m_fitMode = true;
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}

void ImageView::toggleCropMode()
{
    setCropMode(!m_cropMode);
}

void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    if (!src.isNull()) {
        item->setSourceImage(src);
    }
    item->setSessionCrop(state.hasCrop, state.cropRect);
    item->setContentHFlip(state.contentHFlip);
    item->setContentVFlip(state.contentVFlip);
    applyState(item, state);
    // Seed appearance with the full state (including cropRotation) before
    // commitItemSessionEdit, which rebuilds the slot via captureState.
    {
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_currentSessionId;
        }
        if (sid != kInvalidSessionImageId) {
            WorkspaceItemState slot = state;
            slot.sessionId = sid;
            slot.path = item->path();
            m_appearance.set(sid, slot);
        } else {
            m_itemStates.insert(item->path(), state);
        }
    }
    // Appearance persistence is commitItemSessionEdit → m_appearance (by id).
    // Do not write crop state into the path map for bound tiles.
    commitItemSessionEdit(item);
    if (isImageMode()) {
        m_fitMode = true;
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}

void ImageView::applyCrop()
{
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
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = &(*it);
        }
        // Bound session image with no appearance entry = full frame.
    } else {
        // Path map only when unbound (no session image id).
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend()) {
            fallback = *it;
            app = &fallback;
        }
    }
    if (!app) {
        return;
    }
    if (!app->hasCrop && !app->contentHFlip && !app->contentVFlip
        && app->contentQuarterTurns == 0) {
        item->setSessionCrop(false, QRect());
        return;
    }
    applySessionCrop(item, *app);
    applyContentBakes(item, *app);
    item->setSessionCrop(app->hasCrop, app->cropRect);
}

void ImageView::applySessionCrop(ImageItem *item, const WorkspaceItemState &state)
{
    SessionAppearance::applyCrop(item, state);
}

void ImageView::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    if (!item) {
        return;
    }
    const QRectF cr = item->contentRect();
    QRectF local = localCrop.normalized();
    if (!m_cropAllowExpand) {
        local = local.intersected(cr);
    }
    if (local.width() < 1.0 || local.height() < 1.0) {
        return;
    }
    const QPointF off = item->offset();
    // Crop mode always edits the full on-disk image — store absolute source rect.
    int dx = qRound(local.left() - off.x());
    int dy = qRound(local.top() - off.y());
    int dw = qMax(1, qRound(local.width()));
    int dh = qMax(1, qRound(local.height()));
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    const int iw = item->imageSize().width();
    const int ih = item->imageSize().height();
    if (item->itemHFlip()) {
        dx = iw - dx - dw;
    }
    if (item->itemVFlip()) {
        dy = ih - dy - dh;
    }
    const QRect disp(dx, dy, dw, dh);

    WorkspaceItemState s = captureState(item);
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : m_currentSessionId;
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
    if (fullFrame && qAbs(m_cropRotation) < 0.05) {
        s.hasCrop = false;
        s.cropRect = QRect();
        s.cropSourceSize = QSize();
        s.cropRotation = 0.0;
    } else {
        s.hasCrop = true;
        s.cropRect = disp;
        s.cropSourceSize = QSize(iw, ih);
        s.cropRotation = m_cropRotation;
    }
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    if (sid != kInvalidSessionImageId) {
        m_appearance.set(sid, s);
    } else {
        // Unbound only: path map is the sole store.
        m_itemStates.insert(item->path(), s);
    }
}

void ImageView::leaveCropModeInternal(bool apply)
{
    if (!m_cropMode) {
        return;
    }
    ImageItem *item = cropTargetItem();
    // When a Workspace crop is committed, placement rotation follows the crop
    // frame angle (straightened pixels + matching pose). Cancel / full-frame
    // restore the pre-crop placement instead.
    bool preserveCropFrameRotation = false;
    if (apply && item) {
        ensureCropRectValid();
        const QRectF full = item->contentRect();
        const bool fullFrame =
            !m_cropRect.isValid()
            || (qAbs(m_cropRect.left() - full.left()) < 0.5
                && qAbs(m_cropRect.top() - full.top()) < 0.5
                && qAbs(m_cropRect.width() - full.width()) < 0.5
                && qAbs(m_cropRect.height() - full.height()) < 0.5);
        // Record absolute crop (or clear it) while the full image is still loaded.
        recordSessionCrop(item, m_cropRect.isValid() ? m_cropRect : full);
        if (!fullFrame) {
            // Scene position of the crop-frame centre — new pixels stay here.
            const QPointF cropSceneCenter = item->mapToScene(m_cropRect.center());
            if (item->cropToLocalRect(m_cropRect, backgroundColor(), m_cropRotation)) {
                // Keep stashed Gallery tiles: commitItemSessionEdit peer-syncs
                // cropped pixels. Invalidating forced a full-size probe + pack
                // then a crop decode without repack → tiny tiles on return.
                if (isImageMode()) {
                    m_fitMode = true;
                    fitItem(item, currentFitAspectMode());
                } else if (isWorkspaceMode()) {
                    // New image is centred on the item origin; pin that to the
                    // former crop-frame centre so the region does not jump.
                    alignItemCenterToScene(item, cropSceneCenter);
                    // Straightened crop pixels: place at the crop-frame angle so
                    // the region keeps the same orientation it had while editing
                    // (crop painter samples with -θ; frame was drawn at +θ).
                    item->setItemRotation(m_cropRotation);
                    preserveCropFrameRotation = true;
                    updateWorkspaceSceneRect();
                } else if (isGalleryMode()) {
                    applyLayout(GalleryPackReason::ContentChange);
                }
                commitItemSessionEdit(item);
                // Undo: restore pre-crop-mode appearance + session crop metadata.
                if (m_undoStack && m_cropEnterValid) {
                    class CropCommand : public QUndoCommand {
                    public:
                        CropCommand(ImageView *view, ImageItem *item,
                                    const QImage &beforeSrc, const QImage &afterSrc,
                                    const WorkspaceItemState &beforeSt,
                                    const WorkspaceItemState &afterSt)
                            : m_view(view)
                            , m_item(item)
                            , m_beforeSrc(beforeSrc)
                            , m_afterSrc(afterSrc)
                            , m_beforeSt(beforeSt)
                            , m_afterSt(afterSt)
                        {
                            setText(QObject::tr("Crop"));
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
                        this, item, m_cropEnterSource, item->sourceImage().copy(),
                        m_cropEnterState, afterSt));
                }
                flashHud(tr("Cropped"),
                         QStringLiteral("%1×%2")
                             .arg(item->imageSize().width())
                             .arg(item->imageSize().height()));
            }
        } else {
            // Reset / full frame: keep full pixels; clear session crop metadata.
            if (isImageMode()) {
                m_fitMode = true;
                fitItem(item, currentFitAspectMode());
            } else if (isWorkspaceMode()) {
                // Drop the enter-time crop-frame offset; restore pre-crop pose.
                if (m_cropEnterValid) {
                    item->setPos(m_cropEnterState.pos);
                    item->setItemScale(m_cropEnterState.scale,
                                       m_cropEnterState.scaleY > 0.0
                                           ? m_cropEnterState.scaleY
                                           : m_cropEnterState.scale);
                }
                updateWorkspaceSceneRect();
            } else if (isGalleryMode()) {
                applyLayout(GalleryPackReason::ContentChange);
            }
            commitItemSessionEdit(item);
            if (m_undoStack && m_cropEnterValid
                && (m_cropEnterState.hasCrop
                    || m_cropEnterSource.size() != item->sourceImage().size())) {
                class CropCommand : public QUndoCommand {
                public:
                    CropCommand(ImageView *view, ImageItem *item,
                                const QImage &beforeSrc, const QImage &afterSrc,
                                const WorkspaceItemState &beforeSt,
                                const WorkspaceItemState &afterSt)
                        : m_view(view)
                        , m_item(item)
                        , m_beforeSrc(beforeSrc)
                        , m_afterSrc(afterSrc)
                        , m_beforeSt(beforeSt)
                        , m_afterSt(afterSt)
                    {
                        setText(QObject::tr("Crop reset"));
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
                    this, item, m_cropEnterSource, item->sourceImage().copy(),
                    m_cropEnterState, afterSt));
            }
            flashHud(tr("Crop reset"), tr("Full image"));
        }
    } else if (item && m_cropShowingFullImage) {
        // Esc / toggle off: put the previous session crop back on the canvas.
        restoreSessionCropAppearance(item);
        if (isWorkspaceMode() && m_cropEnterValid) {
            item->setPos(m_cropEnterState.pos);
            item->setItemScale(m_cropEnterState.scale,
                               m_cropEnterState.scaleY > 0.0
                                   ? m_cropEnterState.scaleY
                                   : m_cropEnterState.scale);
        }
    }
    // Restore pre-crop placement rotation unless Apply already set it from the
    // crop frame (Workspace non-full-frame commit).
    if (item && m_cropHadStashedPlacement && !preserveCropFrameRotation) {
        item->setItemRotation(m_cropStashedPlacementRotation);
    }
    m_cropHadStashedPlacement = false;
    m_cropStashedPlacementRotation = 0.0;
    m_cropMode = false;
    m_cropShowingFullImage = false;
    m_cropEnterValid = false;
    m_cropEnterSource = QImage();
    m_cropRect = QRectF();
    m_cropActiveHandle = CropHandle::None;
    m_cropHoverHandle = CropHandle::None;
    m_cropAllowExpand = false;
    m_cropRotation = 0.0;
    m_cropRubberBanding = false;
    emit cropModeChanged(false);
    emit statusChanged();
    viewport()->unsetCursor();
    viewport()->update();
}

QPolygonF ImageView::cropPolygonItemLocal() const
{
    const QRectF r = m_cropRect.normalized();
    QPolygonF poly;
    poly << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
    if (qAbs(m_cropRotation) < 0.05) {
        return poly;
    }
    const QPointF c = r.center();
    QTransform tr;
    tr.translate(c.x(), c.y());
    tr.rotate(m_cropRotation);
    tr.translate(-c.x(), -c.y());
    return tr.map(poly);
}

QRectF ImageView::cropRectView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRect.isValid()) {
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
    if (!m_cropMode) {
        return {};
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.expand : QRect();
}

QRect ImageView::cropResetButtonView() const
{
    if (!m_cropMode) {
        return {};
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.reset : QRect();
}

QRect ImageView::cropCancelButtonView() const
{
    if (!m_cropMode) {
        return {};
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.cancel : QRect();
}

QRect ImageView::cropCloseButtonView() const
{
    if (!m_cropMode) {
        return {};
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    return L.valid ? L.apply : QRect();
}



void ImageView::paintCropOverlay(QPainter &painter)
{
    if (!m_cropMode) {
        return;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRect.isValid()) {
        return;
    }
    ensureCropRectValid();
    const QRectF contentScene = item->mapToScene(item->contentRect()).boundingRect();
    const QPolygonF cropLocal = cropPolygonItemLocal();
    const QPolygonF cropScenePoly = item->mapToScene(cropLocal);
    QPolygonF cropViewPoly;
    for (const QPointF &sp : cropScenePoly) {
        cropViewPoly << QPointF(mapFromScene(sp));
    }
    const QRect contentView = QRect(mapFromScene(contentScene.topLeft()),
                                    mapFromScene(contentScene.bottomRight()))
                                  .normalized();
    const QRect cropView = cropViewPoly.boundingRect().toRect().normalized();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Dim everything outside the (possibly rotated) crop.
    QPainterPath outer;
    outer.addRect(QRectF(viewport()->rect()));
    QPainterPath hole;
    hole.addPolygon(cropViewPoly);
    hole.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawPath(outer.subtracted(hole));

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

    // Bold corner + edge bars at rotated corners (poly order: TL, TR, BR, BL).
    const QPointF tl = cropViewPoly.at(0);
    const QPointF tr = cropViewPoly.at(1);
    const QPointF br = cropViewPoly.at(2);
    const QPointF bl = cropViewPoly.at(3);
    const qreal hs = 14.0;
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB,
                          CropHandle h) {
        const bool hot = (m_cropHoverHandle == h || m_cropActiveHandle == h);
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
        const bool hot = (m_cropHoverHandle == h || m_cropActiveHandle == h);
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

    // Rotate knobs on each side (outward from edge midpoints).
    {
        const QPointF centre = (tl + tr + br + bl) * 0.25;
        const bool hot = (m_cropHoverHandle == CropHandle::Rotate
                          || m_cropActiveHandle == CropHandle::Rotate);
        auto drawRotateKnob = [&](QPointF mid, QPointF edgeAlong) {
            qreal alen = qHypot(edgeAlong.x(), edgeAlong.y());
            if (alen > 1e-6) {
                edgeAlong /= alen;
            }
            QPointF outward(-edgeAlong.y(), edgeAlong.x());
            if (QPointF::dotProduct(outward, mid - centre) < 0) {
                outward = -outward;
            }
            const QPointF knob = mid + outward * 22.0;
            QPen stem(hot ? QColor(255, 255, 255) : QColor(255, 190, 40), 0);
            stem.setCosmetic(true);
            stem.setWidthF(hot ? 1.8 : 1.3);
            painter.setPen(stem);
            painter.drawLine(mid, knob);
            painter.setBrush(hot ? QColor(255, 220, 80) : QColor(255, 190, 40));
            painter.drawEllipse(knob, hot ? 6.0 : 5.0, hot ? 6.0 : 5.0);
            painter.setBrush(Qt::NoBrush);
        };
        drawRotateKnob((tl + tr) / 2.0, tr - tl);
        drawRotateKnob((tr + br) / 2.0, br - tr);
        drawRotateKnob((br + bl) / 2.0, bl - br);
        drawRotateKnob((bl + tl) / 2.0, tl - bl);
    }

    // Move grip at centre (interior of the crop starts a rubber-band, not Move).
    {
        const QPointF centre = (tl + tr + br + bl) * 0.25;
        const bool hot = (m_cropHoverHandle == CropHandle::Move
                          || m_cropActiveHandle == CropHandle::Move);
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

    // Controls: outside below crop when possible, inside if off-screen.
    // Same design language as Workspace chrome (HANDLES.md):
    //   toggle  = rounded square / stronger on-state
    //   action  = dark + accent ring
    //   neutral = grey (Cancel)
    //   commit  = filled accent (Apply)
    enum class CropBtnRole { Toggle, Action, Neutral, Commit };
    auto drawTextButton = [&](const QRect &btn, CropHandle kind, const QString &label,
                              CropBtnRole role, bool toggled = false) {
        if (!btn.isValid()) {
            return;
        }
        const bool hover = (m_cropHoverHandle == kind);
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
        f.setPointSize(qMax(9, f.pointSize() + 1));
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(btn, Qt::AlignCenter, label);
    };
    // Local QPoint names must not hide QObject::tr — use ImageView::tr.
    drawTextButton(cropExpandButtonView(), CropHandle::ExpandToggle,
                   ImageView::tr("Expand"), CropBtnRole::Toggle, m_cropAllowExpand);
    drawTextButton(cropResetButtonView(), CropHandle::Reset, ImageView::tr("Reset"),
                   CropBtnRole::Action);
    drawTextButton(cropCancelButtonView(), CropHandle::Cancel, ImageView::tr("Cancel"),
                   CropBtnRole::Neutral);
    drawTextButton(cropCloseButtonView(), CropHandle::Close, ImageView::tr("Apply"),
                   CropBtnRole::Commit);

    // Crop size in image pixels (same coordinate space as the draft rect).
    const int cropW = qMax(1, qRound(m_cropRect.width()));
    const int cropH = qMax(1, qRound(m_cropRect.height()));
    const QString sizeLabel = QStringLiteral("%1×%2").arg(cropW).arg(cropH);
    {
        QFont f = painter.font();
        f.setPointSize(qMax(9, f.pointSize()));
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

    Q_UNUSED(contentView);
    painter.restore();
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || h == CropHandle::None || h == CropHandle::Reset || h == CropHandle::Close
        || h == CropHandle::Cancel || h == CropHandle::ExpandToggle) {
        return;
    }
    m_cropActiveHandle = h;
    m_cropDragStartRect = m_cropRect;
    m_cropDragStartLocal = item->mapFromScene(mapToScene(viewPos));
    if (h == CropHandle::Rotate) {
        m_cropRotateStartRotation = m_cropRotation;
        const QPointF c = m_cropRect.center();
        const QPointF v = m_cropDragStartLocal - c;
        m_cropRotateStartAngle = qRadiansToDegrees(qAtan2(v.y(), v.x()));
    }
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || m_cropActiveHandle == CropHandle::None) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const QRectF cr = item->contentRect();
    const QRectF limits = m_cropAllowExpand
        ? cr.adjusted(-cr.width() * 4, -cr.height() * 4, cr.width() * 4, cr.height() * 4)
        : cr;
    const qreal minSide = 4.0;
    const bool fromCenter =
        QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    const bool forceSquare =
        QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    const QPointF c0 = m_cropDragStartRect.center();
    const qreal w0 = m_cropDragStartRect.width();
    const qreal h0 = m_cropDragStartRect.height();
    const qreal ang = m_cropRotation;

    auto rotateVec = [](QPointF v, qreal degrees) {
        QTransform tr;
        tr.rotate(degrees);
        return tr.map(v);
    };

    auto clampMove = [&](QRectF rect) {
        if (m_cropAllowExpand) {
            return rect;
        }
        return constrainCropToContent(rect, ang, cr, minSide);
    };


    if (m_cropActiveHandle == CropHandle::Move) {
        const QPointF delta = local - m_cropDragStartLocal;
        QRectF r = m_cropDragStartRect.translated(delta);
        m_cropRect = clampMove(r);
        viewport()->update();
        return;
    }

    if (m_cropActiveHandle == CropHandle::Rotate) {
        const QPointF v = local - c0;
        const qreal angle = qRadiansToDegrees(qAtan2(v.y(), v.x()));
        m_cropRotation = m_cropRotateStartRotation + (angle - m_cropRotateStartAngle);
        while (m_cropRotation > 180.0) {
            m_cropRotation -= 360.0;
        }
        while (m_cropRotation <= -180.0) {
            m_cropRotation += 360.0;
        }
        // Ctrl → 45° (includes 90°); Shift (alone or with Ctrl) → 15°.
        const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
        if (mods & Qt::ShiftModifier) {
            m_cropRotation = qRound(m_cropRotation / 15.0) * 15.0;
        } else if (mods & Qt::ControlModifier) {
            m_cropRotation = qRound(m_cropRotation / 45.0) * 45.0;
        }
        if (!m_cropAllowExpand) {
            m_cropRect = constrainCropToContent(m_cropDragStartRect, m_cropRotation, cr,
                                                minSide);
        }
        viewport()->update();
        return;
    }

    // Resize in crop-local axes (axis-aligned about start centre), then map
    // the new centre back through the crop rotation so edges stay under the
    // grips when the frame is rotated.
    const QPointF pLocal = rotateVec(local - c0, -ang);
    qreal L = -w0 / 2.0;
    qreal R = w0 / 2.0;
    qreal T = -h0 / 2.0;
    qreal B = h0 / 2.0;

    const bool left = (m_cropActiveHandle == CropHandle::Left
                       || m_cropActiveHandle == CropHandle::TopLeft
                       || m_cropActiveHandle == CropHandle::BottomLeft);
    const bool right = (m_cropActiveHandle == CropHandle::Right
                        || m_cropActiveHandle == CropHandle::TopRight
                        || m_cropActiveHandle == CropHandle::BottomRight);
    const bool top = (m_cropActiveHandle == CropHandle::Top
                      || m_cropActiveHandle == CropHandle::TopLeft
                      || m_cropActiveHandle == CropHandle::TopRight);
    const bool bottom = (m_cropActiveHandle == CropHandle::Bottom
                         || m_cropActiveHandle == CropHandle::BottomLeft
                         || m_cropActiveHandle == CropHandle::BottomRight);

    if (fromCenter) {
        if (left || right) {
            const qreal half = qMax(minSide / 2.0, qAbs(pLocal.x()));
            L = -half;
            R = half;
        }
        if (top || bottom) {
            const qreal half = qMax(minSide / 2.0, qAbs(pLocal.y()));
            T = -half;
            B = half;
        }
    } else {
        if (left) {
            L = qMin(pLocal.x(), R - minSide);
        }
        if (right) {
            R = qMax(pLocal.x(), L + minSide);
        }
        if (top) {
            T = qMin(pLocal.y(), B - minSide);
        }
        if (bottom) {
            B = qMax(pLocal.y(), T + minSide);
        }
    }

    if (forceSquare) {
        qreal side = qMax(R - L, B - T);
        if (fromCenter) {
            const qreal half = side / 2.0;
            L = -half;
            R = half;
            T = -half;
            B = half;
        } else {
            // Grow from the fixed corner/edge toward the dragged side.
            if (right && !left) {
                R = L + side;
            } else if (left && !right) {
                L = R - side;
            } else {
                const qreal cx = (L + R) / 2.0;
                L = cx - side / 2.0;
                R = cx + side / 2.0;
            }
            if (bottom && !top) {
                B = T + side;
            } else if (top && !bottom) {
                T = B - side;
            } else {
                const qreal cy = (T + B) / 2.0;
                T = cy - side / 2.0;
                B = cy + side / 2.0;
            }
        }
    }

    const QPointF cLocal((L + R) / 2.0, (T + B) / 2.0);
    const qreal newW = R - L;
    const qreal newH = B - T;
    const QPointF c1 = c0 + rotateVec(cLocal, ang);
    QRectF r(c1.x() - newW / 2.0, c1.y() - newH / 2.0, newW, newH);

    if (m_cropAllowExpand) {
        r = r.intersected(limits);
        if (r.width() < minSide) {
            r.setWidth(minSide);
        }
        if (r.height() < minSide) {
            r.setHeight(minSide);
        }
        m_cropRect = r;
    } else {
        m_cropRect = constrainCropToContent(r, ang, cr, minSide);
    }
    viewport()->update();
}

void ImageView::endCropHandleDrag()
{
    m_cropActiveHandle = CropHandle::None;
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
    m_cropRubberBanding = true;
    m_cropRubberOriginLocal = local;
    m_cropRect = QRectF(local, QSizeF(0, 0));
    m_cropRotation = 0.0; // new rubber-band is axis-aligned
    viewport()->update();
}

void ImageView::updateCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRubberBanding) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const QRectF cr = item->contentRect();
    const QPointF origin = m_cropRubberOriginLocal;
    QRectF r = QRectF(origin, local).normalized();
    if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
        const qreal side = qMax(qAbs(local.x() - origin.x()), qAbs(local.y() - origin.y()));
        const qreal dx = (local.x() >= origin.x()) ? side : -side;
        const qreal dy = (local.y() >= origin.y()) ? side : -side;
        r = QRectF(origin, origin + QPointF(dx, dy)).normalized();
    }
    if (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) {
        // Expand about the press point (centre of the new rect).
        const QPointF c = origin;
        const qreal halfW = qMax(2.0, qAbs(local.x() - c.x()));
        const qreal halfH = qMax(2.0, qAbs(local.y() - c.y()));
        if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
            const qreal half = qMax(halfW, halfH);
            r = QRectF(c - QPointF(half, half), QSizeF(2 * half, 2 * half));
        } else {
            r = QRectF(c - QPointF(halfW, halfH), QSizeF(2 * halfW, 2 * halfH));
        }
    }
    if (!m_cropAllowExpand) {
        r = r.intersected(cr);
    }
    if (r.width() < 1.0) {
        r.setWidth(1.0);
    }
    if (r.height() < 1.0) {
        r.setHeight(1.0);
    }
    m_cropRect = m_cropAllowExpand ? r : r.intersected(cr);
    viewport()->update();
}

void ImageView::endCropRubberBand()
{
    m_cropRubberBanding = false;
    ensureCropRectValid();
    viewport()->update();
}

ImageView::CropHandle ImageView::cropHandleAt(const QPoint &viewPos) const
{
    if (!m_cropMode) {
        return CropHandle::None;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRect.isValid()) {
        return CropHandle::None;
    }
    // Controls sit near the crop frame (checked before edge handles).
    const QRect expandBtn = cropExpandButtonView();
    if (expandBtn.contains(viewPos)) {
        return CropHandle::ExpandToggle;
    }
    const QRect resetBtn = cropResetButtonView();
    if (resetBtn.contains(viewPos)) {
        return CropHandle::Reset;
    }
    const QRect cancelBtn = cropCancelButtonView();
    if (cancelBtn.contains(viewPos)) {
        return CropHandle::Cancel;
    }
    const QRect closeBtn = cropCloseButtonView();
    if (closeBtn.contains(viewPos)) {
        return CropHandle::Close;
    }
    // Map rotated crop corners through item → scene → view.
    const QPolygonF localPoly = cropPolygonItemLocal();
    auto toView = [this, item](const QPointF &local) {
        return mapFromScene(item->mapToScene(local));
    };
    const QPoint tl = toView(localPoly.at(0));
    const QPoint tr = toView(localPoly.at(1));
    const QPoint br = toView(localPoly.at(2));
    const QPoint bl = toView(localPoly.at(3));
    const QPoint tm((tl.x() + tr.x()) / 2, (tl.y() + tr.y()) / 2);
    const QPoint bm((bl.x() + br.x()) / 2, (bl.y() + br.y()) / 2);
    const QPoint lm((tl.x() + bl.x()) / 2, (tl.y() + bl.y()) / 2);
    const QPoint rm((tr.x() + br.x()) / 2, (tr.y() + br.y()) / 2);
    const QPointF centre((tl.x() + tr.x() + br.x() + bl.x()) * 0.25,
                         (tl.y() + tr.y() + br.y() + bl.y()) * 0.25);
    auto rotateKnobAt = [&](QPointF mid, QPointF edgeAlong) -> QPoint {
        qreal alen = qHypot(edgeAlong.x(), edgeAlong.y());
        if (alen > 1e-6) {
            edgeAlong /= alen;
        }
        QPointF outward(-edgeAlong.y(), edgeAlong.x());
        if (QPointF::dotProduct(outward, mid - centre) < 0) {
            outward = -outward;
        }
        return (mid + outward * 22.0).toPoint();
    };
    const QPoint rotTop = rotateKnobAt(QPointF(tm), QPointF(tr - tl));
    const QPoint rotRight = rotateKnobAt(QPointF(rm), QPointF(br - tr));
    const QPoint rotBottom = rotateKnobAt(QPointF(bm), QPointF(bl - br));
    const QPoint rotLeft = rotateKnobAt(QPointF(lm), QPointF(tl - bl));

    constexpr qreal kHit = 16.0;
    auto near = [&](const QPoint &p) {
        return QLineF(viewPos, p).length() <= kHit;
    };
    if (near(rotTop) || near(rotRight) || near(rotBottom) || near(rotLeft)) {
        return CropHandle::Rotate;
    }
    if (near(tl)) {
        return CropHandle::TopLeft;
    }
    if (near(tr)) {
        return CropHandle::TopRight;
    }
    if (near(bl)) {
        return CropHandle::BottomLeft;
    }
    if (near(br)) {
        return CropHandle::BottomRight;
    }
    if (near(tm)) {
        return CropHandle::Top;
    }
    if (near(bm)) {
        return CropHandle::Bottom;
    }
    if (near(lm)) {
        return CropHandle::Left;
    }
    if (near(rm)) {
        return CropHandle::Right;
    }
    // Move: only the centre grip (hit ~matches painted size). Frame interior
    // stays free so a rubber-band crop can start there (None → mouse path).
    const QPoint moveGrip = centre.toPoint();
    if (QLineF(viewPos, moveGrip).length() <= 12.0) {
        return CropHandle::Move;
    }
    return CropHandle::None;
}
