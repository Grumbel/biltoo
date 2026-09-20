// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// View zoom, fit/fill, sticky zoom/pan, and image-mode framing.

#include "imageview.h"
#include "imageitem.h"
#include "itemcomponents.h"
#include "viewtransform.h"
#include "viewframing.h"

#include <QScrollBar>
#include "toolpolicy.h"
#include <QPointer>
#include <QTimer>
#include "contentxform.h"
#include "sessionappearance.h"

void ImageView::setStickyZoomEnabled(bool on)
{
    if (!m_framing.setStickyZoomEnabled(on)) {
        return;
    }
    emit stickyZoomChanged();
    emit statusChanged();
}


void ImageView::releaseStickyZoom()
{
    if (!m_framing.setStickyZoomEnabled(false)) {
        return;
    }
    emit stickyZoomChanged();
    emit statusChanged();
}


void ImageView::captureStickyZoomFromCurrentFraming()
{
    m_framing.syncStickyKindFromFitFill();
}


void ImageView::setStickyZoomKind(StickyZoomKind kind)
{
    m_framing.setStickyZoomKind(kind);
}


void ImageView::captureStickyPanAnchor(ImageItem *item)
{
    // Always sample scale + pan when leaving an image so free navigation
    // (no sticky Fit/Fill/1:1) can keep the same zoom and relative position.
    m_framing.clearStickyPan();
    m_framing.clearPreservedViewScale();
    if (!item || !viewport() || !m_scene) {
        return;
    }
    if (!m_items.contains(item) || item->scene() != m_scene) {
        return;
    }
    const qreal sx = transform().m11();
    if (qIsFinite(sx) && sx > 1e-6) {
        m_framing.setPreservedViewScale(sx);
    }
    const QRectF r = item->sceneBoundingRect();
    if (r.width() < 1.0 || r.height() < 1.0) {
        return;
    }
    const QPointF vc = mapToScene(viewport()->rect().center());
    m_framing.setStickyPanFromScene(vc, r);
}


void ImageView::restoreStickyPanAnchor(ImageItem *item)
{
    if (!m_framing.hasStickyPan() || !item || !m_scene || !viewport()) {
        return;
    }
    if (!m_items.contains(item) || item->scene() != m_scene) {
        return;
    }
    const QRectF r = item->sceneBoundingRect();
    if (r.width() < 1.0 || r.height() < 1.0) {
        return;
    }
    centerOn(m_framing.sceneFromStickyPan(r));
}


void ImageView::applyImageModeFraming(ImageItem *item)
{
    if (!item || !isImageMode()) {
        return;
    }
    if (m_framing.isStickyZoomEnabled()) {
        // Fit: unique home pose (centred). Fill / 1:1: frame, then best-effort
        // restore viewport centre in image-normalized coords (prev/next compare).
        // Always restore *after* setSceneRect/refreshScrollBarGeometry — those
        // often reset QAbstractScrollArea scroll position.
        switch (m_framing.currentStickyZoomKind()) {
        case StickyZoomKind::Fill:
            m_framing.setFillMode();
            fitItem(item, Qt::KeepAspectRatioByExpanding);
            break;
        case StickyZoomKind::Actual:
            m_framing.clearFitFill();
            {
                ItemComponents::Placement pl = item->placement();
                pl.scale = 1.0;
                pl.scaleY = 1.0;
                item->applyPlacement(pl);
            }
            resetTransform();
            centerOn(item);
            break;
        case StickyZoomKind::Fit:
        default:
            m_framing.setFitOnly();
            fitItem(item, Qt::KeepAspectRatio);
            break;
        }
        syncImageModeSceneRect(item);
        refreshScrollBarGeometry();
        if (!m_framing.isStickyFit()) {
            restoreStickyPanAnchor(item);
            // Scroll ranges often settle after this returns — restore again.
            // QPointer so a destroy mid-navigation cancels the callback safely.
            const QPointer<ImageView> guard(this);
            QTimer::singleShot(0, this, [guard]() {
                ImageView *const view = guard.data();
                if (!view || !view->m_framing.isStickyZoomEnabled()
                    || view->m_framing.isStickyFit()
                    || !view->m_scene || !view->viewport()) {
                    return;
                }
                if (ImageItem *cur = view->targetItem()) {
                    view->restoreStickyPanAnchor(cur);
                }
            });
        }
        return;
    }
    // Non-sticky: keep the previous view scale + relative pan (prev/next at the
    // same zoom). Cold open with no prior capture still defaults to Fit.
    if (m_framing.hasPreservedViewScale()) {
        m_framing.clearFitFill();
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        resetTransform();
        scale(m_framing.currentPreservedViewScale(), m_framing.currentPreservedViewScale());
        syncImageModeSceneRect(item);
        refreshScrollBarGeometry();
        restoreStickyPanAnchor(item);
        const QPointer<ImageView> guard(this);
        QTimer::singleShot(0, this, [guard]() {
            ImageView *const view = guard.data();
            if (!view || view->m_framing.isStickyZoomEnabled() || !view->m_scene
                || !view->viewport()) {
                return;
            }
            if (ImageItem *cur = view->targetItem()) {
                view->restoreStickyPanAnchor(cur);
            }
        });
        return;
    }
    m_framing.setFitOnly();
    fitItem(item, Qt::KeepAspectRatio);
}


void ImageView::cancelZoomRegion()
{
    m_zoomRegion.disarm();
    m_zoomRegion.hideRubber();
    if (!m_chrome.isPanning() && !m_itemInteract.isRotating()) {
        setCursor(ToolPolicy::cursorFor(m_tool));
    }
    emit statusChanged();
}


void ImageView::setImageModeLeftDragPan(bool on)
{
    m_chrome.setImageModeLeftDragPan(on);
}


void ImageView::preserveImageViewOnLogicalSizeChange(ImageItem *item,
                                                     const QSize &before,
                                                     const QSize &after)
{
    if (!item || !viewport()) {
        return;
    }
    if (!after.isValid() || after.width() < 1 || after.height() < 1) {
        return;
    }
    const bool beforeOk = before.isValid() && before.width() > 1 && before.height() > 1;
    const bool aspectShifted = !beforeOk || ContentXform::aspectChanged(before, after);
    if (aspectShifted) {
        if (m_slideshow.hud().isProgressActive() && m_slideshow.settings().isMotionOff()) {
            m_slideshow.applySlideshowZoomFraming(item);
        } else if (!m_slideshow.hud().isProgressActive()) {
            // Sticky Fill/1:1: reframe + restore pan (fitItem alone recentres).
            if (m_framing.isStickyZoomEnabled()) {
                applyImageModeFraming(item);
            } else {
                fitItem(item, currentFitAspectMode());
            }
        }
    } else if (beforeOk && before != after && !m_slideshow.hud().isProgressActive()) {
        // Same aspect, larger/smaller logical size: scale the view so the image
        // keeps the same on-screen footprint (soft→native must not zoom).
        // Slideshow pure-phase paints via m_slideshow.paintMotionCover(logical size) and
        // does not use the view matrix for framing.
        const qreal factor = ContentXform::footprintScaleFactor(before, after);
        if (factor > 0.0 && qIsFinite(factor) && !qFuzzyCompare(factor, 1.0)) {
            const QPointF sceneCenter = mapToScene(viewport()->rect().center());
            const QGraphicsView::ViewportAnchor saved =
                transformationAnchor();
            setTransformationAnchor(QGraphicsView::NoAnchor);
            // Scale about the viewport centre in scene space.
            QTransform t = transform();
            t.translate(sceneCenter.x(), sceneCenter.y());
            t.scale(factor, factor);
            t.translate(-sceneCenter.x(), -sceneCenter.y());
            setTransform(t, false);
            setTransformationAnchor(saved);
            centerOn(sceneCenter);
        }
    }
    syncImageModeSceneRect(item);
}


void ImageView::syncImageModeSceneRect(ImageItem *item)
{
    if (!item || !m_scene || !isImageMode()) {
        return;
    }
    // Tight margin only: Image mode pans the view when zoomed past fit, not a
    // free Workspace-style halo. Stale larger/null rects (mode switch, resize
    // fitItem without this, provisional size race) cause intermittent free or
    // asymmetric scroll range.
    const QRectF bounds = item->sceneBoundingRect().adjusted(-8, -8, 8, 8);
    if (!bounds.isValid() || bounds.isEmpty()) {
        return;
    }
    if (m_scene->sceneRect() != bounds) {
        m_scene->setSceneRect(bounds);
    }
}


void ImageView::fitItem(ImageItem *item, Qt::AspectRatioMode mode)
{
    if (!item) {
        return;
    }
    // docs/CROP_MODE.md: during crop draft, never layoutSize(file, want-with-crop).
    // DOMAIN.md ownership (Image mode):
    //   View matrix owns framing (fit / zoom / pan).
    //   Object keeps rotation and flips; this helper must never clear them.
    //   Object scale is normalized to 1 so residual Workspace scale does not
    //   fight the view transform when showing a single image.
    // Logical size owns geometry — soft display pixels must not define fit.
    //
    // Exception: a session crop bake (materializeDisplay / materializeDisplay) sets
    // intrinsic to the crop pixel size. Forcing full-file logicalSizeForPath here
    // immediately after Apply stretched the crop into the pre-crop box.
    //
    // Content ±90° turns: never force file-native size. After rotate, fitItem
    // used to reset intrinsic to unoriented native and paint stretched oriented
    // pixels into the old contentRect.
    const QString path = item->path();
    // While crop mode is active the draft is orient-only *full* frame
    // (docs/CROP_MODE.md). wantAppearance still has the stored crop — using it
    // here collapsed intrinsic to the old crop box right after enter (Image
    // mode calls fitItem; Workspace does not — that is why Workspace worked).
    // Crop draft: never layoutSize with store crop — that collapses the
    // full-frame draft to the old crop box.
    //
    // Critical: m_cropCtrl.session().active() stays true through applyCropCommit → fitItem, *after*
    // the crop bake is attached. Treating any m_cropCtrl.session().active() as draft forced
    // orient-only layout on top of crop pixels → stretch into the pre-crop
    // contentRect. Only pure draft (no applied crop, no session crop on the
    // item) is draft geometry.
    const bool cropDraft = CropSession::isDraftLayoutGeometry(
        m_cropCtrl.session().active(),
        item->hasAppliedContentXform() && item->appliedContentXform().hasCrop,
        item->sessionHasCrop());
    if (!path.isEmpty() && !item->sessionHasCrop() && !cropDraft) {
        const QSize fileNative = ensureLogicalSizeForPath(path);
        if (fileNative.isValid() && fileNative.width() > 1 && fileNative.height() > 1
            && !isProvisionalImageSize(path)) {
            const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
                ? item->sessionId()
                : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
            const WorkspaceItemState want = m_displayPipeline.wantAppearanceForItem(item, sid);
            const QSize lay = ContentXform::layoutSize(fileNative, want);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                item->setIntrinsicSize(lay);
            }
        }
    } else if (cropDraft && !path.isEmpty()) {
        const WorkspaceItemState orientOnly = SessionAppearance::withoutCrop(
            m_displayPipeline.wantAppearanceForItem(
                item,
                item->sessionId() != kInvalidSessionImageId
                    ? item->sessionId()
                    : (isImageMode() ? m_sessionId.currentIdValue()
                                     : kInvalidSessionImageId)));
        applyContentLayoutSize(item, orientOnly);
    }
    if (isImageMode() || m_items.size() == 1) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            if (isImageMode()) {
                pl.pos = QPointF(0, 0);
            }
            item->applyPlacement(pl);
        }
        resetTransform();
        fitInView(item, mode);
        // fitInView alone does not tighten sceneRect — a prior Workspace/Gallery
        // or provisional rect would leave free/asymmetric pan after resize fit.
        syncImageModeSceneRect(item);
        return;
    }
    fitInView(item, mode);
}


void ImageView::ensureVisibleItem(ImageItem *item)
{
    if (item) {
        ensureVisible(item, 32, 32);
    }
}

