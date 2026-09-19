// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "tile_load_coordinator.h"
#include "gallerylayout.h"
#include "toolpolicy.h"
#include "gallerysoftsm.h"
#include "viewtransform.h"
#include "displayedgepolicy.h"
#include "slideshowatlaspolicy.h"
#include "slideshowmotiongeometry.h"
#include "slideshowclocks.h"
#include "zoomblurhelpers.h"
#include "slideshowphasepolicy.h"
#include "displayquality.h"
#include "biltoo_thread.h"

#include "archivepath.h"
#include "biltoo_logging.h"
#include "contentxform.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "imageitem.h"
#include "imageloader.h"
#include "pagepath.h"
#include "thumtoocache.h"
#include "tilelod/tile_lod_controller.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRubberBand>
#include <QScrollBar>
#include <QThreadPool>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <QtMath>

#include <cmath>
#include <cstdio>
#include <cstdlib>

void ImageView::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    setCursor(ToolPolicy::cursorFor(m_tool));
    // Workspace Select: rubber-band multi-select on empty drag (same as Gallery).
    // Pan / Zoom keep NoDrag (view gestures are handled in mouse handlers).
    if (isWorkspaceMode()) {
        setDragMode(ToolPolicy::workspaceRubberBand(m_tool)
                        ? QGraphicsView::RubberBandDrag
                        : QGraphicsView::NoDrag);
    }
    emit toolChanged(m_tool);
}

void ImageView::setImageModeNavigationEnabled(bool on)
{
    if (!m_sessionNav.setImageModeNav(on)) {
        return;
    }
    if (!on && m_hoverEdge != EdgeZone::GalleryReturn) {
        clearHoverEdge();
    }
    viewport()->update();
}

void ImageView::setGalleryReturnAvailable(bool on)
{
    if (!m_sessionNav.setGalleryReturnAvailable(on)) {
        return;
    }
    if (!on && m_hoverEdge == EdgeZone::GalleryReturn) {
        clearHoverEdge();
    }
    viewport()->update();
}

void ImageView::setBackgroundColor(const QColor &color)
{
    if (!m_canvasBg.setColor(color)) {
        return;
    }
    setBackgroundBrush(QBrush(m_canvasBg.primaryColor()));
    if (viewport()) {
        viewport()->update();
    }
}

QColor ImageView::slideshowPadColor() const
{
    if (m_slideshow.settings().isSolidLetterbox()
        && m_slideshow.settings().padColorRef().isValid()) {
        return m_slideshow.settings().padColorRef();
    }
    if (m_canvasBg.primaryColor().isValid()) {
        return m_canvasBg.primaryColor();
    }
    const QBrush b = backgroundBrush();
    if (b.style() != Qt::NoBrush && b.color().isValid()) {
        return b.color();
    }
    return m_slideshow.settings().padColorRef().isValid() ? m_slideshow.settings().padColorRef() : QColor(42, 42, 42);
}

void ImageView::setBackgroundColorAlt(const QColor &color)
{
    if (!m_canvasBg.setColorAlt(color)) {
        return;
    }
    viewport()->update();
}

void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    if (!m_canvasBg.setPattern(pattern)) {
        return;
    }
    viewport()->update();
}

void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    if (!m_canvasBg.setCheckerWorkspaceOnly(on)) {
        return;
    }
    viewport()->update();
}

void ImageView::setWorkspaceBackground(const WorkspaceBackground &bg)
{
    // No-op when durable fields match: leave temporary "show default" preview
    // alone so the toolbar toggle does not desync from paint.
    if (!m_canvasBg.setWorkspace(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_canvasBg.workspaceTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_canvasBg.setWorkspaceTile(px, bg.imagePath);
            }
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::clearWorkspaceBackground()
{
    WorkspaceBackground def;
    setWorkspaceBackground(def);
}

void ImageView::setWorkspaceBackgroundShowDefault(bool on)
{
    if (!m_canvasBg.setWorkspaceShowDefault(on)) {
        return;
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::setViewBackground(const WorkspaceBackground &bg)
{
    if (!m_canvasBg.setView(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_canvasBg.viewTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_canvasBg.setViewTile(px, bg.imagePath);
            }
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::clearViewBackground()
{
    WorkspaceBackground def;
    setViewBackground(def);
}

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
        scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowScrollMs);
    } else {
        scheduleTileLodAfterInteraction(50);
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
            updateGalleryDecodeWindow();
        }
        emit statusChanged();
        return;
    }
    // Image mode 1:1 — item at native scale, view identity, then centre
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
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
            updateGalleryDecodeWindow();
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
        item->setItemScale(1.0);
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
            updateGalleryDecodeWindow();
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
        item->setItemScale(1.0);
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
    if (m_items.isEmpty() && isImageMode() && !hasClassicPath()) {
        return;
    }
    cancelZoomRegion();
    m_zoomRegion.arm();
    setCursor(Qt::CrossCursor);
    emit statusChanged();
    viewport()->update();
}


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
            item->setItemScale(1.0);
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
        item->setItemScale(1.0);
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

void ImageView::setContentEditMarksVisible(bool on)
{
    ImageItem::setContentEditMarksVisible(on);
    if (m_scene) {
        for (ImageItem *it : m_items) {
            if (it) {
                it->update();
            }
        }
        m_scene->update();
    }
    if (viewport()) {
        viewport()->update();
    }
}

bool ImageView::contentEditMarksVisible() const
{
    return ImageItem::contentEditMarksVisible();
}

QSize ImageView::logicalSizeForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = m_sizeBook.known(path);
    if (!known.isEmpty()) {
        return known;
    }
    const QSize cached = ThumtooCache::cachedSize(path);
    if (isPositiveSize(cached)) {
        return cached;
    }
    return {};
}

QSize ImageView::ensureSlideshowLogicalSize(const QString &path)
{
    return ensureLogicalSizeForPath(path);
}

QSize ImageView::ensureLogicalSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !isProvisionalImageSize(path)) {
        return known;
    }
    // imageSizeForPath may schedule a probe and/or install thumtoo cache.
    return imageSizeForPath(path);
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
    // Exception: a session crop bake (cropToLocalRect / materializeDisplay) sets
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
            const WorkspaceItemState want = wantAppearanceForItem(item, sid);
            const QSize lay = ContentXform::layoutSize(fileNative, want);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                item->setIntrinsicSize(lay);
            }
        }
    } else if (cropDraft && !path.isEmpty()) {
        const WorkspaceItemState orientOnly = SessionAppearance::withoutCrop(
            wantAppearanceForItem(
                item,
                item->sessionId() != kInvalidSessionImageId
                    ? item->sessionId()
                    : (isImageMode() ? m_sessionId.currentIdValue()
                                     : kInvalidSessionImageId)));
        applyContentLayoutSize(item, orientOnly);
    }
    if (isImageMode() || m_items.size() == 1) {
        item->setItemScale(1.0);
        if (isImageMode()) {
            item->setPos(0, 0);
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

QString ImageView::currentPath() const
{
    if (ImageItem *item = targetItem()) {
        return item->path();
    }
    if (ImageItem *item = primaryItem()) {
        return item->path();
    }
    return {};
}

void ImageView::setSlideshowPadColor(const QColor &color)
{
    m_slideshow.setSlideshowPadColor(color);
}

void ImageView::setSlideshowLetterboxFill(SlideshowLetterboxFill mode)
{
    m_slideshow.setSlideshowLetterboxFill(mode);
}

void ImageView::setSessionPosition(int index, int total, bool pulseIdentity)
{
    m_slideshow.setSessionPosition(index, total, pulseIdentity);
}

void ImageView::setSlideshowProgress(bool active, int intervalMs)
{
    m_slideshow.setSlideshowProgress(active, intervalMs);
}

void ImageView::setSlideshowProgressPaused(bool paused)
{
    m_slideshow.setSlideshowProgressPaused(paused);
}

void ImageView::setSlideshowTimeline(qint64 elapsedMs, qint64 totalMs)
{
    m_slideshow.setSlideshowTimeline(elapsedMs, totalMs);
}

void ImageView::setSlideshowCycleProgress(qreal phase01)
{
    m_slideshow.setSlideshowCycleProgress(phase01);
}

void ImageView::setSlideshowTransition(SlideshowTransition kind)
{
    m_slideshow.setSlideshowTransition(kind);
}

void ImageView::setSlideshowTransitionDurationMs(int ms)
{
    m_slideshow.setSlideshowTransitionDurationMs(ms);
}

void ImageView::cancelSlideshowTransition()
{
    m_slideshow.cancelSlideshowTransition();
}

void ImageView::setSlideshowMotion(SlideshowMotion mode)
{
    m_slideshow.setSlideshowMotion(mode);
}

void ImageView::setPanZoomFactor(qreal factor)
{
    m_slideshow.setPanZoomFactor(factor);
}

void ImageView::setSlideshowZoom(SlideshowZoom mode)
{
    m_slideshow.setSlideshowZoom(mode);
}

void ImageView::reapplySlideshowFraming()
{
    m_slideshow.reapplySlideshowFraming();
}

void ImageView::setSlideshowMotionPaused(bool paused)
{
    m_slideshow.setSlideshowMotionPaused(paused);
}

void ImageView::setSlideshowPausedHud(bool on)
{
    m_slideshow.setSlideshowPausedHud(on);
}

void ImageView::cancelSlideshowMotion()
{
    m_slideshow.cancelSlideshowMotion();
}

void ImageView::restoreImageFramingAfterSlideshow()
{
    m_slideshow.restoreImageFramingAfterSlideshow();
}

void ImageView::setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT)
{
    m_slideshow.setSlideshowPhase(fromPath, toPath, fadeT);
}

void ImageView::setSlideshowNavHot(bool hot)
{
    m_slideshow.setSlideshowNavHot(hot);
}

void ImageView::preloadSlideshowImage(const QString &path)
{
    m_slideshow.preloadSlideshowImage(path);
}

