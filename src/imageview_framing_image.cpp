// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// View zoom, fit/fill, sticky zoom enable, zoom-region. Image-mode framing: ImageController.

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

void ImageView::cancelZoomRegion()
{
    m_zoomRegion.disarm();
    m_zoomRegion.hideRubber();
    if (!m_chrome.isPanning() && !m_workspace.itemInteract().isRotating()) {
        setCursor(ToolPolicy::cursorFor(m_tool));
    }
    emit statusChanged();
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
            const WorkspaceItemState want = m_displayPipeline->wantAppearanceForItem(item, sid);
            const QSize lay = ContentXform::layoutSize(fileNative, want);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                m_displayPipeline->hostSetIntrinsicSize(item, lay);
            }
        }
    } else if (cropDraft && !path.isEmpty()) {
        const WorkspaceItemState orientOnly = SessionAppearance::withoutCrop(
            m_displayPipeline->wantAppearanceForItem(
                item, resolveContentEditSessionId(item)));
        m_displayPipeline->applyContentLayoutSize(item, orientOnly);
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
        m_displayPipeline->scheduleTileLodAfterInteraction(50);
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


// --- Image-mode framing: owned by ImageController (thin host forwards) ---

void ImageView::captureStickyPanAnchor(ImageItem *item)
{
    m_image.captureStickyPanAnchor(item);
}

void ImageView::restoreStickyPanAnchor(ImageItem *item)
{
    m_image.restoreStickyPanAnchor(item);
}

void ImageView::applyImageModeFraming(ImageItem *item)
{
    m_image.applyImageModeFraming(item);
}

void ImageView::preserveImageViewOnLogicalSizeChange(ImageItem *item,
                                                     const QSize &before,
                                                     const QSize &after)
{
    m_image.preserveImageViewOnLogicalSizeChange(item, before, after);
}

void ImageView::syncImageModeSceneRect(ImageItem *item)
{
    m_image.syncImageModeSceneRect(item);
}
