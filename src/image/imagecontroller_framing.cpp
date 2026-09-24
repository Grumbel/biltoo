// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image-mode framing and sticky pan (owned by ImageController).

#include "image/imagecontroller.h"
#include <QWheelEvent>
#include <QGraphicsView>
#include "imageview.h"
#include "util/biltoo_thread.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "view/viewtransform.h"
#include "view/viewframing.h"
#include "view/viewmodeflags.h"

#include <QScrollBar>
#include "image/toolpolicy.h"
#include <QPointer>
#include <QTransform>
#include <QTimer>
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "crop/cropsession.h"
#include "crop/cropcontroller.h"
#include "display/displaypipelinecontroller.h"
#include "item/imagesizebook.h"
#include "gallery/gallerylayout.h"
#include "gallery/gallerycontroller.h"
#include "gallery/gallerydecodesm.h"
#include <QGraphicsScene>
#include <QWidget>


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

void ImageController::maybeCaptureStickyPanOnLeave(int previousMode)
{
    if (!m_view
        || !ViewModeFlags::shouldCaptureStickyPanOnLeave(
            previousMode, !m_view->liveItems().isEmpty())) {
        return;
    }
    if (ImageItem *cur = m_view->targetItem()) {
        captureStickyPanAnchor(cur);
    } else {
        captureStickyPanAnchor(m_view->liveItems().first());
    }
}

void ImageController::setStickyZoomEnabled(bool on)
{
    if (!m_framing.setStickyZoomEnabled(on)) {
        return;
    }
    emit m_view->stickyZoomChanged();
    emit m_view->statusChanged();
}

void ImageController::releaseStickyZoom()
{
    setStickyZoomEnabled(false);
}

void ImageController::captureStickyPanAnchor(ImageItem *item)
{
    // Sample scale + pan when leaving an image so free navigation (and mode
    // leave/enter) can keep the same zoom and relative position.
    if (!item || !m_view->viewport() || !m_view->canvasScene()) {
        return;
    }
    if (!m_view->liveItems().contains(item) || item->scene() != m_view->canvasScene()) {
        return;
    }
    // Do not clobber a good preserved scale with a 1×1-placeholder frame.
    if (!itemHasReliableFrameSize(item)) {
        return;
    }
    const qreal sx = ViewTransform::scaleFrom(m_view->transform());
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
    const QPointF vc = m_view->mapToScene(m_view->viewport()->rect().center());
    m_framing.setStickyPanFromScene(vc, r);
}


void ImageController::restoreStickyPanAnchor(ImageItem *item)
{
    if (!m_framing.hasStickyPan() || !item || !m_view->canvasScene() || !m_view->viewport()) {
        return;
    }
    if (!m_view->liveItems().contains(item) || item->scene() != m_view->canvasScene()) {
        return;
    }
    const QRectF r = item->sceneBoundingRect();
    if (r.width() < 1.0 || r.height() < 1.0) {
        return;
    }
    m_view->centerOn(m_framing.sceneFromStickyPan(r));
}


void ImageController::applyImageModeFraming(ImageItem *item)
{
    GUI_BUDGET("ImageController::applyImageModeFraming");
    if (!item || !m_view->isImageMode()) {
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
            m_view->fitItem(item, Qt::KeepAspectRatioByExpanding);
            break;
        case StickyZoomKind::Actual:
            m_framing.clearFitFill();
            {
                ItemComponents::Placement pl = item->placement();
                pl.scale = 1.0;
                pl.scaleY = 1.0;
                item->applyPlacement(pl);
            }
            m_view->resetTransform();
            m_view->centerOn(item);
            break;
        case StickyZoomKind::Fit:
        default:
            m_framing.setFitOnly();
            m_view->fitItem(item, Qt::KeepAspectRatio);
            break;
        }
        syncImageModeSceneRect(item);
        m_view->refreshScrollBarGeometry();
        if (!m_framing.isStickyFit()) {
            restoreStickyPanAnchor(item);
            // Scroll ranges often settle after this returns — restore again.
            // QPointer so a destroy mid-navigation cancels the callback safely.
            const QPointer<ImageView> guard(m_view);
            QTimer::singleShot(0, m_view, [guard]() {
                ImageView *const view = guard.data();
                if (!view || !view->hostFraming().isStickyZoomEnabled()
                    || view->hostFraming().isStickyFit()
                    || !view->canvasScene() || !view->viewport()) {
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
            m_view->fitItem(item, Qt::KeepAspectRatio);
            return;
        }
        m_framing.clearFitFill();
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        m_view->resetTransform();
        m_view->scale(sx, sx);
        syncImageModeSceneRect(item);
        m_view->refreshScrollBarGeometry();
        restoreStickyPanAnchor(item);
        const QPointer<ImageView> guard(m_view);
        QTimer::singleShot(0, m_view, [guard]() {
            ImageView *const view = guard.data();
            if (!view || view->hostFraming().isStickyZoomEnabled() || !view->canvasScene()
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
    m_view->fitItem(item, Qt::KeepAspectRatio);
}


void ImageController::preserveImageViewOnLogicalSizeChange(ImageItem *item,
                                                     const QSize &before,
                                                     const QSize &after)
{
    if (!item || !m_view->viewport()) {
        return;
    }
    if (!after.isValid() || after.width() < 1 || after.height() < 1) {
        return;
    }
    const bool beforeOk = before.isValid() && before.width() > 1 && before.height() > 1;
    const bool aspectShifted = !beforeOk || ContentXform::aspectChanged(before, after);
    if (aspectShifted) {
        if (m_view->hostSlideshow().hud().isProgressActive() && m_view->hostSlideshow().settings().isMotionOff()) {
            m_view->hostSlideshow().applySlideshowZoomFraming(item);
        } else if (!m_view->hostSlideshow().hud().isProgressActive()) {
            // Sticky or preserved free-zoom: full framing path. fitItem alone
            // would drop a leave/enter captured scale after a 1×1 placeholder.
            if (m_framing.isStickyZoomEnabled() || m_framing.hasPreservedViewScale()) {
                applyImageModeFraming(item);
            } else {
                m_view->fitItem(item, m_view->currentFitAspectMode());
            }
        }
    } else if (beforeOk && before != after && !m_view->hostSlideshow().hud().isProgressActive()) {
        // Same aspect, larger/smaller logical size: scale the view so the image
        // keeps the same on-screen footprint (soft→native must not zoom).
        // Slideshow pure-phase paints via m_view->hostSlideshow().paintMotionCover(logical size) and
        // does not use the view matrix for framing.
        const qreal factor = ContentXform::footprintScaleFactor(before, after);
        if (factor > 0.0 && qIsFinite(factor) && !qFuzzyCompare(factor, 1.0)) {
            const QPointF sceneCenter = m_view->mapToScene(m_view->viewport()->rect().center());
            const QGraphicsView::ViewportAnchor saved =
                m_view->transformationAnchor();
            m_view->setTransformationAnchor(QGraphicsView::NoAnchor);
            // Scale about the viewport centre in scene space.
            QTransform t = m_view->transform();
            t.translate(sceneCenter.x(), sceneCenter.y());
            t.scale(factor, factor);
            t.translate(-sceneCenter.x(), -sceneCenter.y());
            m_view->setTransform(t, false);
            m_view->setTransformationAnchor(saved);
            m_view->centerOn(sceneCenter);
        }
    }
    syncImageModeSceneRect(item);
}


void ImageController::syncImageModeSceneRect(ImageItem *item)
{
    if (!item || !m_view->canvasScene() || !m_view->isImageMode()) {
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
    if (m_view->sceneRect() != bounds) {
        m_view->setSceneRect(bounds);
    }
}


void ImageController::fitItem(ImageItem *item, Qt::AspectRatioMode mode)
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
    // Exception: a session crop bake (materializeDisplay) sets intrinsic to the
    // crop pixel size. Forcing full-file logicalSizeForPath here immediately
    // after Apply stretched the crop into the pre-crop box.
    //
    // Content ±90° turns: never force file-native size. After rotate, fitItem
    // used to reset intrinsic to unoriented native and paint stretched oriented
    // pixels into the old contentRect.
    const QString path = item->path();
    // Critical: crop session stays active through applyCropCommit → fitItem, *after*
    // the crop bake is attached. Treating any session.active() as draft forced
    // orient-only layout on top of crop pixels → stretch into the pre-crop
    // contentRect. Only pure draft (no applied crop, no session crop on the
    // item) is draft geometry.
    // Draft enter clears live session crop while ItemWorld still holds durable
    // crop until Apply. isDraftLayoutGeometry must use the live flag only
    // Applied ContentXform: ItemWorld when bound (itemAppliedContentXform).
    const ContentXform::Value liveCx = m_view->itemAppliedContentXform(item);
    const bool liveSessionCrop = liveCx.hasCrop;
    const bool cropDraft = CropSession::isDraftLayoutGeometry(
        m_view->hostCrop().session().active(),
        m_view->itemHasAppliedContentXform(item) && liveCx.hasCrop,
        liveSessionCrop);
    if (!path.isEmpty() && !liveSessionCrop && !cropDraft) {
        const QSize fileNative = m_view->ensureLogicalSizeForPath(path);
        if (fileNative.isValid() && fileNative.width() > 1 && fileNative.height() > 1
            && !m_view->hostSizeBook().isProvisional(path)) {
            const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
            const WorkspaceItemState want =
                m_view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
            const QSize lay = ContentXform::layoutSize(fileNative, want);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                m_view->hostDisplayPipeline().hostSetIntrinsicSize(item, lay);
            }
        }
    } else if (cropDraft && !path.isEmpty()) {
        const WorkspaceItemState orientOnly = SessionAppearance::withoutCrop(
            m_view->hostDisplayPipeline().wantAppearanceForItem(
                item, m_view->hostResolveContentEditSessionId(item)));
        m_view->hostDisplayPipeline().applyContentLayoutSize(item, orientOnly);
    }
    if (m_view->isImageMode() || m_view->liveItems().size() == 1) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            if (m_view->isImageMode()) {
                pl.pos = QPointF(0, 0);
            }
            item->applyPlacement(pl);
        }
        m_view->resetTransform();
        m_view->fitInView(item, mode);
        // fitInView alone does not tighten sceneRect — a prior Workspace/Gallery
        // or provisional rect would leave free/asymmetric pan after resize fit.
        m_view->syncImageModeSceneRect(item);
        return;
    }
    m_view->fitInView(item, mode);
}

void ImageController::zoomFit()
{
    m_framing.setFitOnly();
    if (m_view->isGalleryMode()) {
        // Fit the packed gallery into the viewport (whole pack). Sticky zoom
        // is Image-mode only — Gallery uses one-shot framing + ensureVisible.
        if (!m_view->liveItems().isEmpty()) {
            QGraphicsScene *scene = m_view->canvasScene();
            const QRectF bounds = ViewTransform::padded(
                scene->itemsBoundingRect(), GalleryLayout::Params::kDefaultMargin);
            if (bounds.isValid() && !bounds.isEmpty()) {
                scene->setSceneRect(bounds);
                m_view->fitInView(bounds, Qt::KeepAspectRatio);
            }
            m_view->hostGallery().updateDecodeWindow();
            m_view->refreshScrollBarGeometry();
            emit m_view->statusChanged();
        }
        return;
    }
    if (m_view->isWorkspaceMode()) {
        if (!m_view->liveItems().isEmpty()) {
            m_view->fitInView(
                ViewTransform::padded(m_view->canvasScene()->itemsBoundingRect(), 32),
                Qt::KeepAspectRatio);
            m_view->refreshScrollBarGeometry();
            emit m_view->statusChanged();
        }
        return;
    }
    if (ImageItem *item = m_view->targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        fitItem(item, Qt::KeepAspectRatio);
        m_view->refreshScrollBarGeometry();
        emit m_view->statusChanged();
    } else if (m_view->liveItems().size() > 1) {
        m_view->fitInView(m_view->canvasScene()->itemsBoundingRect(), Qt::KeepAspectRatio);
        m_view->refreshScrollBarGeometry();
        emit m_view->statusChanged();
    }
}

void ImageController::zoomFill()
{
    m_framing.setFillMode();
    if (m_view->isGalleryMode()) {
        if (!m_view->liveItems().isEmpty()) {
            QGraphicsScene *scene = m_view->canvasScene();
            const QRectF bounds = ViewTransform::padded(
                scene->itemsBoundingRect(), GalleryLayout::Params::kDefaultMargin);
            if (bounds.isValid() && !bounds.isEmpty()) {
                scene->setSceneRect(bounds);
                m_view->fitInView(bounds, Qt::KeepAspectRatioByExpanding);
            }
            m_view->hostGallery().updateDecodeWindow();
            m_view->refreshScrollBarGeometry();
            emit m_view->statusChanged();
        }
        return;
    }
    if (m_view->isWorkspaceMode()) {
        if (!m_view->liveItems().isEmpty()) {
            m_view->fitInView(
                ViewTransform::padded(m_view->canvasScene()->itemsBoundingRect(), 32),
                Qt::KeepAspectRatioByExpanding);
            m_view->refreshScrollBarGeometry();
            emit m_view->statusChanged();
        }
        return;
    }
    if (ImageItem *item = m_view->targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        fitItem(item, Qt::KeepAspectRatioByExpanding);
        m_view->refreshScrollBarGeometry();
        emit m_view->statusChanged();
    } else if (m_view->liveItems().size() > 1) {
        m_view->fitInView(m_view->canvasScene()->itemsBoundingRect(),
                          Qt::KeepAspectRatioByExpanding);
        m_view->refreshScrollBarGeometry();
        emit m_view->statusChanged();
    }
}

void ImageController::zoomReset()
{
    m_framing.clearFitFill();
    if (m_view->isMultiItemMode()) {
        // Gallery/Workspace: one-shot identity view (sticky zoom is Image-only).
        m_view->resetTransform();
        if (m_view->isGalleryMode()) {
            m_view->hostGallery().updateDecodeWindow();
        }
        emit m_view->statusChanged();
        return;
    }
    // Image mode 1:1 — item at native scale, view identity, then centre
    if (ImageItem *item = m_view->targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        m_view->resetTransform();
        m_view->centerOn(item);
        emit m_view->statusChanged();
    }
}

void ImageController::zoomViewBy(qreal factor)
{
    if (factor <= 0.0) {
        return;
    }
    // Image sticky Fit/Fill ends on free zoom so the current framing is
    // preserved for inspection (matches wheel zoom). Pack /
    // resize still resets the view transform so tiles stay layout-correct
    // (AUDIT M4 — one policy: zoom works until next pack).
    m_view->releaseStickyZoom();
    m_framing.clearFitFill();
    // Keep the viewport centre stable when zooming via toolbar/shortcuts
    m_view->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    m_view->scale(factor, factor);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Viewport-space chrome only — no selected-item prepareGeometryChange.
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
    // Zoom changes on-screen cell size → ladder / tile LOD after settle.
    // Gallery already debounced interest; Image/Workspace match that pattern
    // so continuous zoom does not issue tile work every notch.
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowScrollMs);
    } else {
        m_view->hostDisplayPipeline().scheduleTileLodAfterInteraction(50);
    }
    emit m_view->statusChanged();
}


void ImageController::zoomIn()
{
    // View-level zoom in Image mode and free-form Workspace
    zoomViewBy(1.25);
}

void ImageController::zoomOut()
{
    zoomViewBy(1.0 / 1.25);
}

void ImageController::wheelZoomAboutCursor(QWheelEvent *event)
{
    if (!event) {
        return;
    }
    const qreal factor = ViewTransform::wheelZoomFactor(event->angleDelta().y());
    // Image mode and free-form Workspace: zoom the view about the cursor.
    // Do not touch selected-item geometry here — prepareGeometryChange on
    // handle pads was expanding AABBs and fighting the user's pan/zoom.
    m_view->hostSlideshow().cancelSlideshowMotion();
    m_view->releaseStickyZoom();
    m_framing.releaseFit();
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->scale(factor, factor);
    // Workspace: keep sceneRect covering all free-form tiles after zoom so
    // items are not clipped when the viewport-in-scene halo shrinks.
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    // Soft / PreferCache / tile LOD: coalesce continuous wheel notches.
    // Per-notch climb+tick was heavy on the GUI thread (set_viewport, cancel,
    // issue_requests). Paint uses the last plan + soft until the debounce fires.
    m_view->hostDisplayPipeline().scheduleTileLodAfterInteraction(50);
    if (QWidget *vp = m_view->viewport()) {
        vp->update(); // refresh viewport-space chrome at the new scale
    }
    emit m_view->statusChanged();
    event->accept();
}

void ImageController::setWorkspaceDefaultViewScale()
{
    // Workspace is an overview canvas for multiple pages. Match four toolbar
    // zoom-out presses: each step is 1/1.25, so scale = (1/1.25)^4 ≈ 0.4096 (41%).
    constexpr qreal kStep = 1.25;
    const qreal s = 1.0 / (kStep * kStep * kStep * kStep);
    m_framing.clearFitFill();
    m_view->resetTransform();
    m_view->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    m_view->scale(s, s);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
    emit m_view->statusChanged();
}
