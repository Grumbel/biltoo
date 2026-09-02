// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include <QGuiApplication>
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"

#include <QPainter>
#include <QUndoCommand>
#include <QUndoStack>

namespace {
// Shared layout for Reset + Apply: prefer outside below the crop rect; only
// pull inside when the outside placement would leave the viewport.
struct CropButtonLayout {
    int x0 = 0;
    int y = 0;
    int w = 56;
    int h = 22;
    int gap = 6;
    bool valid = false;
};

CropButtonLayout cropButtonLayout(const QRectF &cropView, const QRect &viewportRect)
{
    CropButtonLayout L;
    if (!cropView.isValid() || !viewportRect.isValid()) {
        return L;
    }
    constexpr int kW = 56;
    constexpr int kH = 22;
    constexpr int kGap = 5;
    constexpr int kOutsideGap = 8;
    constexpr int kInsideInset = 8;
    constexpr int kMargin = 6;

    const int totalW = kW * 4 + kGap * 3;
    int x0 = qRound(cropView.center().x() - totalW / 2.0);
    // Prefer outside, centred under the crop bottom edge.
    int yOutside = qRound(cropView.bottom()) + kOutsideGap;
    // Fallback: inside the crop, bottom-centred.
    int yInside = qRound(cropView.bottom()) - kH - kInsideInset;

    // Clamp horizontally into the viewport so labels stay reachable.
    const int minX = viewportRect.left() + kMargin;
    const int maxX = viewportRect.right() - kMargin - totalW;
    if (maxX >= minX) {
        x0 = qBound(minX, x0, maxX);
    } else {
        x0 = minX;
    }

    auto fullyVisible = [&](int y) {
        const QRect row(x0, y, totalW, kH);
        return viewportRect.contains(row);
    };

    int y = yOutside;
    if (!fullyVisible(yOutside)) {
        // Outside would clip — try inside the crop frame.
        if (fullyVisible(yInside)) {
            y = yInside;
        } else {
            // Still not fully visible: clamp the row into the viewport.
            y = qBound(viewportRect.top() + kMargin,
                       yOutside,
                       viewportRect.bottom() - kMargin - kH);
            // Prefer inside if that clamp is closer to the crop bottom interior.
            if (cropView.height() > kH + 2 * kInsideInset
                && viewportRect.intersects(QRect(x0, yInside, totalW, kH))) {
                const int yClampedInside = qBound(viewportRect.top() + kMargin,
                                                  yInside,
                                                  viewportRect.bottom() - kMargin - kH);
                // Use the placement that keeps the row fully on-screen.
                if (fullyVisible(yClampedInside)) {
                    y = yClampedInside;
                } else if (fullyVisible(y)) {
                    // keep clamped outside
                } else {
                    y = yClampedInside;
                }
            }
        }
    }

    L.x0 = x0;
    L.y = y;
    L.w = kW;
    L.h = kH;
    L.gap = kGap;
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

void ImageView::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QRectF cr = item->contentRect();
    if (!m_cropRect.isValid() || m_cropRect.isEmpty()) {
        m_cropRect = cr;
        return;
    }
    m_cropRect = m_cropRect.normalized();
    if (m_cropRect.width() < 1.0) {
        m_cropRect.setWidth(1.0);
    }
    if (m_cropRect.height() < 1.0) {
        m_cropRect.setHeight(1.0);
    }
    if (m_cropAllowExpand) {
        // Outside the image is intentional (pad on apply); only keep min size.
        return;
    }
    m_cropRect = m_cropRect.intersected(cr);
    if (m_cropRect.width() < 1.0) {
        m_cropRect.setWidth(1.0);
    }
    if (m_cropRect.height() < 1.0) {
        m_cropRect.setHeight(1.0);
    }
    // Keep inside content after min-size clamp.
    if (m_cropRect.right() > cr.right()) {
        m_cropRect.moveRight(cr.right());
    }
    if (m_cropRect.bottom() > cr.bottom()) {
        m_cropRect.moveBottom(cr.bottom());
    }
    if (m_cropRect.left() < cr.left()) {
        m_cropRect.moveLeft(cr.left());
    }
    if (m_cropRect.top() < cr.top()) {
        m_cropRect.moveTop(cr.top());
    }
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
        // Snapshot appearance before full-image reload so Apply can be undone.
        m_cropEnterSource = item->sourceImage().copy();
        m_cropEnterState = captureState(item);
        m_cropEnterState.hasCrop = item->sessionHasCrop();
        m_cropEnterState.cropRect = item->sessionCropRect();
        m_cropEnterValid = !m_cropEnterSource.isNull();
        // Crop handles are axis-aligned in item space; free Workspace placement
        // rotation makes rubber-band and edge grips unusable. Unrotate for the
        // crop session and restore on exit.
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
        m_cropMode = true;
        m_cropActiveHandle = CropHandle::None;
        m_cropHoverHandle = CropHandle::None;
        m_cropRubberBanding = false;
        flashHud(tr("Crop mode"),
                 m_cropHadStashedPlacement
                     ? tr("Unrotated for crop · Close commits · Esc cancels")
                     : tr("Close commits · Cancel / Esc discards"));
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
    const bool hFlip = haveApp && (app.contentHFlip || app.hFlip);
    const bool vFlip = haveApp && (app.contentVFlip || app.vFlip);

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

    const QRectF cr = item->contentRect();
    if (hadCrop) {
        const QSize sz = item->imageSize();
        const QRect bounds(0, 0, sz.width(), sz.height());
        const QRect prior = priorCrop.normalized();
        // Expand crops are stored outside the source bounds; restore Expand mode
        // so ensureCropRectValid does not clamp them back into the image.
        const bool extendsOutside =
            prior.left() < bounds.left() || prior.top() < bounds.top()
            || prior.right() > bounds.right() || prior.bottom() > bounds.bottom();
        if (extendsOutside) {
            m_cropAllowExpand = true;
        }
        if (prior.width() >= 1 && prior.height() >= 1) {
            const QPointF off = item->offset();
            int dx = prior.x();
            int dy = prior.y();
            int dw = prior.width();
            int dh = prior.height();
            if (hFlip) {
                dx = sz.width() - dx - dw;
            }
            if (vFlip) {
                dy = sz.height() - dy - dh;
            }
            m_cropRect = QRectF(dx + off.x(), dy + off.y(), dw, dh);
        } else {
            m_cropRect = cr;
        }
    } else {
        m_cropRect = cr;
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
    if (fullFrame) {
        s.hasCrop = false;
        s.cropRect = QRect();
        s.cropSourceSize = QSize();
    } else {
        s.hasCrop = true;
        s.cropRect = disp;
        s.cropSourceSize = QSize(iw, ih);
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
            if (item->cropToLocalRect(m_cropRect, backgroundColor())) {
                // Keep stashed Gallery tiles: commitItemSessionEdit peer-syncs
                // cropped pixels. Invalidating forced a full-size probe + pack
                // then a crop decode without repack → tiny tiles on return.
                if (isImageMode()) {
                    m_fitMode = true;
                    fitItem(item, currentFitAspectMode());
                } else if (isWorkspaceMode()) {
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
                m_undoStack->push(new CropCommand(
                    this, item, m_cropEnterSource, item->sourceImage().copy(),
                    m_cropEnterState, afterSt));
            }
            flashHud(tr("Crop reset"), tr("Full image"));
        }
    } else if (item && m_cropShowingFullImage) {
        // Esc / toggle off: put the previous session crop back on the canvas.
        restoreSessionCropAppearance(item);
    }
    // Restore Workspace free placement rotation after crop UI.
    if (item && m_cropHadStashedPlacement) {
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
    m_cropRubberBanding = false;
    emit cropModeChanged(false);
    emit statusChanged();
    viewport()->unsetCursor();
    viewport()->update();
}

QRectF ImageView::cropRectView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRect.isValid()) {
        return QRectF();
    }
    const QPolygonF poly = item->mapToScene(m_cropRect);
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
    if (!L.valid) {
        return {};
    }
    return QRect(L.x0, L.y, L.w, L.h);
}

QRect ImageView::cropResetButtonView() const
{
    if (!m_cropMode) {
        return {};
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    if (!L.valid) {
        return {};
    }
    return QRect(L.x0 + L.w + L.gap, L.y, L.w, L.h);
}

QRect ImageView::cropCancelButtonView() const
{
    if (!m_cropMode) {
        return {};
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    if (!L.valid) {
        return {};
    }
    return QRect(L.x0 + 2 * (L.w + L.gap), L.y, L.w, L.h);
}

QRect ImageView::cropCloseButtonView() const
{
    if (!m_cropMode) {
        return {};
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    if (!L.valid) {
        return {};
    }
    return QRect(L.x0 + 3 * (L.w + L.gap), L.y, L.w, L.h);
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
    const QRectF cropScene = item->mapToScene(m_cropRect).boundingRect();
    const QRect contentView = QRect(mapFromScene(contentScene.topLeft()),
                                    mapFromScene(contentScene.bottomRight()))
                                  .normalized();
    const QRect cropView = QRect(mapFromScene(cropScene.topLeft()),
                                 mapFromScene(cropScene.bottomRight()))
                               .normalized();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Dim everything outside the crop (viewport-space; covers outside content too).
    QPainterPath outer;
    outer.addRect(QRectF(viewport()->rect()));
    QPainterPath hole;
    hole.addRect(QRectF(cropView));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawPath(outer.subtracted(hole));

    // Crop frame — amber family (distinct from single-select blue / group violet).
    painter.setBrush(Qt::NoBrush);
    QPen frame(QColor(255, 190, 40, 240), 0);
    frame.setCosmetic(true);
    frame.setWidthF(1.75);
    painter.setPen(frame);
    painter.drawRect(cropView);
    QPen dash(QColor(40, 30, 10, 180), 0, Qt::DashLine);
    dash.setCosmetic(true);
    dash.setWidthF(1.0);
    painter.setPen(dash);
    painter.drawRect(cropView.adjusted(1, 1, -1, -1));

    // Bold corner (line-arc-line) + edge bars — same language as Workspace chrome.
    const QPoint tl = cropView.topLeft();
    const QPoint tr = cropView.topRight();
    const QPoint bl = cropView.bottomLeft();
    const QPoint br = cropView.bottomRight();
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
    // along directions point along the crop edges away from the corner.
    drawCorner(tl, QPointF(1, 0), QPointF(0, 1), CropHandle::TopLeft);
    drawCorner(tr, QPointF(-1, 0), QPointF(0, 1), CropHandle::TopRight);
    drawCorner(bl, QPointF(1, 0), QPointF(0, -1), CropHandle::BottomLeft);
    drawCorner(br, QPointF(-1, 0), QPointF(0, -1), CropHandle::BottomRight);

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
        QPen hp(hot ? QColor(255, 255, 255) : QColor(120, 80, 10), 0);
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
    drawEdgeBar(QPointF((tl.x() + tr.x()) / 2.0, tl.y()), QPointF(1, 0), CropHandle::Top);
    drawEdgeBar(QPointF((bl.x() + br.x()) / 2.0, bl.y()), QPointF(1, 0), CropHandle::Bottom);
    drawEdgeBar(QPointF(tl.x(), (tl.y() + bl.y()) / 2.0), QPointF(0, 1), CropHandle::Left);
    drawEdgeBar(QPointF(tr.x(), (tr.y() + br.y()) / 2.0), QPointF(0, 1), CropHandle::Right);

    // Reset / Apply: outside below crop when possible, inside if off-screen.
    auto drawTextButton = [&](const QRect &btn, CropHandle kind, const QString &label,
                              bool primary) {
        if (!btn.isValid()) {
            return;
        }
        const bool hover = (m_cropHoverHandle == kind);
        painter.setPen(QPen(primary ? QColor(120, 80, 10) : QColor(40, 40, 40), 1.0));
        if (primary) {
            // Amber primary to match crop handle language.
            painter.setBrush(hover ? QColor(255, 210, 70, 255) : QColor(240, 175, 40, 245));
        } else {
            painter.setBrush(hover ? QColor(255, 245, 220, 255) : QColor(255, 255, 255, 230));
        }
        painter.drawRoundedRect(btn, 4, 4);
        painter.setPen(primary ? QColor(40, 25, 5) : QColor(30, 30, 30));
        QFont f = painter.font();
        f.setPointSize(qMax(8, f.pointSize()));
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(btn, Qt::AlignCenter, label);
    };
    // Local QPoint names must not hide QObject::tr — use ImageView::tr.
    // Label stays short so it fits the chrome button; On state uses primary fill.
    drawTextButton(cropExpandButtonView(), CropHandle::ExpandToggle,
                   ImageView::tr("Expand"), m_cropAllowExpand);
    drawTextButton(cropResetButtonView(), CropHandle::Reset, ImageView::tr("Reset"), false);
    drawTextButton(cropCancelButtonView(), CropHandle::Cancel, ImageView::tr("Cancel"), false);
    drawTextButton(cropCloseButtonView(), CropHandle::Close, ImageView::tr("Close"), true);

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
        int lx = cropView.center().x() - (tw + 2 * padX) / 2;
        int ly = cropView.top() - th - 2 * padY - 6;
        if (ly < 4) {
            ly = cropView.top() + 6;
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
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || m_cropActiveHandle == CropHandle::None) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const QRectF cr = item->contentRect();
    // When expand is off, edges stay inside the image; when on, use a large pad.
    const QRectF limits = m_cropAllowExpand
        ? cr.adjusted(-cr.width() * 4, -cr.height() * 4, cr.width() * 4, cr.height() * 4)
        : cr;
    QRectF r = m_cropDragStartRect;
    const qreal minSide = 4.0;
    const bool fromCenter =
        QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    const bool forceSquare =
        QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    const QPointF startCenter = m_cropDragStartRect.center();

    auto clampMove = [&](QRectF rect) {
        if (m_cropAllowExpand) {
            return rect; // free translate; pad fills the outside on apply
        }
        // Keep size; slide within content bounds.
        const qreal w = rect.width();
        const qreal h = rect.height();
        qreal x = rect.left();
        qreal y = rect.top();
        if (w >= cr.width()) {
            x = cr.left();
        } else {
            x = qBound(cr.left(), x, cr.right() - w);
        }
        if (h >= cr.height()) {
            y = cr.top();
        } else {
            y = qBound(cr.top(), y, cr.bottom() - h);
        }
        return QRectF(QPointF(x, y), QSizeF(w, h));
    };

    if (m_cropActiveHandle == CropHandle::Move) {
        const QPointF delta = local - m_cropDragStartLocal;
        r.translate(delta);
        m_cropRect = clampMove(r);
        viewport()->update();
        return;
    }

    // Edge / corner resize (optionally from centre, optionally square).
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
            const qreal half = qMax(minSide / 2.0, qAbs(local.x() - startCenter.x()));
            r.setLeft(startCenter.x() - half);
            r.setRight(startCenter.x() + half);
        }
        if (top || bottom) {
            const qreal half = qMax(minSide / 2.0, qAbs(local.y() - startCenter.y()));
            r.setTop(startCenter.y() - half);
            r.setBottom(startCenter.y() + half);
        }
    } else {
        if (left) {
            r.setLeft(qBound(limits.left(), local.x(), r.right() - minSide));
        }
        if (right) {
            r.setRight(qBound(r.left() + minSide, local.x(), limits.right()));
        }
        if (top) {
            r.setTop(qBound(limits.top(), local.y(), r.bottom() - minSide));
        }
        if (bottom) {
            r.setBottom(qBound(r.top() + minSide, local.y(), limits.bottom()));
        }
    }

    r = r.normalized();

    if (forceSquare) {
        // Match the larger side so the dragged edge stays under the cursor when possible.
        qreal side = qMax(r.width(), r.height());
        side = qMax(side, minSide);
        if (fromCenter) {
            const qreal half = side / 2.0;
            r = QRectF(startCenter - QPointF(half, half), QSizeF(side, side));
        } else {
            // Anchor opposite corner/edge of the start rect.
            QPointF anchor = startCenter;
            if (left && !right) {
                anchor.setX(m_cropDragStartRect.right());
            } else if (right && !left) {
                anchor.setX(m_cropDragStartRect.left());
            }
            if (top && !bottom) {
                anchor.setY(m_cropDragStartRect.bottom());
            } else if (bottom && !top) {
                anchor.setY(m_cropDragStartRect.top());
            }
            qreal x1 = anchor.x();
            qreal y1 = anchor.y();
            qreal x2 = left ? anchor.x() - side : (right ? anchor.x() + side : r.right());
            qreal y2 = top ? anchor.y() - side : (bottom ? anchor.y() + side : r.bottom());
            if (!left && !right) {
                x1 = r.center().x() - side / 2.0;
                x2 = x1 + side;
            }
            if (!top && !bottom) {
                y1 = r.center().y() - side / 2.0;
                y2 = y1 + side;
            }
            r = QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized();
            if (r.width() < minSide || r.height() < minSide) {
                r.setSize(QSizeF(qMax(r.width(), minSide), qMax(r.height(), minSide)));
            }
        }
    }

    // Clamp into allowed limits; prefer keeping the resize intent when possible.
    r = r.intersected(limits);
    if (r.width() < minSide) {
        if (left && !fromCenter) {
            r.setLeft(r.right() - minSide);
        } else if (right && !fromCenter) {
            r.setRight(r.left() + minSide);
        } else {
            r.setWidth(minSide);
        }
        r = r.intersected(cr);
    }
    if (r.height() < minSide) {
        if (top && !fromCenter) {
            r.setTop(r.bottom() - minSide);
        } else if (bottom && !fromCenter) {
            r.setBottom(r.top() + minSide);
        } else {
            r.setHeight(minSide);
        }
        r = r.intersected(cr);
    }

    m_cropRect = r.normalized().intersected(limits);
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
    // Map crop rect corners through item → scene → view (handles stay screen-sized).
    const QRectF r = m_cropRect;
    auto toView = [this, item](const QPointF &local) {
        return mapFromScene(item->mapToScene(local));
    };
    const QPoint tl = toView(r.topLeft());
    const QPoint tr = toView(r.topRight());
    const QPoint bl = toView(r.bottomLeft());
    const QPoint br = toView(r.bottomRight());
    const QPoint tm((tl.x() + tr.x()) / 2, (tl.y() + tr.y()) / 2);
    const QPoint bm((bl.x() + br.x()) / 2, (bl.y() + br.y()) / 2);
    const QPoint lm((tl.x() + bl.x()) / 2, (tl.y() + bl.y()) / 2);
    const QPoint rm((tr.x() + br.x()) / 2, (tr.y() + br.y()) / 2);

    constexpr qreal kHit = 16.0;
    auto near = [&](const QPoint &p) {
        return QLineF(viewPos, p).length() <= kHit;
    };
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
    // Interior of the crop frame: move (not resize). Outside → rubber-band.
    const QRectF viewCrop = QRectF(tl, br).normalized();
    // Inflate slightly negative so edge hit zones stay exclusive.
    if (viewCrop.adjusted(6, 6, -6, -6).contains(viewPos)) {
        return CropHandle::Move;
    }
    return CropHandle::None;
}
