// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image-mode framing and sticky pan (owned by ImageController).

#include "image/imagecontroller.h"
#include "imageview.h"
#include "util/biltoo_thread.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "view/viewtransform.h"
#include "view/viewframing.h"

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
