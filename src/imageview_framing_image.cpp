// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// View zoom, fit/fill, sticky zoom/pan, and image-mode framing.

#include "imageview.h"
#include "util/biltoo_thread.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "view/viewtransform.h"
#include "view/viewframing.h"

#include <QScrollBar>
#include "image/toolpolicy.h"
#include <QPointer>
#include <QTimer>
#include "content/contentxform.h"
#include "session/sessionappearance.h"

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






/** True when intrinsic size is large enough that fitInView will not explode. */
static bool itemHasReliableFrameSize(const ImageItem *item)
{
    if (!item) {
        return false;
    }
    const QSize s = item->imageSize();
    // QSize(1,1) placeholders produced ~5000% view scale via fitInView.
    return s.width() > 8 && s.height() > 8;
}

void ImageView::captureStickyPanAnchor(ImageItem *item)
{
    // Sample scale + pan when leaving an image so free navigation (and mode
    // leave/enter) can keep the same zoom and relative position.
    if (!item || !viewport() || !m_scene) {
        return;
    }
    if (!m_items.contains(item) || item->scene() != m_scene) {
        return;
    }
    // Do not clobber a good preserved scale with a 1×1-placeholder frame.
    if (!itemHasReliableFrameSize(item)) {
        return;
    }
    const qreal sx = ViewTransform::scaleFrom(transform());
    if (!qIsFinite(sx) || sx <= 1e-6) {
        return;
    }
    // Refuse absurd scales (fit-on-1×1 residue or transform corruption).
    if (sx > 50.0) {
        return;
    }
    m_framing.clearStickyPan();
    m_framing.clearPreservedViewScale();
    m_framing.setPreservedViewScale(sx);
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
    GUI_BUDGET("ImageView::applyImageModeFraming");
    if (!item || !isImageMode()) {
        return;
    }
    // Defer framing until the item has a real layout size. Fitting a 1×1
    // placeholder yields multi-thousand-percent view scale and poisons
    // preserved zoom for later images.
    if (!itemHasReliableFrameSize(item)) {
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
        const qreal sx = m_framing.currentPreservedViewScale();
        if (!qIsFinite(sx) || sx <= 1e-6 || sx > 50.0) {
            m_framing.clearPreservedViewScale();
            m_framing.setFitOnly();
            fitItem(item, Qt::KeepAspectRatio);
            return;
        }
        m_framing.clearFitFill();
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        resetTransform();
        scale(sx, sx);
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
            // Sticky or preserved free-zoom: full framing path. fitItem alone
            // would drop a leave/enter captured scale after a 1×1 placeholder.
            if (m_framing.isStickyZoomEnabled() || m_framing.hasPreservedViewScale()) {
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
    // Draft enter clears live session crop while ItemWorld still holds durable
    // crop until Apply. isDraftLayoutGeometry must use the live flag only
    // Applied ContentXform: ItemWorld when bound (itemAppliedContentXform).
    const ContentXform::Value liveCx = itemAppliedContentXform(item);
    const bool liveSessionCrop = liveCx.hasCrop;
    const bool cropDraft = CropSession::isDraftLayoutGeometry(
        m_cropCtrl.session().active(),
        itemHasAppliedContentXform(item) && liveCx.hasCrop,
        liveSessionCrop);
    if (!path.isEmpty() && !liveSessionCrop && !cropDraft) {
        const QSize fileNative = ensureLogicalSizeForPath(path);
        if (fileNative.isValid() && fileNative.width() > 1 && fileNative.height() > 1
            && !m_sizeBook.isProvisional(path)) {
            const SessionImageId sid = resolveContentEditSessionId(item);
            const WorkspaceItemState want = m_displayPipeline.wantAppearanceForItem(item, sid);
            const QSize lay = ContentXform::layoutSize(fileNative, want);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                m_displayPipeline.hostSetIntrinsicSize(item, lay);
            }
        }
    } else if (cropDraft && !path.isEmpty()) {
        const WorkspaceItemState orientOnly = SessionAppearance::withoutCrop(
            m_displayPipeline.wantAppearanceForItem(
                item, resolveContentEditSessionId(item)));
        m_displayPipeline.applyContentLayoutSize(item, orientOnly);
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

// --- View zoom / fit / fill (was imageview_framing.cpp) ---

qreal ImageView::viewScale() const
{
    return ViewTransform::scaleFrom(transform());
}


void ImageView::zoomViewBy(qreal factor)
{
    // Gallery: view zoom is allowed for inspection (matches wheel zoom). Pack /
    // resize still resets the view transform so tiles stay layout-correct
    // (AUDIT M4 — one policy: zoom works until next pack).
    releaseStickyZoom();
    m_framing.clearFitFill();
    // Keep the viewport centre stable when zooming via toolbar/shortcuts
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Viewport-space chrome only — no selected-item prepareGeometryChange.
    if (viewport()) {
        viewport()->update();
    }
    // Zoom changes on-screen cell size → ladder / tile LOD after settle.
    // Gallery already debounced interest; Image/Workspace match that pattern
    // so continuous zoom does not issue tile work every notch.
    if (isGalleryMode()) {
        m_gallery.scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowScrollMs);
    } else {
        m_displayPipeline.scheduleTileLodAfterInteraction(50);
    }
    emit statusChanged();
}


void ImageView::zoomIn()
{
    // View-level zoom in Image mode and free-form Workspace
    zoomViewBy(1.25);
}


void ImageView::zoomOut()
{
    zoomViewBy(1.0 / 1.25);
}


void ImageView::setWorkspaceDefaultViewScale()
{
    // Workspace is an overview canvas for multiple pages. Match four toolbar
    // zoom-out presses: each step is 1/1.25, so scale = (1/1.25)^4 ≈ 0.4096 (41%).
    constexpr qreal kStep = 1.25;
    const qreal s = 1.0 / (kStep * kStep * kStep * kStep);
    m_framing.clearFitFill();
    resetTransform();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(s, s);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}


void ImageView::zoomReset()
{
    m_framing.clearFitFill();
    if (isMultiItemMode()) {
        // Gallery/Workspace: one-shot identity view (sticky zoom is Image-only).
        resetTransform();
        if (isGalleryMode()) {
            m_gallery.updateDecodeWindow();
        }
        emit statusChanged();
        return;
    }
    // Image mode 1:1 — item at native scale, view identity, then centre
    if (ImageItem *item = targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        resetTransform();
        centerOn(item);
        emit statusChanged();
    }
}


void ImageView::refreshScrollBarGeometry()
{
    // fitInView / sceneRect changes can leave AsNeeded bars with a stale range
    // until policy is toggled. Re-apply the current policies to force
    // QAbstractScrollArea to recompute visibility (public API only).
    const auto h = horizontalScrollBarPolicy();
    const auto v = verticalScrollBarPolicy();
    if (h == Qt::ScrollBarAsNeeded || v == Qt::ScrollBarAsNeeded) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(h);
        setVerticalScrollBarPolicy(v);
    }
}


void ImageView::zoomFit()
{
    m_framing.setFitOnly();
    if (isGalleryMode()) {
        // Fit the packed gallery into the viewport (whole pack). Sticky zoom
        // is Image-mode only — Gallery uses one-shot framing + ensureVisible.
        if (!m_items.isEmpty()) {
            const QRectF bounds = ViewTransform::padded(m_scene->itemsBoundingRect(), GalleryLayout::Params::kDefaultMargin);
            if (bounds.isValid() && !bounds.isEmpty()) {
                m_scene->setSceneRect(bounds);
                fitInView(bounds, Qt::KeepAspectRatio);
            }
            m_gallery.updateDecodeWindow();
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(ViewTransform::padded(m_scene->itemsBoundingRect(), 32),
                      Qt::KeepAspectRatio);
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        fitItem(item, Qt::KeepAspectRatio);
        refreshScrollBarGeometry();
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        refreshScrollBarGeometry();
        emit statusChanged();
    }
}


void ImageView::zoomFill()
{
    m_framing.setFillMode();
    if (isGalleryMode()) {
        if (!m_items.isEmpty()) {
            const QRectF bounds = ViewTransform::padded(m_scene->itemsBoundingRect(), GalleryLayout::Params::kDefaultMargin);
            if (bounds.isValid() && !bounds.isEmpty()) {
                m_scene->setSceneRect(bounds);
                fitInView(bounds, Qt::KeepAspectRatioByExpanding);
            }
            m_gallery.updateDecodeWindow();
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(ViewTransform::padded(m_scene->itemsBoundingRect(), 32),
                      Qt::KeepAspectRatioByExpanding);
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        fitItem(item, Qt::KeepAspectRatioByExpanding);
        refreshScrollBarGeometry();
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatioByExpanding);
        refreshScrollBarGeometry();
        emit statusChanged();
    }
}


void ImageView::armZoomRegion()
{
    if (m_items.isEmpty() && isImageMode() && !m_image.hasClassicPath()) {
        return;
    }
    cancelZoomRegion();
    m_zoomRegion.arm();
    setCursor(Qt::CrossCursor);
    emit statusChanged();
    viewport()->update();
}
