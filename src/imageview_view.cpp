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
    if (m_ssSettings.isSolidLetterbox()
        && m_ssSettings.padColorRef().isValid()) {
        return m_ssSettings.padColorRef();
    }
    if (m_canvasBg.primaryColor().isValid()) {
        return m_canvasBg.primaryColor();
    }
    const QBrush b = backgroundBrush();
    if (b.style() != Qt::NoBrush && b.color().isValid()) {
        return b.color();
    }
    return m_ssSettings.padColorRef().isValid() ? m_ssSettings.padColorRef() : QColor(42, 42, 42);
}

void ImageView::setSlideshowPadColor(const QColor &color)
{
    if (!m_ssSettings.setPadColor(color)) {
        return;
    }
    clearSlideshowZoomBlurSlots();
    if (m_ssHud.isProgressActive() && viewport()) {
        viewport()->update();
    }
}

void ImageView::setSlideshowLetterboxFill(SlideshowLetterboxFill mode)
{
    if (!m_ssSettings.setLetterboxFill(mode)) {
        return;
    }
    clearSlideshowZoomBlurSlots();
    if (m_ssHud.isProgressActive() && viewport()) {
        viewport()->update();
    }
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

void ImageView::refreshStatus()
{
    // Coalesce rapid soft-climb / provenance updates so the HUD and status
    // bar are not rewritten every frame.
    if (!m_statusRefreshTimer) {
        m_statusRefreshTimer = new QTimer(this);
        m_statusRefreshTimer->setSingleShot(true);
        m_statusRefreshTimer->setInterval(HudAppearance::kStatusRefreshMs);
        connect(m_statusRefreshTimer, &QTimer::timeout, this, [this]() {
            emit statusChanged();
            if ((m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_ssHud.isPausedHud())
                && viewport()) {
                viewport()->update();
            }
        });
    }
    m_statusRefreshTimer->start();
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

void ImageView::setSessionPosition(int index, int total, bool pulseIdentity)
{
    const bool changed = m_sessionId.setPosition(index, total);
    // Pulse only when the session cursor actually moves (user Next/Prev, etc.).
    // Do not pulse on every statusChanged while total > 0 (AUDIT H7).
    // Slideshow auto-advance passes pulseIdentity=false.
    if (pulseIdentity && changed) {
        m_hudFlash.setIdentityPulse(true);
        if (m_hudFlashTimer) {
            m_hudFlashTimer->start(HudFlash::kIdentityPulseMs);
        }
    }
    if (!(changed || m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_hudFlash.isIdentityPulse()
          || m_ssHud.isPausedHud())) {
        return;
    }
    // Gallery selection already invalidates the tile; a full viewport()->update()
    // here forced every image through the GL path and felt like lag on click.
    if (isGalleryMode() && !m_hudPrefs.isVisible() && !m_hudFlash.isIdentityPulse()) {
        return;
    }
    if (viewport()) {
        viewport()->update();
    }
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

void ImageView::setHudVisible(bool on)
{
    if (!m_hudPrefs.setVisible(on)) {
        return;
    }
    // Progress line only paints with the pinned HUD; drive the timer accordingly.
    if (m_slideshowProgressTimer) {
        if (on && m_ssHud.isProgressActive() && m_ssHud.hasProgressInterval()) {
            m_slideshowProgressTimer->start();
        } else {
            m_slideshowProgressTimer->stop();
        }
    }
    viewport()->update();
}

void ImageView::setHudFontPointSize(int pt)
{
    if (!m_hudPrefs.setFontPointSize(pt)) {
        return;
    }
    viewport()->update();
}

void ImageView::setHudTextColor(const QColor &color)
{
    if (!m_hudPrefs.setTextColor(color)) {
        return;
    }
    viewport()->update();
}

void ImageView::setHudPanelColor(const QColor &color)
{
    if (!m_hudPrefs.setPanelColor(color)) {
        return;
    }
    viewport()->update();
}

void ImageView::flashHud(const QString &action, const QString &detail)
{
    m_hudFlash.show(action, detail);
    if (m_hudFlashTimer) {
        m_hudFlashTimer->start(HudFlash::kActionFlashMs);
    }
    viewport()->update();
}

void ImageView::setSlideshowProgress(bool active, int intervalMs)
{
    // Speed / interval edit while the show is already running: only update the
    // interval. Do not restart the progress clock or clear phase buffers —
    // that produced HUD and paint blips on every [/] or settings change.
    if (active && m_ssHud.isProgressActive()) {
        m_ssHud.setProgressIntervalMs(intervalMs);
        if (m_ssHud.hasProgressInterval() && m_slideshowProgressTimer
            && !m_ssHud.isProgressClockPaused()) {
            m_slideshowProgressTimer->start();
        } else if (m_slideshowProgressTimer && !m_ssHud.hasProgressInterval()) {
            m_slideshowProgressTimer->stop();
        }
        if (viewport()) {
            viewport()->update();
        }
        return;
    }

    m_ssHud.setProgressActive(active);
    m_ssHud.setProgressIntervalMs(active ? intervalMs : 0);
    if (active) {
        // Pure phase owns the viewport — never flash the underlay item.
        hideSlideshowUnderlay();
        m_ssHud.resetProgressClock();
        if (m_ssHud.hasProgressInterval() && m_slideshowProgressTimer) {
            m_slideshowProgressTimer->start();
        } else if (m_slideshowProgressTimer) {
            m_slideshowProgressTimer->stop();
        }
    } else if (m_slideshowProgressTimer) {
        m_slideshowProgressTimer->stop();
        m_ssHud.resetProgressClock();
    }
    if (!active) {
        cancelSlideshowMotion();
        m_ssDwell.clearBias();
        m_ssHud.clearTimeline();
        m_ssHud.clearPaintFingerprint();
        m_ss.clearFromPath();
        m_ss.clearToPath();
        unbindSlideshowPhaseSurface(&m_ss.fromSurfaceRef());
        unbindSlideshowPhaseSurface(&m_ss.toSurfaceRef());
        m_ss.clearFromImage();
        m_ss.clearToImage();
        m_ss.beginDwell();
        m_ss.stopMotionClocks();
        // Rasters live in ImageCache — do not clear the host map on stop.
        m_ss.clearRasterQueues();
    }
    viewport()->update();
}

void ImageView::setSlideshowProgressPaused(bool paused)
{
    if (paused == m_ssHud.isProgressClockPaused()) {
        return;
    }
    if (paused) {
        if (m_ssHud.isProgressElapsedValid()) {
            m_ssHud.accumulateProgressBaseFromElapsed();
        }
        m_ssHud.setProgressClockPaused(true);
        if (m_slideshowProgressTimer) {
            m_slideshowProgressTimer->stop();
        }
    } else {
        m_ssHud.setProgressClockPaused(false);
        m_ssHud.startProgressElapsed();
        if (m_ssHud.isProgressActive() && m_ssHud.hasProgressInterval()
            && m_slideshowProgressTimer) {
            m_slideshowProgressTimer->start();
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::setSlideshowTimeline(qint64 elapsedMs, qint64 totalMs)
{
    if (totalMs <= 0) {
        if (!m_ssHud.hasTimelineTotal()) {
            return;
        }
        m_ssHud.clearTimeline();
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    elapsedMs = SlideshowProgressHud::clampElapsedMs(elapsedMs, totalMs);
    if (elapsedMs == m_ssHud.timelineElapsed()
        && totalMs == m_ssHud.timelineTotal()) {
        return;
    }
    m_ssHud.setTimelineProgress(elapsedMs, totalMs);
    // Progress bar needs sub-second updates while the extended HUD is pinned.
    if (m_hudPrefs.isVisible() && viewport()) {
        viewport()->update();
    }
}


void ImageView::setSlideshowCycleProgress(qreal phase01)
{
    m_ssHud.setCycleProgress01(phase01);
}


void ImageView::setSlideshowTransition(SlideshowTransition kind)
{
    if (!m_ssSettings.setTransition(kind)) {
        return;
    }
    if (kind == SlideshowTransition::None) {
        cancelSlideshowTransition();
    }
}

void ImageView::setSlideshowTransitionDurationMs(int ms)
{
    m_ssSettings.setTransitionDurationMs(ms);
}

QPixmap ImageView::captureSlideshowFrame() const
{
    // QOpenGLWidget::grab() often returns a blank/white pixmap. Paint the
    // current slide into an offscreen pixmap instead (software, reliable).
    if (!viewport()) {
        return {};
    }
    const int vw = ViewTransform::atLeast1(viewport()->width());
    const int vh = ViewTransform::atLeast1(viewport()->height());
    const qreal dpr = viewport()->devicePixelRatioF();
    QPixmap pm(QSize(vw, vh) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(slideshowPadColor());
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_ssDwell.isMotionActive() && m_ssDwell.hasSourceImage()) {
        paintMotionCover(&painter, m_ssDwell.sourceImageRef(), m_ssDwell.motionTValue(),
                         m_ssDwell.biasAPoint(), m_ssDwell.biasBPoint(), m_ss.fromPathRef());
    } else if (ImageItem *item = targetItem()) {
        // Still frame: draw source (or displayed pixmap) with cover/fit framing.
        const QImage src = item->hasDecodedPixels() ? item->sourceImage()
                                                    : item->pixmap().toImage();
        if (!src.isNull()) {
            paintMotionCover(&painter, src, 0.0, QPointF(0, 0), QPointF(0, 0),
                             item->path());
        }
    }
    painter.end();
    return pm;
}

void ImageView::cancelSlideshowTransition()
{
    // Pure phase has no overlay to cancel; keep the hook for callers that
    // clear transition intent on user nav / stop / pause.
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::setSlideshowMotion(SlideshowMotion mode)
{
    if (!m_ssSettings.setMotion(mode)) {
        return;
    }
    if (mode == SlideshowMotion::Off) {
        cancelSlideshowMotion();
        if (m_ssHud.isProgressActive()) {
            reapplySlideshowFraming();
        }
    } else if (m_ssHud.isProgressActive()) {
        reapplySlideshowFraming();
    }
}

void ImageView::setPanZoomFactor(qreal factor)
{
    m_ssSettings.setPanZoomFactor(factor);
}

void ImageView::setSlideshowZoom(SlideshowZoom mode)
{
    if (!m_ssSettings.setZoom(mode)) {
        return;
    }
    // Zoom is the base scale for Ken Burns as well as static framing.
    if (m_ssHud.isProgressActive()) {
        reapplySlideshowFraming();
    }
}

void ImageView::applySlideshowZoomFraming(ImageItem *item)
{
    if (!item || !viewport()) {
        return;
    }
    // Explicit uniform scale for static slideshow framing — logical size owns
    // geometry (same model as paintMotionCover), not soft contentRect pixels.
    item->setItemShear(0.0);
    item->setItemRotation(0.0);
    item->setItemScale(1.0);
    if (isImageMode()) {
        item->setPos(0, 0);
    }
    const QString path = item->path();
    QSize logical = ensureSlideshowLogicalSize(path);
    if (isPositiveSize(logical) && !isProvisionalImageSize(path)) {
        // File-native → ContentXform layout when phase orient is applied (same
        // rule as resolveMotionLogicalSize / paintMotionCover).
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(path, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            const QSize lay = ContentXform::layoutSize(logical, app);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                logical = lay;
            }
        }
        item->setIntrinsicSize(logical);
    }
    // Provisional / unknown: leave intrinsic alone — LQIP/soft must not set geometry.
    const QRectF content = item->contentRect();
    if (content.width() < 1.0 || content.height() < 1.0) {
        return;
    }
    // Scale from logical size when known; contentRect only for scene mid-point.
    const qreal vw = qreal(ViewTransform::atLeast1(viewport()->width()));
    const qreal vh = qreal(ViewTransform::atLeast1(viewport()->height()));
    const QPointF mid = item->mapToScene(content.center());

    if (!isPositiveSize(logical)) {
        logical = QSize(int(content.width()), int(content.height()));
    }
    qreal scale = slideshowZoomBaseScale(logical, int(vw), int(vh));
    switch (m_ssSettings.currentZoom()) {
    case SlideshowZoom::Fill:
        m_framing.setFitFillFlags(false, true);
        break;
    case SlideshowZoom::Actual:
        m_framing.clearFitFill();
        break;
    case SlideshowZoom::Fit:
    default:
        m_framing.setFitOnly();
        break;
    }
    if (scale <= 0.0 || !qIsFinite(scale)) {
        return;
    }
    const qreal vx = qreal(viewport()->width()) * 0.5;
    const qreal vy = qreal(viewport()->height()) * 0.5;
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    QTransform xform;
    xform.translate(vx, vy);
    xform.scale(scale, scale);
    xform.translate(-mid.x(), -mid.y());
    setTransform(xform);
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setValue(0);
    }
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(0);
    }
}

void ImageView::reapplySlideshowFraming()
{
    if (!m_ssHud.isProgressActive() || !isImageMode()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }
    if (!m_ssSettings.isMotionOff()) {
        int duration = m_ssHud.progressInterval();
        if (duration < 250) {
            duration = 3000;
        }
        // Already in motion (typical interval edit): retarget duration and keep
        // normalized progress + dwell atlas. startSlideshowMotion cancels first
        // and clears the atlas — that is the speed-change flash.
        if (m_ssDwell.isMotionActive()) {
            retargetSlideshowMotionDuration(duration);
        } else {
            startSlideshowMotion(duration);
        }
    } else {
        cancelSlideshowMotion();
        applySlideshowZoomFraming(item);
        if (viewport()) {
            viewport()->update();
        }
        emit statusChanged();
    }
}

void ImageView::setSlideshowMotionPaused(bool paused)
{
    if (paused == m_ssDwell.isMotionPaused()) {
        return;
    }
    if (paused) {
        if (m_ssDwell.isMotionActive() && m_motionTimer && m_motionTimer->isActive()) {
            // Fold wall into unitless dwell progress, then freeze.
            if (m_ssDwell.hasDuration() && m_ssDwell.isClockValid()) {
                const qint64 d = m_ssDwell.clockElapsed();
                if (d > 0) {
                    const qreal t = qreal(m_ssDwell.elapsedOffsetMsValue() + d)
                        / qreal(m_ssDwell.durationMsValue());
                    m_ssDwell.setMotionT(t);
                    m_ssDwell.setElapsedOffsetMs(
                        qint64(m_ssDwell.motionTValue() * qreal(m_ssDwell.durationMsValue())));
                }
            }
            m_motionTimer->stop();
        }
        // Fold phase-motion clocks into T ∈ [0,1].
        const int pathMs = slideshowPathDurationMs();
        if (pathMs > 0) {
            SlideshowClocks::integrateMotionProgress01(&m_ss.fromMotionTRef(), &m_ss.fromMotionClockMutable(),
                                      m_ss.isFromMotionClockRunning(), false, pathMs);
            SlideshowClocks::integrateMotionProgress01(&m_ss.toMotionTRef(), &m_ss.toMotionClockMutable(),
                                      m_ss.isToMotionClockRunning(), false, pathMs);
            if (m_ss.isFromMotionClockRunning()) {
                m_ssDwell.setMotionT(m_ss.fromMotionTValue());
            }
        }
        m_ssDwell.setMotionPaused(true);
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    m_ssDwell.setMotionPaused(false);
    if (m_ss.isFromMotionClockRunning()) {
        m_ss.startFromMotionClock();
    }
    if (m_ss.isToMotionClockRunning()) {
        m_ss.startToMotionClock();
    }
    if (m_ssDwell.isMotionActive() && m_motionTimer && m_ssDwell.hasDuration()) {
        m_ssDwell.restartClock();
        m_motionTimer->start();
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setSlideshowPausedHud(bool on)
{
    if (!m_ssHud.setPausedHud(on)) {
        return;
    }
    if (on) {
        // Keep a stable action line for the permanent cue; flash timer must
        // not clear it (paint draws paused HUD independently of flash).
        m_hudFlash.setPausedLabel(tr("❚❚  Paused"));
        if (m_hudFlashTimer) {
            m_hudFlashTimer->stop();
        }
    } else if (m_hudFlash.actionText().contains(QStringLiteral("Paused"))) {
        m_hudFlash.clearAction();
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::cancelSlideshowMotion()
{
    const bool wasMotion = m_ssDwell.isMotionActive();
    m_ssDwell.setMotionActive(false);
    m_ssDwell.setMotionPaused(false);
    {
        Qt::ScrollBarPolicy h = Qt::ScrollBarAsNeeded;
        Qt::ScrollBarPolicy v = Qt::ScrollBarAsNeeded;
        if (m_motionScroll.release(&h, &v)) {
            // freezeScrollbars may have saved Gallery AsNeeded from before the
            // session was marked running. Restoring that mid-show brings bars back.
            if (m_ssHud.isProgressActive()) {
                setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            } else {
                setHorizontalScrollBarPolicy(h);
                setVerticalScrollBarPolicy(v);
            }
        }
    }
    setSlideshowUnderlayVisible(true);
    m_ssDwell.clearAtlasPixmap();
    m_ssDwell.setElapsedOffsetMs(0);
    if (m_motionTimer) {
        m_motionTimer->stop();
    }
    // Ken Burns only moved the overlay blit. Bring the underlay back in line
    // with static slideshow framing while the show is still running.
    if (wasMotion && m_ssHud.isProgressActive() && isImageMode()) {
        if (ImageItem *item = targetItem()) {
            applySlideshowZoomFraming(item);
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::restoreImageFramingAfterSlideshow()
{
    // Leave slideshow Fill/Actual flags and camera from applySlideshowZoomFraming
    // so Image mode is normal Fit-to-window again.
    if (!isImageMode()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }
    m_framing.setFitOnly();
    fitItem(item, Qt::KeepAspectRatio);
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

bool ImageView::tryApplyAttentionMotionBiases(uint seed, const QImage &source)
{
    QPointF att01;
    bool haveAtt = false;
    if (ImageItem *item = targetItem()) {
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            if (const WorkspaceItemState *st = m_appearance.get(sid)) {
                if (st->hasAttention) {
                    att01 = st->attentionNorm;
                    haveAtt = true;
                }
            }
        }
    }
    if (!haveAtt && !source.isNull() && ImageLoader::attentionPoint(source, &att01)) {
        haveAtt = true;
    }
    if (!haveAtt) {
        return false;
    }
    SlideshowMotionGeometry::BiasPath path;
    if (!SlideshowMotionGeometry::attentionBiasPath(att01, seed, &path)) {
        return false;
    }
    m_ssDwell.applyBias(path.a, path.b, path.travelDir, path.motionSign);
    return true;
}

void ImageView::applyGeometricMotionBiases(uint seed)
{
    const SlideshowMotionGeometry::BiasPath path =
        SlideshowMotionGeometry::geometricBiasPath(seed);
    m_ssDwell.applyBias(path.a, path.b, path.travelDir, path.motionSign);
}

void ImageView::pickInterestingMotionBiases(uint seed, const QImage &source)
{
    // Prefer content-aware centres (libvips attention) when the decoded frame
    // is available; otherwise path-hash corners/edges as before.
    if (seed == 0) {
        seed = 1;
    }
    if (tryApplyAttentionMotionBiases(seed, source)) {
        return;
    }
    applyGeometricMotionBiases(seed);
}




// ---------------------------------------------------------------------------
// Slideshow size model
//
//   logical size  = identity of the image (probe / full decode / thumtoo)
//   sample raster = soft or target-edge pixels used only for sampling
//
// Camera (fit/fill/actual/Ken Burns) always uses logical size.
// putSlideshowRaster / preload never write sample dimensions into the size map.
// ---------------------------------------------------------------------------

void ImageView::putSlideshowRaster(const QString &path, const QImage &image)
{
    // Thin wrapper: host ImageCache is the only path→raster store.
    ImageCache::put(path, image);
}

bool ImageView::phaseBufferWantsSample(const QString &path, int sampleEdge) const
{
    if (sampleEdge <= 0 || path.isEmpty()) {
        return false;
    }
    // Appearance presence is resolved on the GUI; pure size policy is shared.
    WorkspaceItemState app;
    const bool pendingContent = snapshotSlideshowContentAppearance(path, &app)
        && SessionAppearance::hasContentAppearance(app);
    return SlideshowPhasePolicy::bufferWantsSample(
               m_ss.fromPathRef(), m_ss.fromImageRef(), m_ss.isFromContentApplied(), path,
               sampleEdge, pendingContent)
        || SlideshowPhasePolicy::bufferWantsSample(
               m_ss.toPathRef(), m_ss.toImageRef(), m_ss.isToContentApplied(), path,
               sampleEdge, pendingContent);
}

bool ImageView::snapshotSlideshowContentAppearance(const QString &path,
                                                   WorkspaceItemState *out) const
{
    // GUI-only: session map → path map → durable XDG. Worker must not call this.
    // Same resolution order as imageWithSessionAppearance (without transforming).
    if (!out || path.isEmpty()) {
        return false;
    }
    *out = {};
    const SessionImageId sid = sessionIdForPath(path);
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *app = m_appearance.get(sid)) {
            if (SessionAppearance::hasContentAppearance(*app)) {
                *out = *app;
                return true;
            }
        }
    }
    if (const WorkspaceItemState *st = m_itemStateBook.get(path)) {
        if (SessionAppearance::hasContentAppearance(*st)) {
            *out = *st;
            return true;
        }
    }
    ThumtooCache::StoredContentAppearance stored;
    if (ThumtooCache::loadContentAppearance(path, &stored)
        && (stored.contentHFlip || stored.contentVFlip
            || stored.contentQuarterTurns != 0 || stored.hasCrop)) {
        out->path = path;
        out->sessionId = sid;
        out->contentHFlip = stored.contentHFlip;
        out->contentVFlip = stored.contentVFlip;
        out->contentQuarterTurns = stored.contentQuarterTurns;
        out->hasCrop = stored.hasCrop;
        out->cropRect = stored.cropRect;
        out->cropSourceSize = stored.cropSourceSize;
        out->cropRotation = stored.cropRotation;
        return SessionAppearance::hasContentAppearance(*out);
    }
    return false;
}

void ImageView::finishSlideshowPhaseBufferUpgrade(const QString &path, const QImage &oriented,
                                                  quint64 generation)
{
    // GUI assign after pool orient. Generation discards stale mid-slide work.
    if (generation != m_ss.phaseUpgradeGenerationValue()) {
        return;
    }
    if (path.isEmpty() || oriented.isNull()) {
        return;
    }
    const int incoming = ImageCache::longEdge(oriented);
    const int need = SlideshowAtlasPolicy::needEdge(slideshowTargetEdge());
    bool changed = false;
    if (m_ss.isFromPath(path)
        && SlideshowPhasePolicy::acceptOrientedUpgrade(
               incoming, ImageCache::longEdge(m_ss.fromImageRef()),
               m_ss.isFromContentApplied())) {
        m_ss.setFromImage(oriented, true);
        ImageCache::stampDebugOverlayIfEnabled(&m_ss.fromImageMutable(), path);
        m_ssDwell.setSourceImage(m_ss.fromImageRef());
        // Orient may swap aspect — drop atlas built from the unoriented sample.
        if (m_ssDwell.hasAtlas() && m_ssDwell.atlasRef().height() > 0
            && oriented.height() > 0) {
            if (SlideshowMotionGeometry::aspectMismatch(
                    qreal(m_ssDwell.atlasRef().width()), qreal(m_ssDwell.atlasRef().height()),
                    qreal(oriented.width()), qreal(oriented.height()))) {
                invalidateDwellAtlasRebuilds();
                m_ssDwell.clearAtlasPixmap();
            }
        }
        const DwellAtlasParams params = dwellAtlasParams();
        const bool needAtlas =
            !m_ssDwell.hasAtlas()
            || incoming >= need
            || incoming > ThumtooCache::kGalleryLadderEdge
            || !SlideshowAtlasPolicy::coversSource(
                   m_ssDwell.atlasRef(), m_ssDwell.atlasScaleValue(), m_ssDwell.atlasVwValue(),
                   m_ssDwell.atlasVhValue(), params, oriented);
        if (needAtlas) {
            requestDwellAtlasRebuild();
        }
        changed = true;
    }
    if (m_ss.isToPath(path)
        && SlideshowPhasePolicy::acceptOrientedUpgrade(
               incoming, ImageCache::longEdge(m_ss.toImageRef()),
               m_ss.isToContentApplied())) {
        m_ss.setToImage(oriented, true);
        ImageCache::stampDebugOverlayIfEnabled(&m_ss.toImageMutable(), path);
        if (m_ss.hasToAtlas() && m_ss.toAtlasRef().height() > 0
            && oriented.height() > 0) {
            if (SlideshowMotionGeometry::aspectMismatch(
                    qreal(m_ss.toAtlasRef().width()), qreal(m_ss.toAtlasRef().height()),
                    qreal(oriented.width()), qreal(oriented.height()))) {
                m_ss.bumpToAtlasRebuildGeneration();
                m_ss.clearToAtlas();
            }
        }
        const DwellAtlasParams params = dwellAtlasParams();
        const bool needAtlas =
            !m_ss.hasToAtlas()
            || incoming >= need
            || incoming > ThumtooCache::kGalleryLadderEdge
            || !SlideshowAtlasPolicy::coversSource(
                   m_ss.toAtlasRef(), m_ss.toAtlasScaleValue(), m_ss.toAtlasVwValue(), m_ss.toAtlasVhValue(),
                   params, oriented);
        if (needAtlas) {
            requestToPhaseAtlasRebuild();
        }
        changed = true;
    }
    if (changed && viewport()) {
        viewport()->update();
    }
}

void ImageView::scheduleSlideshowPhaseBufferUpgrade(const QString &path, const QImage &image)
{
    // HQ→full mid-slide: clamp + orient off the GUI thread (never Smooth scale here).
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    if (!m_ss.isPhasePath(path)) {
        return;
    }
    // Cheap long-edge check before pool work; clamp itself runs off-GUI.
    const int incomingRaw = ImageCache::longEdge(image);
    const int targetEdge = slideshowTargetEdge();
    const int incoming = incomingRaw > targetEdge && targetEdge > 0 ? targetEdge : incomingRaw;
    if (!phaseBufferWantsSample(path, incoming)) {
        return;
    }

    WorkspaceItemState appState;
    const bool hasApp = snapshotSlideshowContentAppearance(path, &appState);
    const quint64 gen = m_ss.bumpPhaseUpgradeGeneration();
    const QPointer<ImageView> guard(this);
    const QString pathCopy = path;
    const QImage raw = image;
    const int edgeCap = targetEdge;

    QThreadPool::globalInstance()->start(
        [guard, pathCopy, raw, appState, hasApp, gen, edgeCap]() {
            QImage capped = ImageCache::clampToMaxEdge(raw, edgeCap);
            if (capped.isNull()) {
                capped = raw;
            }
            QImage out = capped;
            if (hasApp) {
                const QImage oriented = SessionAppearance::materializeDisplay(
                    capped, appState, SessionAppearance::PixelKind::SoftPreview);
                if (!oriented.isNull()) {
                    out = oriented;
                }
            }
            ImageView *view = guard.data();
            if (!view) {
                return;
            }
            QTimer::singleShot(0, view, [view, pathCopy, out, gen]() {
                view->finishSlideshowPhaseBufferUpgrade(pathCopy, out, gen);
            });
        },
        -1);
}

void ImageView::finishSlideshowAtlas(SlideshowAtlasKind kind, quint64 generation,
                                     const QImage &scaled, qreal atlasScale,
                                     int atlasVw, int atlasVh)
{
    if (scaled.isNull()) {
        return;
    }
    if (kind == SlideshowAtlasKind::From) {
        if (generation != m_ssDwell.atlasRebuildGenerationValue()) {
            return;
        }
        m_ssDwell.setAtlas(QPixmap::fromImage(scaled), atlasScale, atlasVw, atlasVh);
    } else {
        if (generation != m_ss.toAtlasRebuildGenerationValue()) {
            return;
        }
        m_ss.setToAtlas(QPixmap::fromImage(scaled), atlasScale, atlasVw, atlasVh);
    }
    if (viewport() && m_ssHud.isProgressActive()) {
        viewport()->update();
    }
}

void ImageView::finishDwellAtlasRebuild(quint64 generation, const QImage &scaled,
                                        qreal atlasScale, int atlasVw, int atlasVh)
{
    finishSlideshowAtlas(SlideshowAtlasKind::From, generation, scaled, atlasScale,
                         atlasVw, atlasVh);
}

void ImageView::requestSlideshowAtlas(SlideshowAtlasKind kind)
{
    // Scale off the GUI thread; keep the previous atlas until finish assigns.
    if (!viewport() || m_ssHud.isNavHot()) {
        return;
    }
    const QImage *source = (kind == SlideshowAtlasKind::From)
                               ? &m_ssDwell.sourceImageRef()
                               : &m_ss.toImageRef();
    if (!source || source->isNull()) {
        return;
    }
    const DwellAtlasParams params = dwellAtlasParams();
    if (!params.valid) {
        return;
    }
    const QPixmap *atlas = (kind == SlideshowAtlasKind::From) ? &m_ssDwell.atlasRef() : &m_ss.toAtlasRef();
    const qreal aScale = (kind == SlideshowAtlasKind::From) ? m_ssDwell.atlasScaleValue() : m_ss.toAtlasScaleValue();
    const int aVw = (kind == SlideshowAtlasKind::From) ? m_ssDwell.atlasVwValue() : m_ss.toAtlasVwValue();
    const int aVh = (kind == SlideshowAtlasKind::From) ? m_ssDwell.atlasVhValue() : m_ss.toAtlasVhValue();
    if (SlideshowAtlasPolicy::coversSource(*atlas, aScale, aVw, aVh, params,
                                           *source)) {
        return;
    }

    const quint64 gen = (kind == SlideshowAtlasKind::From)
                            ? m_ssDwell.bumpAtlasRebuildGeneration()
                            : m_ss.bumpToAtlasRebuildGeneration();
    const QImage src = *source;
    const int longCap = params.longCap;
    const qreal keyScale = params.keyScale;
    const int vw = params.vw;
    const int vh = params.vh;
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start(
        [guard, src, longCap, keyScale, vw, vh, gen, kind]() {
            if (src.isNull()) {
                return;
            }
            // Always Smooth on the pool thread. FastTransformation upscales
            // soft samples to nearest-neighbour garbage that stays on screen
            // for the whole dwell when coverage skips a later rebuild.
            QImage scaled = src.scaled(longCap, longCap, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
            ImageView *view = guard.data();
            if (scaled.isNull() || !view) {
                return;
            }
            QTimer::singleShot(0, view, [view, gen, scaled, keyScale, vw, vh, kind]() {
                view->finishSlideshowAtlas(kind, gen, scaled, keyScale, vw, vh);
            });
        },
        -1);
}

void ImageView::requestDwellAtlasRebuild()
{
    requestSlideshowAtlas(SlideshowAtlasKind::From);
}

void ImageView::requestToPhaseAtlasRebuild()
{
    requestSlideshowAtlas(SlideshowAtlasKind::To);
}

void ImageView::onSlideshowRasterReady(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    const int incoming = ImageCache::longEdge(image);
    // Host ImageCache is often already updated by noteDelivery / ladderReady
    // *before* this runs. Comparing incoming to the cache edge skipped every
    // upgrade and left m_ssFrom/To stuck on LQIP/soft until re-enter.
    const int hadPhase =
        (m_ss.isFromPath(path)) ? ImageCache::longEdge(m_ss.fromImageRef())
        : (m_ss.isToPath(path))  ? ImageCache::longEdge(m_ss.toImageRef())
                                 : 0;
    putSlideshowRaster(path, image);

    const bool isPhasePath = m_ss.isPhasePath(path);
    if (!isPhasePath) {
        return;
    }

    if (phaseBufferWantsSample(path, incoming)) {
        qCDebug(lcSlideshow).nospace()
            << "[slideshow] raster-ready " << QFileInfo(path).fileName()
            << " " << image.width() << "x" << image.height()
            << " (phase was " << hadPhase << ")";
        // Phase buffer upgrade (clamp + orient + atlas) is deferred off this stack
        // so ladderReady does not block the pure-phase clock.
        scheduleSlideshowPhaseBufferUpgrade(path, image);
    } else if (viewport() && m_ssHud.isProgressActive()) {
        viewport()->update();
    }

    // Climb while the *phase buffer* is still short of target (not only when
    // the host cache edge increases).
    if (m_pathRaster) {
        const int target = cappedDisplayEdgeForPath(path, slideshowTargetEdge());
        const int need = SlideshowAtlasPolicy::needEdge(target);
        const int phaseHave =
            (m_ss.isFromPath(path)) ? ImageCache::longEdge(m_ss.fromImageRef())
                                   : ImageCache::longEdge(m_ss.toImageRef());
        if (phaseHave < need || incoming < need) {
            const auto policy =
                PathRasterService::ClimbPolicy::SoftDisplay;
            m_pathRaster->ensure(path, target, logicalSizeForPath(path), policy);
        }
    }
}


void ImageView::bindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id,
                                            const QString &path)
{
    if (!id) {
        return;
    }
    unbindSlideshowPhaseSurface(id);
    if (path.isEmpty()) {
        return;
    }
    *id = m_displaySurfaces.bind(
        DisplaySurface::Kind::SlideshowPhase, path, kInvalidSessionImageId);
}

void ImageView::unbindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id)
{
    if (!id || *id == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    m_displaySurfaces.unbind(*id);
    *id = DisplaySurface::kInvalidSurfaceId;
}

void ImageView::slideshowPhaseSurfaceTick()
{
    // ImageFocus: never. Slideshow phase buffers only, while a transition is live.
    if (!m_ssHud.isProgressActive()) {
        return;
    }

    auto drivePhase = [this](DisplaySurface::SurfaceId *sid, const QString &path,
                             int shownEdge) {
        if (path.isEmpty()) {
            return;
        }
        if (!sid || *sid == DisplaySurface::kInvalidSurfaceId) {
            bindSlideshowPhaseSurface(sid, path);
        }
        if (!sid || *sid == DisplaySurface::kInvalidSurfaceId) {
            return;
        }
        const int target = slideshowTargetEdge();
        const int hostEdge = DisplayQuality::hostLongEdge(path);
        const bool pending =
            m_pathRaster && m_pathRaster->isClimbPending(path);
        m_displaySurfaces.setNeed(*sid, target);
        m_displaySurfaces.setHostLongEdge(*sid, hostEdge);
        m_displaySurfaces.setClimbPending(*sid, pending);
        DisplaySurface::AttachedKind ak = DisplaySurface::AttachedKind::None;
        if (shownEdge > 0) {
            ak = (shownEdge >= target)
                ? DisplaySurface::AttachedKind::FullSource
                : DisplaySurface::AttachedKind::SoftPreview;
        }
        m_displaySurfaces.setAttached(*sid, ak, shownEdge, ContentXform::Value{});
        const DisplaySurface::Action act = m_displaySurfaces.evaluate(*sid);
        using AT = DisplaySurface::ActionType;
        if (act.type == AT::None) {
            return;
        }
        if (act.type == AT::AttachSoft || act.type == AT::AttachFull
            || act.type == AT::ScheduleAsyncMaterialize) {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                scheduleSlideshowPhaseBufferUpgrade(path, host);
            }
            return;
        }
        if (act.type == AT::ScheduleClimb && m_pathRaster) {
            const auto policy =
                PathRasterService::ClimbPolicy::SoftDisplay;
            m_pathRaster->ensure(
                path, target, logicalSizeForPath(path), policy);
        }
    };
    drivePhase(&m_ss.fromSurfaceRef(), m_ss.fromPathRef(), ImageCache::longEdge(m_ss.fromImageRef()));
    drivePhase(&m_ss.toSurfaceRef(), m_ss.toPathRef(), ImageCache::longEdge(m_ss.toImageRef()));
}


QString ImageView::slideshowPrefetchHudLine() const
{
    // Ladder edge chips ("Loading 1024→2048") were noisy and not actionable —
    // especially with HUD off. Prefetch still runs; status is not shown here.
    Q_UNUSED(m_ssHud.isProgressActive());
    return {};
}

QString ImageView::loadingStatusHudLine() const
{
    // Dedicated HUD line: job queue + cache vs file/archive + weak tiles.
    QString core = ThumtooCache::loadingBreakdownLabel();
    int blank = 0;
    int weak = 0;
    if (isGalleryMode()) {
        for (ImageItem *item : m_items) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            if (!item->hasDisplayPixels()) {
                ++blank;
            } else if (item->displayPixelLongEdge() > 0
                       && item->displayPixelLongEdge() < 96) {
                ++weak;
            }
        }
    }
    QStringList extra;
    if (blank > 0) {
        extra << tr("%1 blank").arg(blank);
    }
    if (weak > 0) {
        extra << tr("%1 quick preview").arg(weak);
    }
    if (core.isEmpty() && extra.isEmpty()) {
        return {};
    }
    if (core.isEmpty()) {
        return tr("Loading · %1").arg(extra.join(QStringLiteral(" · ")));
    }
    if (extra.isEmpty()) {
        return core;
    }
    return core + QStringLiteral(" · ") + extra.join(QStringLiteral(" · "));
}

QImage ImageView::slideshowRaster(const QString &path) const
{
    // Unoriented host samples only — not item->sourceImage() (appearance baked).
    return path.isEmpty() ? QImage() : ImageCache::get(path);
}

QImage ImageView::slideshowFullIfReady(const QString &path) const
{
    return slideshowRaster(path);
}

QImage ImageView::slideshowSoftPlaceholder(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    QImage have = slideshowRaster(path);
    if (!have.isNull()) {
        return have;
    }
    // Any host sample (incl. filmstrip soft), then durable LQIP. GUI-safe —
    // never loadThumbnail / native decode here. Schedule soft for the next tick.
    const int edge = slideshowTargetEdge();
    QImage soft = ImageCache::get(path);
    if (soft.isNull()) {
        soft = ThumtooCache::cachedLqipImage(path);
    }
    if (soft.isNull()) {
        // LQIP/cache miss: PathRaster SoftDisplay → TileSynth or pyramid.
        // Fallback without PathRaster: same (never PreferCache soft encode).
        if (m_pathRaster) {
            const auto policy =
                PathRasterService::ClimbPolicy::SoftDisplay;
            m_pathRaster->ensure(path, edge, logicalSizeForPath(path), policy);
        } else if (ThumtooCache::isAvailable()) {
            (void)ThumtooCache::scheduleTileSynthOrPyramid(
                path, ThumtooCache::kGalleryLadderEdge);
        }
        return {};
    }
    soft = ImageCache::clampToMaxEdge(soft, edge);
    putSlideshowRaster(path, soft);
    return soft;
}

SessionImageId ImageView::sessionIdForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return kInvalidSessionImageId;
    }
    // Prefer ordered session row (slideshow / gallery path list).
    {
        const SessionImageId ordered = m_pathOrderBook.firstIdForPath(path);
        if (ordered != kInvalidSessionImageId) {
            return ordered;
        }
    }
    // Image-mode slideshow: current session cursor when path matches.
    if (m_sessionId.hasCurrentId()) {
        if (ImageItem *it = findItemBySessionId(m_sessionId.currentIdValue())) {
            if (it->path() == path) {
                return m_sessionId.currentIdValue();
            }
        }
        if (classicPath() == path || currentPath() == path) {
            return m_sessionId.currentIdValue();
        }
    }
    return kInvalidSessionImageId;
}

QImage ImageView::orientSlideshowImage(const QImage &raw, const QString &path) const
{
    // SessionAppearanceStore is sole content truth (CROP_MODE.md): flips, turns, crop.
    if (raw.isNull() || path.isEmpty()) {
        return raw;
    }
    WorkspaceItemState app;
    if (!snapshotSlideshowContentAppearance(path, &app)
        || !SessionAppearance::hasContentAppearance(app)) {
        return raw;
    }
    QImage soft = raw;
    if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
        soft = ImageCache::clampToMaxEdge(
            soft, ContentXform::kGuiMaterializeMaxEdge);
    }
    const QImage oriented = SessionAppearance::materializeDisplay(
        soft, app, SessionAppearance::PixelKind::SoftPreview);
    return oriented.isNull() ? raw : oriented;
}

QImage ImageView::slideshowSampleUnoriented(const QString &path) const
{
    // Host sample (any soft) + LQIP — no flip/rotate, no loadThumbnail (GUI-safe).
    const int edge = slideshowTargetEdge();
    QImage img = slideshowRaster(path);
    if (img.isNull()) {
        img = ImageCache::get(path);
    }
    if (img.isNull()) {
        img = ThumtooCache::cachedLqipImage(path);
        // Const method: do not put into ImageCache here; soft placeholder may.
    }
    return ImageCache::clampToMaxEdge(img, edge);
}

QImage ImageView::slideshowPixelsForPath(const QString &path)
{
    // Oriented sample for callers that need correct content orientation now.
    // Phase arm uses the unoriented path + async upgrade to avoid GUI hitches.
    const QImage img = slideshowSampleUnoriented(path);
    if (img.isNull()) {
        QImage soft = slideshowSoftPlaceholder(path);
        soft = ImageCache::clampToMaxEdge(soft, slideshowTargetEdge());
        return orientSlideshowImage(soft, path);
    }
    return orientSlideshowImage(img, path);
}

void ImageView::pruneZoomBlurOutsidePhasePair(const QString &fromPath, const QString &toPath)
{
    const int vw = m_ssZoomBlur.viewportWidth() > 0 ? m_ssZoomBlur.viewportWidth()
                                    : (viewport() ? viewport()->width() : 0);
    const int vh = m_ssZoomBlur.viewportHeight() > 0 ? m_ssZoomBlur.viewportHeight()
                                    : (viewport() ? viewport()->height() : 0);
    // Keep lastGood across path changes — previous underlay holds until the
    // new key finishes (paintZoomBlurUnderlay draws it).
    ZoomBlur::pruneOutsidePair(&m_ssZoomBlur,
                               ZoomBlur::key(fromPath, vw, vh),
                               ZoomBlur::key(toPath, vw, vh));
}

void ImageView::schedulePhaseZoomBlur(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull() || !viewport()) {
        return;
    }
    const QSize vs = viewport()->size();
    const qint64 key = ZoomBlur::key(path, vs.width(), vs.height());
    if (key != 0) {
        scheduleZoomBlurBuild(image, vs.width(), vs.height(), key);
    }
}

void ImageView::captureMotionBiasesForPath(const QString &path, const QImage &image,
                                           QPointF *outA, QPointF *outB)
{
    if (!outA || !outB) {
        return;
    }
    const QPointF saveA = m_ssDwell.biasAPoint();
    const QPointF saveB = m_ssDwell.biasBPoint();
    const QPointF saveDir = m_ssDwell.travelDirPoint();
    const qreal saveSign = m_ssDwell.motionSignValue();
    const bool saveV = m_ssDwell.hasBias();
    m_ssDwell.clearBias();
    pickInterestingMotionBiases(qHash(path), image);
    *outA = m_ssDwell.biasAPoint();
    *outB = m_ssDwell.biasBPoint();
    if (saveV) {
        m_ssDwell.applyBias(saveA, saveB, saveDir, saveSign);
    } else {
        m_ssDwell.clearBias();
    }
}

void ImageView::ensureSlideshowMotionTimer()
{
    if (m_motionTimer) {
        return;
    }
    m_motionTimer = new QTimer(this);
    m_motionTimer->setTimerType(Qt::PreciseTimer);
    m_motionTimer->setInterval(SlideshowProgressHud::kMotionTickMs);
    connect(m_motionTimer, &QTimer::timeout, this, &ImageView::tickSlideshowMotion);
}

bool ImageView::shouldPromoteSlideshowToAsFrom(const QString &fromPath) const
{
    // Spec: B moves through transition *and* its following interval.
    // When dwell becomes B after A+B fade, promote B's motion — do not restart at 0.
    return !fromPath.isEmpty() && fromPath == m_ss.toPathRef() && m_ss.isToMotionClockRunning();
}

void ImageView::promoteSlideshowFromToPhase(const QString &fromPath)
{
    if (m_ss.hasToImage()) {
        m_ss.setFromImage(m_ss.toImageRef(), m_ss.isToContentApplied());
    } else {
        // Oriented path when to-phase missing (slideshowPixelsForPath materializes).
        m_ss.setFromImage(slideshowPixelsForPath(fromPath), true);
    }
    m_ssDwell.applyBias(m_ss.toBiasAPoint(), m_ss.toBiasBPoint(), m_ssDwell.travelDirPoint(), m_ssDwell.motionSignValue());
    m_ssDwell.setBiasPath(fromPath);
    m_ss.promoteFromMotionFromTo();
    m_ssDwell.setMotionT(m_ss.fromMotionTValue());
    // Keep the to-atlas as the from/dwell atlas — clearing it forced multi-MP
    // drawImage every frame until rebuild (visible frame drops on promote).
    if (m_ss.hasToAtlas()) {
        m_ssDwell.setAtlas(m_ss.toAtlasRef(), m_ss.toAtlasScaleValue(), m_ss.toAtlasVwValue(),
                           m_ss.toAtlasVhValue(), m_ss.toAtlasRebuildGenerationValue());
    }
}

void ImageView::startSlideshowFromPhase(const QString &fromPath)
{
    // Unoriented clamp only — ContentXform orient + atlas run async
    // (prepareSlideshowFromDwell → scheduleSlideshowPhaseBufferUpgrade).
    // Sync orient of multi-MP on every ←/→ dropped frames; same-edge orient must
    // still be accepted (see phaseBufferWantsSample / m_ss.isFromContentApplied()).
    m_ss.setFromImage(slideshowSampleUnoriented(fromPath), false);
    if (!m_ss.hasFromImage() && !fromPath.isEmpty()) {
        m_ss.setFromImage(ImageCache::clampToMaxEdge(
            slideshowSoftPlaceholder(fromPath), slideshowTargetEdge()), false);
    }
    // Always orient a ≤512 stand-in on the GUI when appearance is present so
    // the first paint is correct. Larger samples are clamped for this pass;
    // sharper unoriented host climbs via scheduleSlideshowPhaseBufferUpgrade
    // (must pass *host* raw — never re-materialize an oriented phase buffer).
    if (!fromPath.isEmpty() && m_ss.hasFromImage()) {
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(fromPath, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            QImage soft = m_ss.fromImageRef();
            if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
                soft = ImageCache::clampToMaxEdge(
                    soft, ContentXform::kGuiMaterializeMaxEdge);
            }
            const QImage oriented = SessionAppearance::materializeDisplay(
                soft, app, SessionAppearance::PixelKind::SoftPreview);
            if (!oriented.isNull()) {
                m_ss.setFromImage(oriented, true);
            }
        }
    }
    if (!fromPath.isEmpty()) {
        (void)ensureSlideshowLogicalSize(fromPath);
        if (!m_ss.hasFromImage()
            || ImageCache::longEdge(m_ss.fromImageRef())
                   < SlideshowAtlasPolicy::needEdge(slideshowTargetEdge())) {
            preloadSlideshowImage(fromPath);
        }
        m_ssDwell.clearBias();
        pickInterestingMotionBiases(qHash(fromPath), m_ss.fromImageRef());
        m_ssDwell.setBiasPath(fromPath);
    }
    m_ss.startFromMotionClock();
    m_ss.setFromMotionT(0.0);
    m_ssDwell.setMotionT(0.0);
}

void ImageView::prepareSlideshowFromDwell(const QString &fromPath)
{
    m_ssDwell.setSourceImage(m_ss.fromImageRef());
    if (!m_ss.hasFromImage()) {
        return;
    }
    m_ss.bumpPhaseUpgradeGeneration(); // drop mid-slide upgrades for previous path

    // Rapid user ←/→ (nav hot): phase buffer already holds soft/best cache.
    // Do not schedule atlas rebuild, zoom-blur, PreferCache, or phase-buffer
    // upgrades per key — those flood the thread pool and PathRaster and make
    // the show progressively worse the longer a key is held. Settle (MainWindow
    // timer) clears nav-hot and re-arms full quality for the current path only.
    if (m_ssHud.isNavHot()) {
        invalidateDwellAtlasRebuilds();
        m_ssDwell.clearAtlasPixmap();
        return;
    }

    // Keep atlas only when promote carried oriented continuity for the *same*
    // sample. Fresh arm (contentApplied false) or aspect mismatch: drop atlas
    // so paintMotionCover blits the live sample without stretch for a frame.
    const bool keepAtlas = m_ss.isFromContentApplied()
        && m_ssDwell.hasAtlas()
        && SlideshowAtlasPolicy::coversSource(
               m_ssDwell.atlasRef(), m_ssDwell.atlasScaleValue(), m_ssDwell.atlasVwValue(),
               m_ssDwell.atlasVhValue(), dwellAtlasParams(), m_ss.fromImageRef());
    if (!keepAtlas) {
        invalidateDwellAtlasRebuilds();
        m_ssDwell.clearAtlasPixmap();
    }
    // Async atlas — never scale multi-MP on the GUI during ←/→ or phase arm.
    // paintMotionCover falls back to drawImage until the atlas is ready.
    requestDwellAtlasRebuild();
    schedulePhaseZoomBlur(fromPath, m_ss.fromImageRef());
    // Sharper climb from *unoriented* host only. Passing the phase buffer here
    // re-materialized an already-oriented sample (double turns/flips → glitch).
    if (!fromPath.isEmpty()) {
        QImage host = ImageCache::get(fromPath);
        if (host.isNull()) {
            host = slideshowSampleUnoriented(fromPath);
        }
        if (!host.isNull()) {
            scheduleSlideshowPhaseBufferUpgrade(fromPath, host);
        }
    }
}

void ImageView::armSlideshowMotionClock(int pathMs)
{
    if (m_ssSettings.isMotionOff() || pathMs < 250) {
        return;
    }
    ensureSlideshowMotionTimer();
    m_ssDwell.setMotionActive(true);
    m_ssDwell.setDurationMs(pathMs);
    // While paused, arm motion state but do not run the timer.
    if (!m_ssDwell.isMotionPaused()) {
        m_motionTimer->start();
    }
}

void ImageView::armSlideshowFromPhase(const QString &fromPath, int pathMs)
{
    const bool promote = shouldPromoteSlideshowToAsFrom(fromPath);
    m_ss.setFromPath(fromPath);
    bindSlideshowPhaseSurface(&m_ss.fromSurfaceRef(), fromPath);
    if (promote) {
        promoteSlideshowFromToPhase(fromPath);
    } else {
        startSlideshowFromPhase(fromPath);
    }
    prepareSlideshowFromDwell(fromPath);
    armSlideshowMotionClock(pathMs);
    qCDebug(lcSlideshow).nospace()
        << "[slideshow] phase-from "
        << QFileInfo(fromPath).fileName()
        << " " << m_ss.fromImageRef().width() << "x" << m_ss.fromImageRef().height()
        << (promote ? " (continue)" : " (start)");
}

void ImageView::armSlideshowToPhase(const QString &toPath)
{
    if (toPath.isEmpty()) {
        m_ss.clearToPath();
        unbindSlideshowPhaseSurface(&m_ss.toSurfaceRef());
        m_ss.clearToImage();
        m_ss.bumpToAtlasRebuildGeneration();
        m_ss.clearToAtlas();
        m_ss.setToMotionClockRunning(false);
        m_ss.setToMotionT(0.0);
        return;
    }
    m_ss.setToPath(toPath);
    bindSlideshowPhaseSurface(&m_ss.toSurfaceRef(), toPath);
    (void)ensureSlideshowLogicalSize(toPath);
    m_ss.setToImage(slideshowSampleUnoriented(toPath), false);
    if (!m_ss.hasToImage()) {
        m_ss.setToImage(ImageCache::clampToMaxEdge(
            slideshowSoftPlaceholder(toPath), slideshowTargetEdge()), false);
    }
    if (m_ss.hasToImage()) {
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(toPath, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            QImage soft = m_ss.toImageRef();
            if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
                soft = ImageCache::clampToMaxEdge(
                    soft, ContentXform::kGuiMaterializeMaxEdge);
            }
            const QImage oriented = SessionAppearance::materializeDisplay(
                soft, app, SessionAppearance::PixelKind::SoftPreview);
            if (!oriented.isNull()) {
                m_ss.setToImage(oriented, true);
            }
        }
    }
    // Cold to-path: kick preload immediately (do not wait for neighbour pump).
    if (!m_ss.hasToImage()
        || ImageCache::longEdge(m_ss.toImageRef()) < SlideshowAtlasPolicy::needEdge(slideshowTargetEdge())) {
        preloadSlideshowImage(toPath);
    }
    captureMotionBiasesForPath(toPath, m_ss.toImageRef(), &m_ss.toBiasARef(), &m_ss.toBiasBRef());
    m_ss.startToMotionClock();
    m_ss.setToMotionT(0.0);
    if (m_ss.hasToImage()) {
        schedulePhaseZoomBlur(toPath, m_ss.toImageRef());
    }
    m_ss.bumpToAtlasRebuildGeneration(); // drop stale to-atlas jobs
    m_ss.clearToAtlas();
    requestToPhaseAtlasRebuild();
    if (m_ss.hasToImage()) {
        {
            QImage host = ImageCache::get(toPath);
            if (host.isNull()) {
                host = slideshowSampleUnoriented(toPath);
            }
            if (!host.isNull()) {
                scheduleSlideshowPhaseBufferUpgrade(toPath, host);
            }
        }
    }
    qCDebug(lcSlideshow).nospace()
        << "[slideshow] phase-to "
        << QFileInfo(toPath).fileName()
        << " " << m_ss.toImageRef().width() << "x" << m_ss.toImageRef().height();
}

int ImageView::slideshowPathDurationMs() const
{
    return SlideshowClocks::pathDurationMs(m_ssHud.progressInterval(),
                                           m_ssSettings.transitionDuration());
}

void ImageView::warmZoomBlurForCurrentPhase()
{
    // Skip while user is key-repeating — builds fight soft decode.
    if (!viewport() || m_ssHud.isNavHot()
        || !m_ssSettings.isZoomBlurLetterbox()) {
        return;
    }
    const QSize vs = viewport()->size();
    if (vs.width() <= 0 || vs.height() <= 0) {
        return;
    }
    auto warm = [&](const QString &path, const QImage &img) {
        if (path.isEmpty() || img.isNull()) {
            return;
        }
        const qint64 key = qint64(qHash(path))
            ^ (qint64(vs.width()) << 16) ^ qint64(vs.height());
        scheduleZoomBlurBuild(img, vs.width(), vs.height(), key);
    };
    warm(m_ss.fromPathRef(), !m_ss.hasFromImage() ? m_ssDwell.sourceImageRef() : m_ss.fromImageRef());
    warm(m_ss.toPathRef(), m_ss.toImageRef());
}

bool ImageView::applySlideshowFadeProgressOnly(qreal fadeT)
{
    // Pure-phase clock ticks at 16ms with unchanged from/to — only advance fade.
    if (qFuzzyCompare(fadeT, m_ss.fadeTValue()) || (fadeT < 0.0 && m_ss.inDwell())) {
        return false;
    }
    m_ss.setFadeBlend(fadeT);
    if (viewport()) {
        viewport()->update();
    }
    return true;
}

void ImageView::updateSlideshowPhaseMotionProgress(int pathMs)
{
    // T ∈ [0,1] is authority; clocks only measure Δt for integration.
    SlideshowClocks::advancePhaseMotion(&m_ss, &m_ssDwell, pathMs);
}

void ImageView::setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT)
{
    if (!m_ssHud.isProgressActive()) {
        return;
    }

    const bool fromChanged = (fromPath != m_ss.fromPathRef());
    const bool toChanged = (toPath != m_ss.toPathRef());
    // Unchanged paths: never hide underlay / full scene refresh every 16ms.
    if (!fromChanged && !toChanged) {
        (void)applySlideshowFadeProgressOnly(fadeT);
        return;
    }
    // Do NOT bump ZoomBlur generation here — that cancelled the incoming
    // slide's underlay build every transition and caused letterbox flicker.
    // Evict only slots that are neither from nor to; in-flight jobs for the
    // new pair keep running.
    pruneZoomBlurOutsidePhasePair(fromPath, toPath);
    const int pathMs = slideshowPathDurationMs();

    // Phase buffers lock at from/toChanged; preload fills ImageCache for next.
    // Keep m_ss.rasterPending look-ahead (do not clear — cancelled +2/+3 warm-up).
    if (fromChanged) {
        armSlideshowFromPhase(fromPath, pathMs);
    }
    if (toPath.isEmpty()) {
        armSlideshowToPhase(QString()); // clear
    } else if (toChanged) {
        armSlideshowToPhase(toPath);
    }
    updateSlideshowPhaseMotionProgress(pathMs);
    m_ss.setFadeBlend(fadeT);

    if (const char *dbg = std::getenv("BILTOO_DEBUG_SLIDESHOW");
        (dbg && dbg[0] && dbg[0] != '0')
        || (std::getenv("THUMTOO_DEBUG")
            && std::getenv("THUMTOO_DEBUG")[0]
            && std::getenv("THUMTOO_DEBUG")[0] != '0')) {
        fprintf(stderr,
                "biltoo/ss: phase from=%s to=%s fade=%.3f fromImg=%dx%d toImg=%dx%d\n",
                qPrintable(QFileInfo(fromPath).fileName()),
                qPrintable(QFileInfo(toPath).fileName()),
                fadeT,
                m_ss.fromImageRef().width(), m_ss.fromImageRef().height(),
                m_ss.toImageRef().width(), m_ss.toImageRef().height());
    }

    if (!m_ssHud.isNavHot()) {
        warmZoomBlurForCurrentPhase();
    }
    hideSlideshowUnderlay();
    if (viewport()) {
        viewport()->update();
    }
}

qreal ImageView::slideshowMotionHeadroom() const
{
    return SlideshowAtlasPolicy::motionHeadroom(
        m_ssSettings.currentMotion(), m_ssSettings.currentPanZoomFactor(), m_ssHud.isProgressActive());
}

int ImageView::slideshowTargetEdge() const
{
    if (!viewport()) {
        return SlideshowAtlasPolicy::targetLongEdge(false, 0, 0, 1.0, 1.0);
    }
    const QSize vs = viewport()->size();
    return SlideshowAtlasPolicy::targetLongEdge(
        true, vs.width(), vs.height(), devicePixelRatioF(),
        slideshowMotionHeadroom());
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

qreal ImageView::slideshowZoomBaseScale(const QSize &logical, int vw, int vh) const
{
    return SlideshowAtlasPolicy::zoomBaseScale(m_ssSettings.currentZoom(), logical, vw, vh);
}

void ImageView::setSlideshowNavHot(bool hot)
{
    if (!m_ssHud.setNavHot(hot)) {
        return;
    }
    // Do NOT invalidateZoomBlurQueue here — keep the previous underlay until a
    // new key's blur is ready (solid flash on every ←/→ was the bug).
    if (hot) {
        // Drop off-canvas prefetch sessions: their InFlight tiles compete with
        // settle soft/climb after a long key-repeat burst.
        m_tileNeighborPrefetch.clear();
    }
}

void ImageView::pumpSlideshowPreloadQueue()
{
    // Start at most one pending path now that an inflight slot freed.
    const int needEdge = SlideshowAtlasPolicy::needEdge(slideshowTargetEdge());
    QString next;
    while (m_ss.takeNextRasterPending(&next)) {
        if (m_ss.rasterInflightContains(next)) {
            continue;
        }
        if (ImageCache::adequate(slideshowRaster(next), needEdge)) {
            continue;
        }
        preloadSlideshowImage(next);
        break;
    }
}

void ImageView::finishSlideshowPreload(const QString &path, const QImage &image)
{
    // Legacy pool-preload completion — climb is owned by PathRasterService.
    m_ss.removeRasterInflight(path);
    if (!image.isNull()) {
        if (m_pathRaster) {
            m_pathRaster->noteDelivery(path, 0, image);
        } else {
            ImageCache::put(path, image);
        }
        onSlideshowRasterReady(path, image);
        qCDebug(lcSlideshow).nospace()
            << "[slideshow] preload-ready " << QFileInfo(path).fileName()
            << " " << image.width() << "x" << image.height();
    }
    pumpSlideshowPreloadQueue();
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::preloadSlideshowImage(const QString &path)
{
    if (path.isEmpty() || !m_pathRaster) {
        return;
    }
    // User key-repeat: no PathRaster EscalateToFull per visited path.
    if (m_ssHud.isNavHot()) {
        return;
    }
    const int targetEdge = cappedDisplayEdgeForPath(path, slideshowTargetEdge());
    const int need = SlideshowAtlasPolicy::needEdge(targetEdge);
    const QSize native = logicalSizeForPath(path);
    const QImage cached = ImageCache::get(path);
    const int haveEdge = ImageCache::longEdge(cached);

    // Quiet no-ops: look-ahead is once per toIdx; still guard re-entry.
    if (need > 0 && ImageCache::adequate(cached, need)) {
        if (!cached.isNull()) {
            onSlideshowRasterReady(path, cached);
        }
        return;
    }
    if (m_pathRaster->isClimbPending(path)) {
        if (!cached.isNull()) {
            onSlideshowRasterReady(path, cached);
        }
        return;
    }
    // PreferCache plateau with Full already done for this want — stop.
    if (m_pathRaster->isGaveUp(path) && !m_pathRaster->isClimbPending(path)) {
        // SoftDisplay plateau — do not escalate to Full native.
    }

    qCDebug(lcSlideshow).nospace()
        << "[slideshow] preload-ensure " << QFileInfo(path).fileName()
        << " edge=" << targetEdge
        << " have=" << haveEdge;

    // SoftDisplay only: PreferCache/TileSynth at screen-fit edge. Never Full
    // native whole-frame (that pulled multi-MP samples for every slide).
    m_pathRaster->ensure(path, targetEdge, native,
                         PathRasterService::ClimbPolicy::SoftDisplay);
    // Warm durable tiles into the shared path TileMemoryCache (TileLodRegistry)
    // so SoftDisplay TileSynth and later Image/Gallery views reuse them.
    if (!ThumtooCache::hasDurableTilesKnown(path)) {
        (void)ThumtooCache::scheduleTilePyramid(path);
    }
    tickPrimaryTileLod(TileLoadCoordinator::kDefaultTickBudget);

    const QImage have = ImageCache::get(path);
    if (!have.isNull()) {
        onSlideshowRasterReady(path, have);
    }
}

DwellAtlasParams ImageView::dwellAtlasParams() const
{
    // Atlas size is a function of the *viewport* and motion headroom only —
    // not of the source raster's pixel dimensions. Camera dest is aspect-based;
    // the atlas is just a sharp enough texture to sample under max zoom.
    if (!viewport()) {
        return {};
    }
    return SlideshowAtlasPolicy::makeParams(
        viewport()->width(), viewport()->height(), slideshowMotionHeadroom());
}

void ImageView::invalidateDwellAtlasRebuilds()
{
    m_ssDwell.bumpAtlasRebuildGeneration();
}

void ImageView::ensureMotionAtlas(const QImage &image, QPixmap *atlas,
                                  qreal *atlasScale, int *atlasVw, int *atlasVh) const
{
    if (!atlas || !atlasScale || !atlasVw || !atlasVh || image.isNull() || !viewport()) {
        return;
    }
    // Rapid keyboard flip: skip atlas rebuild; paintMotionCover falls back to
    // drawImage. Avoids a scale per key on the GUI thread.
    if (m_ssHud.isNavHot()) {
        return;
    }
    const DwellAtlasParams params = dwellAtlasParams();
    if (!params.valid) {
        return;
    }
    if (SlideshowAtlasPolicy::coversSource(*atlas, *atlasScale, *atlasVw, *atlasVh,
                                           params, image)) {
        return;
    }
    const int srcLong = ContentXform::longEdge(image.size());
    // Upscale must be smooth; Fast on soft→viewport is nearest-neighbour mush.
    const auto mode = (srcLong >= params.longCap)
                          ? Qt::FastTransformation
                          : Qt::SmoothTransformation;
    QImage scaled = image.scaled(params.longCap, params.longCap, Qt::KeepAspectRatio,
                                 mode);
    if (scaled.isNull()) {
        *atlas = QPixmap();
        *atlasScale = 0.0;
        return;
    }
    *atlas = QPixmap::fromImage(std::move(scaled));
    *atlasScale = params.keyScale;
    *atlasVw = params.vw;
    *atlasVh = params.vh;
}

void ImageView::setSlideshowUnderlayVisible(bool visible)
{
    // Invariant: while a slideshow is in progress the underlay is never shown.
    if (m_ssHud.isProgressActive()) {
        visible = false;
    }
    if (ImageItem *item = targetItem()) {
        item->setVisible(visible);
    }
}

void ImageView::hideSlideshowUnderlay()
{
    // Hide every canvas item — new LoadReplace items default to visible and
    // were slipping past a single targetItem() hide (paint: underlayVisible=true).
    if (!m_scene) {
        return;
    }
    for (QGraphicsItem *gi : m_scene->items()) {
        if (qgraphicsitem_cast<ImageItem *>(gi)) {
            gi->setVisible(false);
        }
    }
}

namespace {


} // namespace

void ImageView::clearSlideshowZoomBlurSlots()
{
    // Drop cached underlays and cancel in-flight blur jobs (pad/letterbox/viewport).
    ZoomBlur::clearAllSlots(&m_ssZoomBlur);
    invalidateZoomBlurQueue();
}

void ImageView::invalidateZoomBlurQueue() const
{
    ZoomBlur::invalidateQueue(&m_ssZoomBlur);
}

bool ImageView::zoomBlurKeyCached(qint64 key) const
{
    return ZoomBlur::keyCached(m_ssZoomBlur, key);
}

bool ImageView::zoomBlurKeyInFlight(qint64 key) const
{
    return ZoomBlur::keyInFlight(m_ssZoomBlur, key);
}

int ImageView::claimZoomBlurFlightSlot(qint64 key) const
{
    // Allow up to two concurrent builds (outgoing + incoming underlay).
    return ZoomBlur::claimFlightSlot(&m_ssZoomBlur, key);
}

void ImageView::installZoomBlurResult(const QImage &blurred, qint64 key, quint64 gen)
{
    if (!ZoomBlur::installResult(&m_ssZoomBlur, QPixmap::fromImage(blurred), key, gen)) {
        return; // page flipped — discard
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::scheduleZoomBlurBuild(const QImage &image, int vw, int vh, qint64 key) const
{
    if (m_ssHud.isNavHot()) {
        return;
    }
    if (image.isNull() || vw < 1 || vh < 1 || key == 0) {
        return;
    }
    if (zoomBlurKeyCached(key) || zoomBlurKeyInFlight(key)) {
        return;
    }
    if (claimZoomBlurFlightSlot(key) < 0) {
        return;
    }
    const quint64 gen = m_ssZoomBlur.generationValue();
    // Snapshot pixels for the worker (avoid touching GUI QImage after return).
    const QImage src = image.copy();
    const QPointer<ImageView> guard(const_cast<ImageView *>(this));
    QThreadPool::globalInstance()->start([guard, src, vw, vh, key, gen]() {
        const QImage blurred = ZoomBlur::makeCover(src, vw, vh);
        ImageView *view = guard.data();
        if (blurred.isNull() || !view) {
            return;
        }
        QTimer::singleShot(0, view, [view, blurred, key, gen]() {
            view->installZoomBlurResult(blurred, key, gen);
        });
    });
}



void ImageView::paintZoomBlurUnderlay(QPainter *painter, const QImage &image,
                                      const QRect &viewportRect, qint64 stableKey) const
{
    if (!painter || image.isNull() || viewportRect.isEmpty()) {
        return;
    }
    const int vw = viewportRect.width();
    const int vh = viewportRect.height();
    // Stable identity: path key + viewport only.
    // Do NOT fold source width/height — soft→full upgrades would miss the
    // cache and re-blur mid-transition (the spike after the first fix).
    const qint64 key = stableKey ^ (qint64(vw) << 16) ^ qint64(vh);
    if (!m_ssZoomBlur.viewportMatches(vw, vh)) {
        // Viewport size change: drop sized slots; keep lastGood stretched until
        // async rebuild finishes (still better than solid flash).
        m_ssZoomBlur.clearUnderlays();
        m_ssZoomBlur.setViewportSize(vw, vh);
        invalidateZoomBlurQueue();
    }
    const int slot = m_ssZoomBlur.findCachedSlot(key);
    if (slot >= 0) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(viewportRect, m_ssZoomBlur.underlayAt(slot));
        return;
    }
    // Miss: schedule build only when not in a key-repeat burst (pool pressure).
    // Always keep painting the previous underlay until this key is ready —
    // solid pad on every path change was the "discarded blurry background" bug.
    if (!m_ssHud.isNavHot()) {
        scheduleZoomBlurBuild(image, vw, vh, key);
    }
    if (m_ssZoomBlur.hasLastGood()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(viewportRect, m_ssZoomBlur.lastGoodPixmap());
        return;
    }
    // Slideshow letterbox uses the dedicated pad colour; Image View content-blur
    // falls back to the Preferences / canvas primary colour.
    const QColor pad = m_ssSettings.isZoomBlurLetterbox()
        ? slideshowPadColor()
        : m_canvasBg.primaryColor();
    painter->fillRect(viewportRect, pad.isValid() ? pad : QColor(42, 42, 42));
}

QSize ImageView::resolveMotionLogicalSize(const QString &path) const
{
    // File-native owns motion dest size (SIZE.md). Sample is blit-only; using
    // sample aspect here made cover rect jump when LQIP → soft → full arrived.
    const QSize fileNative = logicalSizeForPath(path);
    if (isPositiveSize(fileNative) && !path.isEmpty()) {
        WorkspaceItemState app;
        if (snapshotSlideshowContentAppearance(path, &app)
            && SessionAppearance::hasContentAppearance(app)) {
            const QSize lay = ContentXform::layoutSize(fileNative, app);
            if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                return lay;
            }
        }
        return fileNative;
    }
    return QSize(kProvisionalLayoutLongEdge, kProvisionalLayoutLongEdge);
}

QRectF ImageView::computeMotionCoverDestRect(qreal iw, qreal ih, int vw, int vh,
                                             qreal motionT, QPointF biasA, QPointF biasB,
                                             const QString &path) const
{
    const QSize logical{int(iw), int(ih)};
    const qreal base = slideshowZoomBaseScale(logical, vw, vh);
    return SlideshowMotionGeometry::coverDestRect(
        m_ssSettings.currentMotion(), base, m_ssSettings.currentPanZoomFactor(), iw, ih, vw, vh,
        motionT, biasA, biasB, m_ssDwell.hasBias(), path);
}


tilelod::TileLodController *ImageView::slideshowTilesForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    if (m_ss.isFromPath(path)) {
        tilelod::TileLodController *ctrl = m_ss.ensureFromTiles();
        if (ctrl->path() != path) {
            ctrl->setPath(path);
        }
        return ctrl;
    }
    if (m_ss.isToPath(path)) {
        tilelod::TileLodController *ctrl = m_ss.ensureToTiles();
        if (ctrl->path() != path) {
            ctrl->setPath(path);
        }
        return ctrl;
    }
    return nullptr;
}

bool ImageView::paintSlideshowTiles(QPainter *painter, const QString &path,
                                    const QRectF &dest, const QImage &underlay) const
{
    if (!painter || path.isEmpty() || dest.isEmpty()) {
        return false;
    }
    tilelod::TileLodController *lod = slideshowTilesForPath(path);
    if (!lod) {
        return false;
    }
    QSize native = logicalSizeForPath(path);
    if (!isPositiveSize(native)) {
        native = ThumtooCache::cachedSize(path);
    }
    if (!isPositiveSize(native)) {
        return false;
    }
    const int longEdge = ContentXform::longEdge(native);
    if (longEdge < 256) {
        return false;
    }
    lod->setContentSize(native.width(), native.height(),
                        ThumtooCache::durableTileMinScale(path));
    // Full content into dest (same model as drawImage(dest, image)).
    const double dpc = SlideshowMotionGeometry::coverDevicePixelScale(
        QSizeF(dest.width(), dest.height()), native);
    if (!tilelod::TileLodController::shouldUseTiles(dpc, longEdge)) {
        return false;
    }
    lod->updateViewport(QRectF(0, 0, native.width(), native.height()), dpc, 0.0);
    (void)lod->tick(12);
    if (!lod->hasAnyTile()) {
        if (!ThumtooCache::hasDurableTilesKnown(path)) {
            (void)ThumtooCache::scheduleTilePyramid(path);
        }
        return false;
    }
    painter->save();
    painter->translate(dest.topLeft());
    painter->scale(SlideshowMotionGeometry::coverAxisScaleX(QSizeF(dest.size()), native),
                   SlideshowMotionGeometry::coverAxisScaleY(QSizeF(dest.size()), native));
    const bool drew = lod->paint(painter, underlay);
    painter->restore();
    return drew;
}

void ImageView::paintMotionCover(QPainter *painter, const QImage &image,
                                 qreal motionT, QPointF biasA, QPointF biasB,
                                 const QString &path) const
{
    if (!painter || image.isNull() || !viewport()) {
        return;
    }
    const int vw = ViewTransform::atLeast1(viewport()->width());
    const int vh = ViewTransform::atLeast1(viewport()->height());

    const QSize logical = resolveMotionLogicalSize(path);
    const qreal iw = qreal(logical.width());
    const qreal ih = qreal(logical.height());
    if (iw < 1.0 || ih < 1.0) {
        return;
    }

    const QRectF dest = computeMotionCoverDestRect(iw, ih, vw, vh, motionT, biasA, biasB, path);
    if (!dest.isValid() || dest.isEmpty()) {
        return;
    }

    // Tiles first (shared TileLodRegistry path cache) — same as Image/Gallery.
    if (paintSlideshowTiles(painter, path, dest, image)) {
        return;
    }

    // Prefer pre-scaled atlases matched by path (not QImage address — pure-phase
    // paint may pass temporaries). From/dwell → m_ssDwell.atlasRef(); to → m_ss.toAtlasRef().
    const QPixmap *atlas = nullptr;
    if (!path.isEmpty() && m_ss.isFromPath(path) && m_ssDwell.hasAtlas()) {
        atlas = &m_ssDwell.atlasRef();
    } else if (!path.isEmpty() && m_ss.isToPath(path) && m_ss.hasToAtlas()) {
        atlas = &m_ss.toAtlasRef();
    } else if (path.isEmpty() && m_ssDwell.hasAtlas()
               && (&image == &m_ssDwell.sourceImageRef() || &image == &m_ss.fromImageRef())) {
        atlas = &m_ssDwell.atlasRef();
    }

    // Stale atlas after ContentXform orient (aspect swap) stretches into dest.
    if (atlas && image.width() > 1 && image.height() > 1
        && atlas->width() > 1 && atlas->height() > 1) {
        if (SlideshowMotionGeometry::aspectMismatch(
                qreal(atlas->width()), qreal(atlas->height()),
                qreal(image.width()), qreal(image.height()))) {
            atlas = nullptr;
        }
    }
    if (atlas) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(dest, *atlas, atlas->rect());
    } else {
        // No atlas yet: smooth when the sample is in the display budget so soft
        // placeholders are not nearest-neighbour. Only skip Smooth for huge
        // native samples (rare during slideshow — atlas should cover those).
        const int srcLong = ContentXform::longEdge(image.size());
        const int budget = SlideshowMotionGeometry::atlasBudgetPx(vw, vh);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, srcLong <= budget);
        painter->drawImage(dest, image);
    }
}


QPixmap ImageView::renderMotionCoverPixmap(const QImage &image, qreal motionT,
                                           uint pathHash) const
{
    // Snapshot helper (rare). Prefer paintMotionCover on the live painter.
    if (image.isNull() || !viewport()) {
        return {};
    }
    const int vw = ViewTransform::atLeast1(viewport()->width());
    const int vh = ViewTransform::atLeast1(viewport()->height());
    QImage out(vw, vh, QImage::Format_ARGB32_Premultiplied);
    out.fill(slideshowPadColor());
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // pathHash was historical; recover path from phase when possible.
    QString path;
    if (pathHash != 0 && qHash(m_ss.toPathRef()) == pathHash) {
        path = m_ss.toPathRef();
    } else if (m_ss.hasFromPath()) {
        path = m_ss.fromPathRef();
    }
    paintMotionCover(&painter, image, motionT, m_ssDwell.biasAPoint(), m_ssDwell.biasBPoint(), path);
    painter.end();
    return QPixmap::fromImage(out);
}

void ImageView::maybeStartSlideshowMotion()
{
    if (m_ssSettings.isMotionOff() || !m_ssHud.isProgressActive()
        || !isImageMode()) {
        return;
    }
    // Already running — do not restart from 0.
    if (m_ssDwell.isMotionActive()) {
        return;
    }
    int duration = m_ssHud.progressInterval();
    if (duration < 250) {
        return;
    }
    // Continue from the dwell sample if soft-handoff already set one.
    const qreal initial = ViewTransform::clamp01(m_ssDwell.motionTValue()); // already clamped on set
    startSlideshowMotion(duration, initial);
}

bool ImageView::prepareSlideshowMotionDwell(ImageItem *item)
{
    // Ken Burns moves the *image* via blit, not the QGraphicsView camera.
    // First paint must be ContentXform-oriented; unoriented dwell was the
    // "garbage first frame" when this ran before setSlideshowPhase.
    const QString path = item->path();
    QImage host = slideshowSampleUnoriented(path);
    if (host.isNull()) {
        host = ImageCache::clampToMaxEdge(item->sourceImage(), slideshowTargetEdge());
    }
    if (host.isNull()) {
        return false;
    }
    QImage dwell = host;
    WorkspaceItemState app;
    if (!path.isEmpty()
        && snapshotSlideshowContentAppearance(path, &app)
        && SessionAppearance::hasContentAppearance(app)) {
        QImage soft = host;
        if (ImageCache::longEdge(soft) > ContentXform::kGuiMaterializeMaxEdge) {
            soft = ImageCache::clampToMaxEdge(
                soft, ContentXform::kGuiMaterializeMaxEdge);
        }
        const QImage oriented = SessionAppearance::materializeDisplay(
            soft, app, SessionAppearance::PixelKind::SoftPreview);
        if (!oriented.isNull()) {
            dwell = oriented;
        }
    }
    m_ssDwell.setSourceImage(dwell);
    // Keep pure-phase buffers in sync when progress is already active (start
    // path arms phase first; motion-only path must not leave m_ssFrom empty).
    if (m_ssHud.isProgressActive() && !path.isEmpty()) {
        if (m_ss.fromPathRef() != path || !m_ss.hasFromImage()) {
            m_ss.setFromPath(path);
            bindSlideshowPhaseSurface(&m_ss.fromSurfaceRef(), path);
            WorkspaceItemState app2;
            const bool applied =
                snapshotSlideshowContentAppearance(path, &app2)
                && SessionAppearance::hasContentAppearance(app2)
                && !dwell.isNull();
            m_ss.setFromImage(dwell, applied);
        }
    }
    // Align underlay camera to slideshow zoom before hiding it so cancel/stop
    // can restore a known static frame.
    applySlideshowZoomFraming(item);
    invalidateDwellAtlasRebuilds();
    requestDwellAtlasRebuild(); // async — do not scale on this stack
    if (!path.isEmpty()) {
        // Upgrade from unoriented host only (never re-materialize dwell).
        scheduleSlideshowPhaseBufferUpgrade(path, host);
    }
    setSlideshowUnderlayVisible(false);
    return true;
}

void ImageView::freezeScrollbarsForMotion()
{
    // Freeze scrollbars so the view cannot re-clamp/centre while the overlay
    // path is the only thing that should move (underlay is hidden).
    m_motionScroll.capture(horizontalScrollBarPolicy(), verticalScrollBarPolicy());
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setValue(0);
    }
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(0);
    }
}

void ImageView::resetItemPlacementForMotion(ImageItem *item)
{
    m_framing.setFitFillFlags(false, m_ssSettings.isZoomFill());
    item->setItemShear(0.0);
    item->setItemRotation(0.0);
    item->setItemScale(1.0);
    if (isImageMode()) {
        item->setPos(0, 0);
    }
}

void ImageView::armMotionBiasForPath(ImageItem *item, const QString &path)
{
    // Biases are per-image. Manual next/prev (and any LoadReplace) must not keep
    // the previous slide's A/B — that made the first post-nav transition glitch.
    // Live handoff installs to-path biases + path before calling here.
    if (m_ssDwell.hasBias() && m_ssDwell.biasPathRef() != path) {
        m_ssDwell.clearBias();
    }
    if (!m_ssDwell.hasBias()) {
        const QImage src = item ? item->sourceImage() : QImage();
        pickInterestingMotionBiases(qHash(path), src);
        m_ssDwell.setBiasPath(path);
    } else if (m_ssDwell.biasPathRef().isEmpty()) {
        m_ssDwell.setBiasPath(path);
    }
}

void ImageView::retargetSlideshowMotionDuration(int durationMs)
{
    // Interval change while Ken Burns is running: keep atlas, biases, and
    // normalized progress; only the path duration changes.
    if (!m_ssDwell.isMotionActive() || m_ssSettings.isMotionOff()) {
        return;
    }
    durationMs = SlideshowClocks::sanitizeDwellDurationMs(durationMs);
    qreal progress = 0.0;
    if (m_ssDwell.hasDuration()) {
        qint64 elapsed = m_ssDwell.elapsedOffsetMsValue();
        if (m_ssDwell.isClockValid() && !m_ssDwell.isMotionPaused()) {
            elapsed += m_ssDwell.clockElapsed();
        }
        progress = SlideshowClocks::progress01(elapsed, m_ssDwell.durationMsValue());
    }
    m_ssDwell.setDurationMs(SlideshowClocks::pathDurationMs(
        durationMs, m_ssSettings.transitionDuration()));
    m_ssDwell.setElapsedOffsetMs(qint64(progress * qreal(m_ssDwell.durationMsValue())));
    m_ssDwell.startClock();
    if (m_motionTimer && !m_ssDwell.isMotionPaused()) {
        m_motionTimer->start();
    }
}

void ImageView::startSlideshowMotion(int durationMs, qreal initialProgress)
{
    cancelSlideshowMotion();
    if (m_ssSettings.isMotionOff() || !isImageMode()
        || durationMs < 250 || !viewport()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }
    if (!prepareSlideshowMotionDwell(item)) {
        return;
    }
    freezeScrollbarsForMotion();
    resetItemPlacementForMotion(item);
    armMotionBiasForPath(item, item->path());
    // Biases + slideshow zoom are read by renderMotionCoverPixmap.

    ensureSlideshowMotionTimer();
    m_ssDwell.setMotionActive(true);
    // Path lasts longer than the dwell interval so that when the slideshow
    // advances (and crossfade runs), from-progress is still < 1 and keeps
    // lerping. Previously duration==interval → progress clamped at 1 for the
    // entire transition → motion looked frozen.
    m_ssDwell.setDurationMs(SlideshowClocks::pathDurationMs(
        durationMs, m_ssSettings.transitionDuration()));
    initialProgress = ViewTransform::clamp01(initialProgress); // [0,1]
    m_ssDwell.startClock();
    m_ssDwell.setMotionT(ViewTransform::clamp01(initialProgress));
    m_ssDwell.setElapsedOffsetMs((m_ssDwell.hasDuration())
        ? qint64(m_ssDwell.motionTValue() * qreal(m_ssDwell.durationMsValue()))
        : 0);
    m_motionTimer->start();
    if (viewport()) {
        viewport()->update();
    }
}



void ImageView::tickSlideshowPhaseMotionClocks()
{
    // Pure-phase path: advance A/B motion from their own clocks (spec: both
    // move during transition; B moves through transition + its interval).
    // Atlas / phase buffers are not rebuilt here — only motion progress.
    updateSlideshowPhaseMotionProgress(slideshowPathDurationMs());
}

void ImageView::tickSlideshowDwellMotionClock()
{
    SlideshowClocks::advanceDwellMotion(&m_ssDwell);
}

void ImageView::tickSlideshowMotion()
{
    if (!m_ssDwell.isMotionActive()) {
        if (m_motionTimer) {
            m_motionTimer->stop();
        }
        return;
    }
    if (!viewport()) {
        return;
    }

    if (m_ssHud.isProgressActive()
        && (m_ss.isFromMotionClockRunning() || m_ss.isToMotionClockRunning())) {
        tickSlideshowPhaseMotionClocks();
        viewport()->update();
        return;
    }

    tickSlideshowDwellMotionClock();
    viewport()->update();
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
        if (m_ssHud.isProgressActive() && m_ssSettings.isMotionOff()) {
            applySlideshowZoomFraming(item);
        } else if (!m_ssHud.isProgressActive()) {
            // Sticky Fill/1:1: reframe + restore pan (fitItem alone recentres).
            if (m_framing.isStickyZoomEnabled()) {
                applyImageModeFraming(item);
            } else {
                fitItem(item, currentFitAspectMode());
            }
        }
    } else if (beforeOk && before != after && !m_ssHud.isProgressActive()) {
        // Same aspect, larger/smaller logical size: scale the view so the image
        // keeps the same on-screen footprint (soft→native must not zoom).
        // Slideshow pure-phase paints via paintMotionCover (logical size) and
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
    // Critical: m_crop.active() stays true through applyCropCommit → fitItem, *after*
    // the crop bake is attached. Treating any m_crop.active() as draft forced
    // orient-only layout on top of crop pixels → stretch into the pre-crop
    // contentRect. Only pure draft (no applied crop, no session crop on the
    // item) is draft geometry.
    const bool cropDraft = CropSession::isDraftLayoutGeometry(
        m_crop.active(),
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

QString ImageView::sessionBadgeText() const
{
    const QString ascii = sessionBadgeAscii(m_sessionId.currentIndex(), m_sessionId.currentTotal());
    if (ascii.isEmpty()) {
        return {};
    }
    // Keep tr for potential locale digit shaping; form is still index/total.
    return tr("%1").arg(ascii);
}

QString ImageView::hudFileName() const
{
    if (!m_gallery.hoverPath().isEmpty()) {
        return PagePath::displayName(m_gallery.hoverPath());
    }
    // Empty Gallery/Workspace canvas: only the drop invite is shown — never a
    // stale classicPath, load error, or leftover session subject filename.
    if (m_items.isEmpty() && isMultiItemMode()) {
        return {};
    }
    if (m_sessionId.hasLastLoadError()) {
        return PagePath::displayName(m_sessionId.lastLoadErrorRef());
    }
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (item) {
        QString name = PagePath::displayName(item->path());
        if (targetHasContentAppearance()) {
            name += tr(" · modified");
        }
        return name;
    }
    if (hasClassicPath() && isImageMode()) {
        return PagePath::displayName(classicPath());
    }
    return {};
}

QString ImageView::pixelQualityLabel(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Prefer what is actually on screen over last thumtoo pipeline tag.
    // hasDecodedPixels alone is not "full": soft samples may have been installed
    // as FullSource by a mistaken PreferCache shortfall classification.
    const int edge = item->displayPixelLongEdge();
    const QSize logical = logicalSizeForPath(item->path());
    const int native = isPositiveSize(logical) ? ContentXform::longEdge(logical) : 0;
    using Tier = DisplayEdgePolicy::QualityTier;
    const Tier t = DisplayEdgePolicy::classifyQualityTier(
        edge, native, item->hasDecodedPixels(),
        ThumtooCache::kBatchOverviewEdge, ThumtooCache::kGalleryLadderEdge,
        ThumtooCache::kFilmstripLadderEdge, DisplayQuality::kLqipMaxEdge);
    QString tier;
    switch (t) {
    case Tier::Loading:
        return tr("Loading…");
    case Tier::FullResolution:
        return tr("Full resolution");
    case Tier::HighQuality:
        tier = tr("High quality");
        break;
    case Tier::Preview:
        tier = tr("Preview");
        break;
    case Tier::Thumbnail:
        tier = tr("Thumbnail");
        break;
    case Tier::Placeholder:
        // Internal name is LQIP — do not show that acronym to users.
        tier = tr("Placeholder");
        break;
    case Tier::QuickPreview:
    default:
        tier = tr("Quick preview");
        break;
    }
    if (isGalleryMode()) {
        const int need = galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
        const GallerySoftState *st = m_gallerySoftBook.get(item->path());
        const int have = st
            ? GallerySoft::maxHave(st->have, edge)
            : edge;
        if (need > 0 && have > 0) {
            return tr("%1 · show %2px · need %3px · have %4px")
                .arg(tier)
                .arg(edge)
                .arg(need)
                .arg(have);
        }
    } else if (isImageMode() && edge > 0) {
        // Image mode: show sample vs native when not yet full coverage.
        if (native > 0 && !DisplayEdgePolicy::coversEdge(edge, native)) {
            return tr("%1 · show %2px · native %3px")
                .arg(tier)
                .arg(edge)
                .arg(native);
        }
    }
    return tier;
}

void ImageView::appendThumtooDebugStatus(QString *text, ImageItem *item) const
{
    if (!text || !item) {
        return;
    }
    // "via file decode" / ladder provenance is pipeline debug, not live status.
    // Showing it next to a stuck "Improving quality…" looked like an active decode.
    const char *dbg = std::getenv("THUMTOO_DEBUG");
    if (!dbg || !dbg[0] || dbg[0] == '0') {
        return;
    }
    const QString src = ThumtooCache::lastPixelSourceLabel(item->path());
    if (!src.isEmpty()) {
        *text += tr(" · via %1").arg(src);
    }
    const QString q = ThumtooCache::queueStatsLabel();
    if (!q.isEmpty()) {
        *text += tr(" · %1").arg(q);
    }
}

QString ImageView::statusTextEmpty() const
{
    if (m_sessionId.hasLastLoadError()) {
        return tr("Failed to load “%1”").arg(PagePath::displayName(m_sessionId.lastLoadErrorRef()));
    }
    if (hasClassicPath() && isImageMode()) {
        return tr("Loading…");
    }
    if (isGalleryMode()) {
        return tr("Gallery — no images");
    }
    if (isWorkspaceMode()) {
        return tr("Workspace — drop images or use Open");
    }
    return tr("Ready");
}

QString ImageView::statusTextMultiItem(ImageItem *item, const QString &quality,
                                       int edge, const QSize &native) const
{
    const QString modeLabel = isGalleryMode() ? tr("Gallery") : tr("Workspace");
    QString text = tr("%1 · %2 images · Zoom %3%")
                       .arg(modeLabel)
                       .arg(m_items.size())
                       .arg(qRound(viewScale() * 100));
    if (!quality.isEmpty()) {
        if (edge > 0) {
            text += tr(" · %1 (%2px)").arg(quality).arg(edge);
        } else {
            text += tr(" · %1").arg(quality);
        }
    }
    if (native.width() > 1 && native.height() > 1
        && native != QSize(1000, 1000) && native != QSize(1024, 1024)) {
        text += tr(" · %1×%2").arg(native.width()).arg(native.height());
    }
    if (isGalleryMode()) {
        int blank = 0, lqip = 0, soft = 0, better = 0, climb = 0;
        for (ImageItem *ii : m_items) {
            if (!ii || ii->path().isEmpty()) {
                continue;
            }
            const int e = ii->displayPixelLongEdge();
            if (!ii->hasDisplayPixels() || e <= 0) {
                ++blank;
            } else if (e <= DisplayQuality::kLqipMaxEdge) {
                ++lqip;
            } else if (e <= DisplayQuality::kSoftMaxEdge) {
                ++soft;
            } else {
                ++better;
            }
            if (const GallerySoftState *sit = m_gallerySoftBook.get(ii->path())) {
                if (sit->inflight > 0) {
                    ++climb;
                }
            }
        }
        // Pipeline mix is debug-only — never put "LQIP" in the status bar.
        const char *dbg = std::getenv("THUMTOO_DEBUG");
        if (dbg && dbg[0] && dbg[0] != '0') {
            text += tr(" · %1 blank · %2 lqip · %3 soft · %4 higher")
                        .arg(blank)
                        .arg(lqip)
                        .arg(soft)
                        .arg(better);
            if (climb > 0) {
                text += tr(" · climbing %1").arg(climb);
            }
        }
        const int pending = pendingDecodeCount();
        if (pending > 0) {
            text += tr(" · Loading %1…").arg(pending);
        }
    } else {
        const int pending = pendingDecodeCount();
        if (pending > 0) {
            text += tr(" · Loading %1…").arg(pending);
        }
    }
    {
        const QString load = ThumtooCache::loadingBreakdownLabel();
        if (!load.isEmpty()) {
            text += tr(" · %1").arg(load);
        }
    }
    if (isWorkspaceMode() && item->isSelected()) {
        if (qAbs(item->itemScaleX() - item->itemScaleY()) < 0.005) {
            text += tr(" · Item %1% · Rot %2°")
                        .arg(qRound(item->itemScaleX() * 100))
                        .arg(qRound(item->itemRotation()));
        } else {
            text += tr(" · Item %1%×%2% · Rot %3°")
                        .arg(qRound(item->itemScaleX() * 100))
                        .arg(qRound(item->itemScaleY() * 100))
                        .arg(qRound(item->itemRotation()));
        }
    }
    if (targetHasContentAppearance()) {
        text += tr(" · Edited");
    }
    appendThumtooDebugStatus(&text, item);
    return text;
}

QString ImageView::imageModeClimbActivityLabel(const ImageItem *item) const
{
    // User-visible activity while samples climb toward *on-screen need*.
    // Do not require native coverage — after progressive Soft→Prefer settle at
    // window size, decoder is idle; claiming "Improving…" was a stuck HUD lie.
    if (!item || item->path().isEmpty()) {
        return {};
    }
    const QString path = item->path();
    const int have = item->displayPixelLongEdge();
    if (have <= 0) {
        return tr("Loading…");
    }
    const int need = imageModeOnScreenNeedEdge();
    // Strict cover (DisplayEdgePolicy::coversEdge): have >= need.
    if (need > 0 && have >= need) {
        return {};
    }
    if (sampleCoversNativeLogical(path, item->displayImage())) {
        return {};
    }
    if (m_pathRaster && m_pathRaster->isClimbPending(path)) {
        return m_pathRaster->isGaveUp(path) ? tr("Decoding full…")
                                            : tr("Improving quality…");
    }
    if (ThumtooCache::isAvailable()) {
        const int want = cappedDisplayEdgeForPath(path, need);
        if (ThumtooCache::isPixelsPending(path, want)
            || ThumtooCache::isPixelsPending(path, ThumtooCache::kGalleryLadderEdge)
            || ThumtooCache::isPixelsPending(path, ThumtooCache::kBatchOverviewEdge)) {
            return tr("Improving quality…");
        }
    }
    // No climb / decode pending: stay quiet even if below native.
    return {};
}

QString ImageView::statusTextImageMode(ImageItem *item, const QString &quality,
                                       int edge, const QSize &native) const
{
    QString text = tr("%1×%2 · Zoom %3%")
                       .arg(native.width())
                       .arg(native.height())
                       .arg(qRound(viewScale() * 100));
    if (!quality.isEmpty()) {
        // quality may already include "show Npx · native Mpx" — avoid double edge.
        if (edge > 0 && !item->hasDecodedPixels() && !quality.contains(QLatin1String("px"))) {
            text += tr(" · %1 (%2px)").arg(quality).arg(edge);
        } else {
            text += tr(" · %1").arg(quality);
        }
    }
    const QString climb = imageModeClimbActivityLabel(item);
    if (!climb.isEmpty()) {
        text += tr(" · %1").arg(climb);
    }
    if (qAbs(item->itemRotation()) > 0.5) {
        text += tr(" · Rot %1°").arg(qRound(item->itemRotation()));
    }
    if (item->itemHFlip() || item->itemVFlip()) {
        QStringList flips;
        if (item->itemHFlip()) {
            flips << tr("H");
        }
        if (item->itemVFlip()) {
            flips << tr("V");
        }
        text += tr(" · Flip %1").arg(flips.join(QLatin1Char('+')));
    }
    if (targetHasContentAppearance()) {
        text += tr(" · Edited");
    }
    appendThumtooDebugStatus(&text, item);
    return text;
}

QString ImageView::statusText() const
{
    if (m_zoomRegion.isActive()) {
        return tr("Zoom region: drag a rectangle · Esc cancels");
    }
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (!item) {
        return statusTextEmpty();
    }

    const QString quality = pixelQualityLabel(item);
    const int edge = item->displayPixelLongEdge();
    const QSize native = item->imageSize();

    if (isMultiItemMode()) {
        return statusTextMultiItem(item, quality, edge, native);
    }
    return statusTextImageMode(item, quality, edge, native);
}

