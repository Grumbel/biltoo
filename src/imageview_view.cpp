// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "displayquality.h"
#include "biltoo_thread.h"

#include "archivepath.h"
#include "biltoo_logging.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "imageitem.h"
#include "imageloader.h"
#include "pagepath.h"
#include "thumtoocache.h"

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

namespace {

/** Fraction of target edge treated as "good enough" for slideshow samples. */
constexpr int kSsAdequacyNumer = 7;
constexpr int kSsAdequacyDenom = 10;
constexpr int kSsMaxInflight = 4;
constexpr int kSsMaxPending = 4;

int slideshowNeedEdge(int targetEdge)
{
    return targetEdge * kSsAdequacyNumer / kSsAdequacyDenom;
}

/** Stable ZoomBlur slot key for path + viewport size. */
qint64 slideshowZoomBlurKey(const QString &path, int vw, int vh)
{
    if (path.isEmpty() || vw < 1 || vh < 1) {
        return 0;
    }
    return qint64(qHash(path)) ^ (qint64(vw) << 16) ^ qint64(vh);
}

/**
 * Advance unitless motion progress with wall Δt / pathMs.
 * Clock is a rate sample only (restart each call); T ∈ [0,1] is authority.
 */
void integrateMotionProgress01(qreal *t, QElapsedTimer *clock, bool running,
                               bool paused, int pathMs)
{
    if (!t || !clock || !running || paused || pathMs <= 0) {
        return;
    }
    if (!clock->isValid()) {
        clock->start();
        return;
    }
    const qint64 d = clock->restart();
    if (d > 0) {
        *t = qBound(0.0, *t + qreal(d) / qreal(pathMs), 1.0);
    }
}

} // namespace


void ImageView::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    if (m_tool == Tool::Pan) {
        setCursor(Qt::OpenHandCursor);
    } else if (m_tool == Tool::Zoom) {
        setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
    // Workspace Select: rubber-band multi-select on empty drag (same as Gallery).
    // Pan / Zoom keep NoDrag (view gestures are handled in mouse handlers).
    if (isWorkspaceMode()) {
        setDragMode(m_tool == Tool::Select ? QGraphicsView::RubberBandDrag
                                           : QGraphicsView::NoDrag);
    }
    emit toolChanged(m_tool);
}

void ImageView::setImageModeNavigationEnabled(bool on)
{
    if (m_imageModeNavEnabled == on) {
        return;
    }
    m_imageModeNavEnabled = on;
    if (!on && m_hoverEdge != EdgeZone::GalleryReturn) {
        m_hoverEdge = EdgeZone::None;
    }
    viewport()->update();
}

void ImageView::setGalleryReturnAvailable(bool on)
{
    if (m_galleryReturnAvailable == on) {
        return;
    }
    m_galleryReturnAvailable = on;
    if (!on && m_hoverEdge == EdgeZone::GalleryReturn) {
        m_hoverEdge = EdgeZone::None;
    }
    viewport()->update();
}

void ImageView::setBackgroundColor(const QColor &color)
{
    if (!color.isValid() || color == m_bgColor) {
        return;
    }
    m_bgColor = color;
    setBackgroundBrush(QBrush(m_bgColor));
    if (viewport()) {
        viewport()->update();
    }
}

QColor ImageView::slideshowPadColor() const
{
    if (m_slideshowLetterboxFill == SlideshowLetterboxFill::Solid
        && m_slideshowPadColor.isValid()) {
        return m_slideshowPadColor;
    }
    if (m_bgColor.isValid()) {
        return m_bgColor;
    }
    const QBrush b = backgroundBrush();
    if (b.style() != Qt::NoBrush && b.color().isValid()) {
        return b.color();
    }
    return m_slideshowPadColor.isValid() ? m_slideshowPadColor : QColor(42, 42, 42);
}

void ImageView::setSlideshowPadColor(const QColor &color)
{
    if (!color.isValid() || color == m_slideshowPadColor) {
        return;
    }
    m_slideshowPadColor = color;
    clearSlideshowZoomBlurSlots();
    if (m_slideshowProgressActive && viewport()) {
        viewport()->update();
    }
}

void ImageView::setSlideshowLetterboxFill(SlideshowLetterboxFill mode)
{
    if (m_slideshowLetterboxFill == mode) {
        return;
    }
    m_slideshowLetterboxFill = mode;
    clearSlideshowZoomBlurSlots();
    if (m_slideshowProgressActive && viewport()) {
        viewport()->update();
    }
}

void ImageView::setBackgroundColorAlt(const QColor &color)
{
    if (!color.isValid() || color == m_bgColorAlt) {
        return;
    }
    m_bgColorAlt = color;
    viewport()->update();
}

void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    if (m_bgPattern == pattern) {
        return;
    }
    m_bgPattern = pattern;
    viewport()->update();
}

void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    if (m_bgCheckerWorkspaceOnly == on) {
        return;
    }
    m_bgCheckerWorkspaceOnly = on;
    viewport()->update();
}

void ImageView::setWorkspaceBackground(const WorkspaceBackground &bg)
{
    if (m_workspaceBackground.mode == bg.mode
        && m_workspaceBackground.color == bg.color
        && m_workspaceBackground.colorAlt == bg.colorAlt
        && m_workspaceBackground.imagePath == bg.imagePath
        && m_workspaceBackground.imagePathRelative == bg.imagePathRelative) {
        // No-op: leave a temporary "show default" preview alone so the
        // toolbar toggle does not desync from paint.
        return;
    }
    // Permanent override changed — drop temporary preview.
    m_workspaceBackgroundShowDefault = false;
    m_workspaceBackground = bg;
    if (bg.mode != WorkspaceBackgroundMode::ImageTile
        || bg.imagePath != m_workspaceBgTilePath) {
        m_workspaceBgTile = QPixmap();
        m_workspaceBgTilePath.clear();
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (m_workspaceBgTilePath != bg.imagePath) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_workspaceBgTile = px;
                m_workspaceBgTilePath = bg.imagePath;
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
    if (m_workspaceBackgroundShowDefault == on) {
        return;
    }
    m_workspaceBackgroundShowDefault = on;
    if (viewport()) {
        viewport()->update();
    }
}

qreal ImageView::viewScale() const
{
    const QTransform t = transform();
    return std::hypot(t.m11(), t.m12());
}

void ImageView::refreshStatus()
{
    // Coalesce rapid soft-climb / provenance updates so the HUD and status
    // bar are not rewritten every frame.
    if (!m_statusRefreshTimer) {
        m_statusRefreshTimer = new QTimer(this);
        m_statusRefreshTimer->setSingleShot(true);
        m_statusRefreshTimer->setInterval(120);
        connect(m_statusRefreshTimer, &QTimer::timeout, this, [this]() {
            emit statusChanged();
            if ((m_hudVisible || m_hudFlashVisible || m_slideshowPausedHud)
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
    m_fitMode = false;
    m_fillMode = false;
    // Keep the viewport centre stable when zooming via toolbar/shortcuts
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Viewport-space chrome only — no selected-item prepareGeometryChange.
    if (viewport()) {
        viewport()->update();
    }
    // Zoom changes on-screen cell size → may need a higher ladder step.
    // Match Ctrl+wheel: debounce so rapid toolbar/shortcut zoom does not
    // rescan all tiles + setInterest on every notch (GUI_THREAD_AUDIT G5).
    if (isGalleryMode()) {
        scheduleGalleryDecodeWindowRefresh(120);
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
    m_fitMode = false;
    m_fillMode = false;
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
    m_fitMode = false;
    m_fillMode = false;
    if (isMultiItemMode()) {
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
    m_fitMode = true;
    m_fillMode = false;
    if (isGalleryMode()) {
        // Fit the packed gallery into the viewport. applyLayout() alone only
        // resets to identity after a view-zoom when the pack already matches
        // the window — use fitInView so +/- zoom is actually undone to "all
        // tiles visible".
        if (!m_items.isEmpty()) {
            const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-16, -16, 16, 16);
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
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
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
    m_fitMode = true;
    m_fillMode = true;
    if (isGalleryMode()) {
        if (!m_items.isEmpty()) {
            const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-16, -16, 16, 16);
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
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
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
    m_zoomRegionArmed = true;
    setCursor(Qt::CrossCursor);
    emit statusChanged();
    viewport()->update();
}


void ImageView::setStickyZoomEnabled(bool on)
{
    if (m_stickyZoomEnabled == on) {
        return;
    }
    m_stickyZoomEnabled = on;
    emit stickyZoomChanged();
    emit statusChanged();
}

void ImageView::releaseStickyZoom()
{
    if (!m_stickyZoomEnabled) {
        return;
    }
    m_stickyZoomEnabled = false;
    emit stickyZoomChanged();
    emit statusChanged();
}

void ImageView::captureStickyZoomFromCurrentFraming()
{
    if (!m_fitMode && !m_fillMode) {
        m_stickyZoomKind = StickyZoomKind::Actual;
    } else if (m_fillMode) {
        m_stickyZoomKind = StickyZoomKind::Fill;
    } else {
        m_stickyZoomKind = StickyZoomKind::Fit;
    }
}

void ImageView::setStickyZoomKind(StickyZoomKind kind)
{
    m_stickyZoomKind = kind;
}

void ImageView::captureStickyPanAnchor(ImageItem *item)
{
    // Always sample scale + pan when leaving an image so free navigation
    // (no sticky Fit/Fill/1:1) can keep the same zoom and relative position.
    m_haveStickyPanAnchor = false;
    m_havePreservedViewScale = false;
    if (!item || !viewport() || !m_scene) {
        return;
    }
    if (!m_items.contains(item) || item->scene() != m_scene) {
        return;
    }
    const qreal sx = transform().m11();
    if (qIsFinite(sx) && sx > 1e-6) {
        m_preservedViewScale = sx;
        m_havePreservedViewScale = true;
    }
    const QRectF r = item->sceneBoundingRect();
    if (r.width() < 1.0 || r.height() < 1.0) {
        return;
    }
    const QPointF vc = mapToScene(viewport()->rect().center());
    m_stickyPanNormX = (vc.x() - r.left()) / r.width();
    m_stickyPanNormY = (vc.y() - r.top()) / r.height();
    m_stickyPanNormX = qBound(0.0, m_stickyPanNormX, 1.0);
    m_stickyPanNormY = qBound(0.0, m_stickyPanNormY, 1.0);
    m_haveStickyPanAnchor = true;
}

void ImageView::restoreStickyPanAnchor(ImageItem *item)
{
    if (!m_haveStickyPanAnchor || !item || !m_scene || !viewport()) {
        return;
    }
    if (!m_items.contains(item) || item->scene() != m_scene) {
        return;
    }
    const QRectF r = item->sceneBoundingRect();
    if (r.width() < 1.0 || r.height() < 1.0) {
        return;
    }
    const QPointF target(r.left() + m_stickyPanNormX * r.width(),
                         r.top() + m_stickyPanNormY * r.height());
    centerOn(target);
}

void ImageView::applyImageModeFraming(ImageItem *item)
{
    if (!item || !isImageMode()) {
        return;
    }
    if (m_stickyZoomEnabled) {
        // Fit: unique home pose (centred). Fill / 1:1: frame, then best-effort
        // restore viewport centre in image-normalized coords (prev/next compare).
        // Always restore *after* setSceneRect/refreshScrollBarGeometry — those
        // often reset QAbstractScrollArea scroll position.
        switch (m_stickyZoomKind) {
        case StickyZoomKind::Fill:
            m_fitMode = true;
            m_fillMode = true;
            fitItem(item, Qt::KeepAspectRatioByExpanding);
            break;
        case StickyZoomKind::Actual:
            m_fitMode = false;
            m_fillMode = false;
            item->setItemScale(1.0);
            resetTransform();
            centerOn(item);
            break;
        case StickyZoomKind::Fit:
        default:
            m_fitMode = true;
            m_fillMode = false;
            fitItem(item, Qt::KeepAspectRatio);
            break;
        }
        if (m_scene) {
            m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
        }
        refreshScrollBarGeometry();
        if (m_stickyZoomKind != StickyZoomKind::Fit) {
            restoreStickyPanAnchor(item);
            // Scroll ranges often settle after this returns — restore again.
            // QPointer so a destroy mid-navigation cancels the callback safely.
            const QPointer<ImageView> guard(this);
            QTimer::singleShot(0, this, [guard]() {
                ImageView *const view = guard.data();
                if (!view || !view->m_stickyZoomEnabled
                    || view->m_stickyZoomKind == StickyZoomKind::Fit
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
    if (m_havePreservedViewScale) {
        m_fitMode = false;
        m_fillMode = false;
        item->setItemScale(1.0);
        resetTransform();
        scale(m_preservedViewScale, m_preservedViewScale);
        if (m_scene) {
            m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
        }
        refreshScrollBarGeometry();
        restoreStickyPanAnchor(item);
        const QPointer<ImageView> guard(this);
        QTimer::singleShot(0, this, [guard]() {
            ImageView *const view = guard.data();
            if (!view || view->m_stickyZoomEnabled || !view->m_scene
                || !view->viewport()) {
                return;
            }
            if (ImageItem *cur = view->targetItem()) {
                view->restoreStickyPanAnchor(cur);
            }
        });
        return;
    }
    m_fitMode = true;
    m_fillMode = false;
    fitItem(item, Qt::KeepAspectRatio);
}

void ImageView::cancelZoomRegion()
{
    m_zoomRegionArmed = false;
    m_zoomRegionDragging = false;
    if (m_zoomRubberBand) {
        m_zoomRubberBand->hide();
    }
    if (!m_panning && !m_rotating) {
        if (m_tool == Tool::Pan) {
            setCursor(Qt::OpenHandCursor);
        } else if (m_tool == Tool::Zoom) {
            setCursor(Qt::CrossCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
    emit statusChanged();
}

void ImageView::setImageModeLeftDragPan(bool on)
{
    m_imageModeLeftDragPan = on;
}

void ImageView::setSessionPosition(int index, int total, bool pulseIdentity)
{
    const bool changed = (m_sessionIndex != index || m_sessionTotal != total);
    m_sessionIndex = index;
    m_sessionTotal = total;
    // Pulse only when the session cursor actually moves (user Next/Prev, etc.).
    // Do not pulse on every statusChanged while total > 0 (AUDIT H7).
    // Slideshow auto-advance passes pulseIdentity=false.
    if (pulseIdentity && changed) {
        m_hudIdentityPulse = true;
        if (m_hudFlashTimer) {
            m_hudFlashTimer->start(1000);
        }
    }
    if (!(changed || m_hudVisible || m_hudFlashVisible || m_hudIdentityPulse
          || m_slideshowPausedHud)) {
        return;
    }
    // Gallery selection already invalidates the tile; a full viewport()->update()
    // here forced every image through the GL path and felt like lag on click.
    if (isGalleryMode() && !m_hudVisible && !m_hudIdentityPulse) {
        return;
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::setHudVisible(bool on)
{
    if (m_hudVisible == on) {
        return;
    }
    m_hudVisible = on;
    // Progress line only paints with the pinned HUD; drive the timer accordingly.
    if (m_slideshowProgressTimer) {
        if (on && m_slideshowProgressActive && m_slideshowProgressIntervalMs > 0) {
            m_slideshowProgressTimer->start();
        } else {
            m_slideshowProgressTimer->stop();
        }
    }
    viewport()->update();
}

void ImageView::setHudFontPointSize(int pt)
{
    pt = qBound(8, pt, 48);
    if (m_hudFontPointSize == pt) {
        return;
    }
    m_hudFontPointSize = pt;
    viewport()->update();
}

void ImageView::setHudTextColor(const QColor &color)
{
    if (!color.isValid() || color == m_hudTextColor) {
        return;
    }
    m_hudTextColor = color;
    viewport()->update();
}

void ImageView::setHudPanelColor(const QColor &color)
{
    if (!color.isValid() || color == m_hudPanelColor) {
        return;
    }
    m_hudPanelColor = color;
    viewport()->update();
}

void ImageView::flashHud(const QString &action, const QString &detail)
{
    m_hudAction = action;
    m_hudDetail = detail;
    m_hudFlashVisible = true;
    m_hudIdentityPulse = true;
    if (m_hudFlashTimer) {
        m_hudFlashTimer->start(1000);
    }
    viewport()->update();
}

void ImageView::setSlideshowProgress(bool active, int intervalMs)
{
    // Speed / interval edit while the show is already running: only update the
    // interval. Do not restart the progress clock or clear phase buffers —
    // that produced HUD and paint blips on every [/] or settings change.
    if (active && m_slideshowProgressActive) {
        m_slideshowProgressIntervalMs = qMax(0, intervalMs);
        if (m_slideshowProgressIntervalMs > 0 && m_slideshowProgressTimer
            && !m_slideshowProgressClockPaused) {
            m_slideshowProgressTimer->start();
        } else if (m_slideshowProgressTimer && m_slideshowProgressIntervalMs <= 0) {
            m_slideshowProgressTimer->stop();
        }
        if (viewport()) {
            viewport()->update();
        }
        return;
    }

    m_slideshowProgressActive = active;
    m_slideshowProgressIntervalMs = active ? qMax(0, intervalMs) : 0;
    if (active) {
        m_slideshowProgressBaseMs = 0;
        m_slideshowProgressClockPaused = false;
        m_slideshowProgressElapsed.start();
        if (m_slideshowProgressIntervalMs > 0 && m_slideshowProgressTimer) {
            m_slideshowProgressTimer->start();
        } else if (m_slideshowProgressTimer) {
            m_slideshowProgressTimer->stop();
        }
    } else if (m_slideshowProgressTimer) {
        m_slideshowProgressTimer->stop();
        m_slideshowProgressBaseMs = 0;
        m_slideshowProgressClockPaused = false;
    }
    if (!active) {
        cancelSlideshowMotion();
        m_motionBiasValid = false;
        m_motionBiasPath.clear();
        m_motionTravelDir = QPointF(0.0, 1.0);
        m_motionSign = 1.0;
        m_slideshowTimelineElapsedMs = 0;
        m_slideshowTimelineTotalMs = 0;
        m_lastSlideshowPaintFp.clear();
        m_ssFromPath.clear();
        m_ssToPath.clear();
        m_ssFromImage = QImage();
        m_ssToImage = QImage();
        m_ssFadeT = -1.0;
        m_ssFromMotionT = 0.0;
        m_ssToMotionT = 0.0;
        m_ssFromMotionClockRunning = false;
        m_ssToMotionClockRunning = false;
        // Rasters live in ImageCache — do not clear the host map on stop.
        m_ssRasterInflight.clear();
        m_ssRasterPending.clear();
    }
    viewport()->update();
}

void ImageView::setSlideshowProgressPaused(bool paused)
{
    if (paused == m_slideshowProgressClockPaused) {
        return;
    }
    if (paused) {
        if (m_slideshowProgressElapsed.isValid()) {
            m_slideshowProgressBaseMs += m_slideshowProgressElapsed.elapsed();
        }
        m_slideshowProgressClockPaused = true;
        if (m_slideshowProgressTimer) {
            m_slideshowProgressTimer->stop();
        }
    } else {
        m_slideshowProgressClockPaused = false;
        m_slideshowProgressElapsed.start();
        if (m_slideshowProgressActive && m_slideshowProgressIntervalMs > 0
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
        if (m_slideshowTimelineTotalMs == 0) {
            return;
        }
        m_slideshowTimelineElapsedMs = 0;
        m_slideshowTimelineTotalMs = 0;
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    elapsedMs = qBound(qint64(0), elapsedMs, totalMs);
    if (elapsedMs == m_slideshowTimelineElapsedMs
        && totalMs == m_slideshowTimelineTotalMs) {
        return;
    }
    m_slideshowTimelineElapsedMs = elapsedMs;
    m_slideshowTimelineTotalMs = totalMs;
    // Progress bar needs sub-second updates while the extended HUD is pinned.
    if (m_hudVisible && viewport()) {
        viewport()->update();
    }
}


void ImageView::setSlideshowCycleProgress(qreal phase01)
{
    m_slideshowCycleProgress01 = qBound(0.0, phase01, 1.0);
    m_slideshowCycleProgressValid = true;
}


void ImageView::setSlideshowTransition(SlideshowTransition kind)
{
    if (m_slideshowTransition == kind) {
        return;
    }
    m_slideshowTransition = kind;
    if (kind == SlideshowTransition::None) {
        cancelSlideshowTransition();
    }
}

void ImageView::setSlideshowTransitionDurationMs(int ms)
{
    m_slideshowTransitionDurationMs = qMax(0, ms);
}

QPixmap ImageView::captureSlideshowFrame() const
{
    // QOpenGLWidget::grab() often returns a blank/white pixmap. Paint the
    // current slide into an offscreen pixmap instead (software, reliable).
    if (!viewport()) {
        return {};
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    const qreal dpr = viewport()->devicePixelRatioF();
    QPixmap pm(QSize(vw, vh) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(slideshowPadColor());
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_slideshowMotionActive && !m_dwellSourceImage.isNull()) {
        paintMotionCover(&painter, m_dwellSourceImage, m_dwellMotionT,
                         m_motionBiasA, m_motionBiasB, m_ssFromPath);
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
    if (m_slideshowMotion == mode) {
        return;
    }
    m_slideshowMotion = mode;
    if (mode == SlideshowMotion::Off) {
        cancelSlideshowMotion();
        if (m_slideshowProgressActive) {
            reapplySlideshowFraming();
        }
    } else if (m_slideshowProgressActive) {
        reapplySlideshowFraming();
    }
}

void ImageView::setPanZoomFactor(qreal factor)
{
    m_panZoomFactor = qBound(1.02, factor, 1.5);
}

void ImageView::setSlideshowZoom(SlideshowZoom mode)
{
    if (m_slideshowZoom == mode) {
        return;
    }
    m_slideshowZoom = mode;
    // Zoom is the base scale for Ken Burns as well as static framing.
    if (m_slideshowProgressActive) {
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
        // Align underlay intrinsic with logical size so contentRect matches
        // Ken Burns / overlay geometry when motion is off.
        item->setIntrinsicSize(logical);
    } else if (!item->displayImage().isNull()) {
        // Provisional: frame from sample aspect so underlay is not square-squashed.
        logical = scaleToLongEdge(item->displayImage().size(), kProvisionalLayoutLongEdge);
        if (isPositiveSize(logical)) {
            item->setIntrinsicSize(logical);
        }
    }
    const QRectF content = item->contentRect();
    if (content.width() < 1.0 || content.height() < 1.0) {
        return;
    }
    // Scale from logical size when known; contentRect only for scene mid-point.
    const qreal vw = qreal(qMax(1, viewport()->width()));
    const qreal vh = qreal(qMax(1, viewport()->height()));
    const QPointF mid = item->mapToScene(content.center());

    if (!isPositiveSize(logical)) {
        logical = QSize(int(content.width()), int(content.height()));
    }
    qreal scale = slideshowZoomBaseScale(logical, int(vw), int(vh));
    switch (m_slideshowZoom) {
    case SlideshowZoom::Fill:
        m_fitMode = false;
        m_fillMode = true;
        break;
    case SlideshowZoom::Actual:
        m_fitMode = false;
        m_fillMode = false;
        break;
    case SlideshowZoom::Fit:
    default:
        m_fitMode = true;
        m_fillMode = false;
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
    if (!m_slideshowProgressActive || !isImageMode()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }
    if (m_slideshowMotion != SlideshowMotion::Off) {
        int duration = m_slideshowProgressIntervalMs;
        if (duration < 250) {
            duration = 3000;
        }
        // Already in motion (typical interval edit): retarget duration and keep
        // normalized progress + dwell atlas. startSlideshowMotion cancels first
        // and clears the atlas — that is the speed-change flash.
        if (m_slideshowMotionActive) {
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
    if (paused == m_slideshowMotionPaused) {
        return;
    }
    if (paused) {
        if (m_slideshowMotionActive && m_motionTimer && m_motionTimer->isActive()) {
            // Fold wall into unitless dwell progress, then freeze.
            if (m_motionDurationMs > 0 && m_motionClock.isValid()) {
                const qint64 d = m_motionClock.elapsed();
                if (d > 0) {
                    const qreal t = qreal(m_motionElapsedOffsetMs + d)
                        / qreal(m_motionDurationMs);
                    m_dwellMotionT = qBound(0.0, t, 1.0);
                    m_motionElapsedOffsetMs =
                        qint64(m_dwellMotionT * qreal(m_motionDurationMs));
                }
            }
            m_motionTimer->stop();
        }
        // Fold phase-motion clocks into T ∈ [0,1].
        const int pathMs = slideshowPathDurationMs();
        if (pathMs > 0) {
            integrateMotionProgress01(&m_ssFromMotionT, &m_ssFromMotionClock,
                                      m_ssFromMotionClockRunning, false, pathMs);
            integrateMotionProgress01(&m_ssToMotionT, &m_ssToMotionClock,
                                      m_ssToMotionClockRunning, false, pathMs);
            if (m_ssFromMotionClockRunning) {
                m_dwellMotionT = m_ssFromMotionT;
            }
        }
        m_slideshowMotionPaused = true;
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    m_slideshowMotionPaused = false;
    if (m_ssFromMotionClockRunning) {
        m_ssFromMotionClock.start();
    }
    if (m_ssToMotionClockRunning) {
        m_ssToMotionClock.start();
    }
    if (m_slideshowMotionActive && m_motionTimer && m_motionDurationMs > 0) {
        m_motionClock.restart();
        m_motionTimer->start();
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setSlideshowPausedHud(bool on)
{
    if (on == m_slideshowPausedHud) {
        return;
    }
    m_slideshowPausedHud = on;
    if (on) {
        // Keep a stable action line for the permanent cue; flash timer must
        // not clear it (paint draws paused HUD independently of flash).
        m_hudAction = tr("❚❚  Paused");
        m_hudDetail.clear();
        m_hudFlashVisible = false;
        if (m_hudFlashTimer) {
            m_hudFlashTimer->stop();
        }
    } else if (m_hudAction.contains(QStringLiteral("Paused"))) {
        m_hudAction.clear();
        m_hudDetail.clear();
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::cancelSlideshowMotion()
{
    const bool wasMotion = m_slideshowMotionActive;
    m_slideshowMotionActive = false;
    m_slideshowMotionPaused = false;
    if (m_motionSavedBarPolicies) {
        setHorizontalScrollBarPolicy(m_motionSavedHBarPolicy);
        setVerticalScrollBarPolicy(m_motionSavedVBarPolicy);
        m_motionSavedBarPolicies = false;
    }
    setSlideshowUnderlayVisible(true);
    m_dwellAtlas = QPixmap();
    m_dwellAtlasScale = 0.0;
    m_motionElapsedOffsetMs = 0;
    if (m_motionTimer) {
        m_motionTimer->stop();
    }
    // Ken Burns only moved the overlay blit. Bring the underlay back in line
    // with static slideshow framing while the show is still running.
    if (wasMotion && m_slideshowProgressActive && isImageMode()) {
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
    m_fitMode = true;
    m_fillMode = false;
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
    // Map normalized focus to bias space [-1, 1] (same as corner table).
    QPointF subject((att01.x() - 0.5) * 2.0, (att01.y() - 0.5) * 2.0);
    subject.setX(qBound(-1.0, subject.x(), 1.0));
    subject.setY(qBound(-1.0, subject.y(), 1.0));
    // Near-centre attention still needs travel — fall through to geometry.
    if (qAbs(subject.x()) <= 0.12 && qAbs(subject.y()) <= 0.12) {
        return false;
    }
    // Subject must sit mid-path: start/end of the dwell are largely
    // hidden by the transition, so endpoint focus is invisible.
    // Travel along the subject↔opposite axis, centred on the subject.
    const QPointF travel = QPointF(subject.x() * 0.55, subject.y() * 0.55);
    // Seed picks which way the path runs (toward / away from opposite).
    if (seed & 1u) {
        m_motionBiasA = subject - travel;
        m_motionBiasB = subject + travel;
    } else {
        m_motionBiasA = subject + travel;
        m_motionBiasB = subject - travel;
    }
    m_motionBiasA.setX(qBound(-1.0, m_motionBiasA.x(), 1.0));
    m_motionBiasA.setY(qBound(-1.0, m_motionBiasA.y(), 1.0));
    m_motionBiasB.setX(qBound(-1.0, m_motionBiasB.x(), 1.0));
    m_motionBiasB.setY(qBound(-1.0, m_motionBiasB.y(), 1.0));
    m_motionBiasValid = true;
    m_motionTravelDir = m_motionBiasB - m_motionBiasA;
    m_motionSign = (m_motionTravelDir.y() >= 0.0) ? 1.0 : -1.0;
    return true;
}

void ImageView::applyGeometricMotionBiases(uint seed)
{
    static const QPointF kBias[8] = {
        QPointF(-1.0, -1.0), QPointF(1.0, -1.0),
        QPointF(-1.0, 1.0), QPointF(1.0, 1.0),
        QPointF(-1.0, 0.0), QPointF(1.0, 0.0),
        QPointF(0.0, -1.0), QPointF(0.0, 1.0),
    };
    m_motionBiasA = kBias[seed % 8];
    m_motionBiasB = kBias[(seed / 8 + 3) % 8];
    if (qFuzzyCompare(m_motionBiasA.x(), m_motionBiasB.x())
        && qFuzzyCompare(m_motionBiasA.y(), m_motionBiasB.y())) {
        m_motionBiasB = kBias[(seed + 5) % 8];
    }
    if (qAbs(m_motionBiasA.x() - m_motionBiasB.x()) < 0.1
        && qAbs(m_motionBiasA.y() - m_motionBiasB.y()) < 0.1) {
        m_motionBiasB = kBias[(seed + 7) % 8];
    }
    m_motionBiasValid = true;
    m_motionTravelDir = m_motionBiasB - m_motionBiasA;
    m_motionSign = (m_motionTravelDir.y() >= 0.0) ? 1.0 : -1.0;
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
    // Any sharper sample may replace the phase buffer (soft → 512 → need).
    // Atlas rebuild is throttled in finishSlideshowPhaseBufferUpgrade so
    // intermediate steps do not thrash the GUI; rejecting intermediates here
    // left the phase stuck on soft when PreferCache plateaued below need.
    if (path == m_ssFromPath && sampleEdge > ImageCache::longEdge(m_ssFromImage)) {
        return true;
    }
    if (path == m_ssToPath && sampleEdge > ImageCache::longEdge(m_ssToImage)) {
        return true;
    }
    return false;
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
    const auto it = m_itemStates.constFind(path);
    if (it != m_itemStates.cend() && SessionAppearance::hasContentAppearance(*it)) {
        *out = *it;
        return true;
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
    if (generation != m_ssPhaseUpgradeGeneration) {
        return;
    }
    if (path.isEmpty() || oriented.isNull()) {
        return;
    }
    const int incoming = ImageCache::longEdge(oriented);
    const int need = slideshowNeedEdge(slideshowTargetEdge());
    bool changed = false;
    if (path == m_ssFromPath && incoming > ImageCache::longEdge(m_ssFromImage)) {
        m_ssFromImage = oriented;
        ImageCache::stampDebugOverlayIfEnabled(&m_ssFromImage, path);
        m_dwellSourceImage = m_ssFromImage;
        // Rebuild when atlas missing/stale soft-upsample, or sample meets need.
        const DwellAtlasParams params = dwellAtlasParams();
        const bool needAtlas =
            m_dwellAtlas.isNull()
            || incoming >= need
            || incoming > ThumtooCache::kGalleryLadderEdge
            || !dwellAtlasCoversSource(m_dwellAtlas, m_dwellAtlasScale,
                                       m_dwellAtlasVw, m_dwellAtlasVh, params,
                                       oriented);
        if (needAtlas) {
            requestDwellAtlasRebuild();
        }
        changed = true;
    }
    if (path == m_ssToPath && incoming > ImageCache::longEdge(m_ssToImage)) {
        m_ssToImage = oriented;
        ImageCache::stampDebugOverlayIfEnabled(&m_ssToImage, path);
        const DwellAtlasParams params = dwellAtlasParams();
        const bool needAtlas =
            m_ssToAtlas.isNull()
            || incoming >= need
            || incoming > ThumtooCache::kGalleryLadderEdge
            || !dwellAtlasCoversSource(m_ssToAtlas, m_ssToAtlasScale, m_ssToAtlasVw,
                                       m_ssToAtlasVh, params, oriented);
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
    if (path != m_ssFromPath && path != m_ssToPath) {
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
    const quint64 gen = ++m_ssPhaseUpgradeGeneration;
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
        if (generation != m_dwellAtlasRebuildGeneration) {
            return;
        }
        m_dwellAtlas = QPixmap::fromImage(scaled);
        m_dwellAtlasScale = atlasScale;
        m_dwellAtlasVw = atlasVw;
        m_dwellAtlasVh = atlasVh;
    } else {
        if (generation != m_ssToAtlasRebuildGeneration) {
            return;
        }
        m_ssToAtlas = QPixmap::fromImage(scaled);
        m_ssToAtlasScale = atlasScale;
        m_ssToAtlasVw = atlasVw;
        m_ssToAtlasVh = atlasVh;
    }
    if (viewport() && m_slideshowProgressActive) {
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
    if (!viewport() || m_slideshowNavHot) {
        return;
    }
    const QImage *source = (kind == SlideshowAtlasKind::From)
                               ? &m_dwellSourceImage
                               : &m_ssToImage;
    if (!source || source->isNull()) {
        return;
    }
    const DwellAtlasParams params = dwellAtlasParams();
    if (!params.valid) {
        return;
    }
    const QPixmap *atlas = (kind == SlideshowAtlasKind::From) ? &m_dwellAtlas : &m_ssToAtlas;
    const qreal aScale = (kind == SlideshowAtlasKind::From) ? m_dwellAtlasScale : m_ssToAtlasScale;
    const int aVw = (kind == SlideshowAtlasKind::From) ? m_dwellAtlasVw : m_ssToAtlasVw;
    const int aVh = (kind == SlideshowAtlasKind::From) ? m_dwellAtlasVh : m_ssToAtlasVh;
    if (dwellAtlasCoversSource(*atlas, aScale, aVw, aVh, params, *source)) {
        return;
    }

    const quint64 gen = (kind == SlideshowAtlasKind::From)
                            ? ++m_dwellAtlasRebuildGeneration
                            : ++m_ssToAtlasRebuildGeneration;
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
        (path == m_ssFromPath) ? ImageCache::longEdge(m_ssFromImage)
        : (path == m_ssToPath)  ? ImageCache::longEdge(m_ssToImage)
                                 : 0;
    putSlideshowRaster(path, image);

    const bool isPhasePath = (path == m_ssFromPath || path == m_ssToPath);
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
    } else if (viewport() && m_slideshowProgressActive) {
        viewport()->update();
    }

    // Climb while the *phase buffer* is still short of target (not only when
    // the host cache edge increases).
    if (m_pathRaster) {
        const int target = cappedDisplayEdgeForPath(path, slideshowTargetEdge());
        const int need = slideshowNeedEdge(target);
        const int phaseHave =
            (path == m_ssFromPath) ? ImageCache::longEdge(m_ssFromImage)
                                   : ImageCache::longEdge(m_ssToImage);
        if (phaseHave < need || incoming < need) {
            m_pathRaster->ensure(path, target, logicalSizeForPath(path),
                                 PathRasterService::ClimbPolicy::EscalateToFull);
        }
    }
}


void ImageView::displayQualityWatchdogTick()
{
    // Image-mode canvas: host better than painted, or LQIP while climbing to soft+.
    if (isImageMode()) {
        ImageItem *item = primaryItem();
        if (item && !item->path().isEmpty()) {
            const QString path = item->path();
            const int shown = item->displayPixelLongEdge();
            const int target = cappedDisplayEdgeForPath(
                path, qMax(viewport() ? qMax(viewport()->width(), viewport()->height()) : 0,
                           DisplayQuality::kSoftMaxEdge));
            const bool pending =
                m_pathRaster && m_pathRaster->isClimbPending(path);
            const DisplayQuality::Check dq =
                DisplayQuality::checkSurface(path, shown, target, pending);
            if (dq.verdict == DisplayQuality::Verdict::InstallHostBetter) {
                const QImage host = ImageCache::get(path);
                if (!host.isNull()
                    && canAcceptDisplaySample(
                        item, host, SessionAppearance::PixelKind::SoftPreview)) {
                    installDisplayPixels(item, host,
                                         SessionAppearance::PixelKind::SoftPreview,
                                         item->sessionId());
                    if (viewport()) {
                        viewport()->update();
                    }
                } else {
                    DisplayQuality::reportViolation("image", path, dq, false);
                }
            } else if (dq.verdict != DisplayQuality::Verdict::Ok && !pending) {
                DisplayQuality::reportViolation(
                    "image", path, dq,
                    dq.verdict == DisplayQuality::Verdict::StuckWeak);
                if (m_pathRaster) {
                    m_pathRaster->ensure(
                        path, target, logicalSizeForPath(path),
                        PathRasterService::ClimbPolicy::EscalateToFull);
                }
            }
        }
    }

    // Slideshow phase buffers must track host upgrades (same bug class as gallery).
    if (m_slideshowProgressActive) {
        auto checkPhase = [this](const QString &path, int shownEdge, const char *tag) {
            if (path.isEmpty()) {
                return;
            }
            const int target = slideshowTargetEdge();
            const DisplayQuality::Check dq =
                DisplayQuality::checkSurface(path, shownEdge, target, false);
            if (dq.verdict == DisplayQuality::Verdict::InstallHostBetter) {
                const QImage host = ImageCache::get(path);
                if (!host.isNull()) {
                    scheduleSlideshowPhaseBufferUpgrade(path, host);
                }
                DisplayQuality::reportViolation(tag, path, dq, false);
            } else if (dq.verdict == DisplayQuality::Verdict::StuckWeak) {
                DisplayQuality::reportViolation(tag, path, dq, true);
                if (m_pathRaster) {
                    m_pathRaster->ensure(
                        path, slideshowTargetEdge(), logicalSizeForPath(path),
                        PathRasterService::ClimbPolicy::EscalateToFull);
                }
            }
        };
        checkPhase(m_ssFromPath, ImageCache::longEdge(m_ssFromImage), "slideshow-from");
        checkPhase(m_ssToPath, ImageCache::longEdge(m_ssToImage), "slideshow-to");
    }
}

QString ImageView::slideshowPrefetchHudLine() const
{
    // Ladder edge chips ("Loading 1024→2048") were noisy and not actionable —
    // especially with HUD off. Prefetch still runs; status is not shown here.
    Q_UNUSED(m_slideshowProgressActive);
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
        // Soft + PreferCache/Full via PathRaster only (contract §1).
        if (m_pathRaster) {
            m_pathRaster->ensure(path, edge, logicalSizeForPath(path),
                                 PathRasterService::ClimbPolicy::EscalateToFull);
        } else if (ThumtooCache::isAvailable()) {
            (void)ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge);
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
    for (int i = 0; i < m_pathOrder.size() && i < m_sessionIdOrder.size(); ++i) {
        if (m_pathOrder.at(i) == path) {
            return m_sessionIdOrder.at(i);
        }
    }
    return kInvalidSessionImageId;
}

QImage ImageView::orientSlideshowImage(const QImage &raw, const QString &path) const
{
    // Soft/slideshow path: flip + quarter-turns + grade only (no crop bake).
    // Shares appearance resolution with scheduleSlideshowPhaseBufferUpgrade.
    if (raw.isNull() || path.isEmpty()) {
        return raw;
    }
    WorkspaceItemState app;
    if (!snapshotSlideshowContentAppearance(path, &app)) {
        return raw;
    }
    const QImage oriented = SessionAppearance::materializeDisplay(
        raw, app, SessionAppearance::PixelKind::SoftPreview);
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
    const int vw = m_zoomBlurVw > 0 ? m_zoomBlurVw
                                    : (viewport() ? viewport()->width() : 0);
    const int vh = m_zoomBlurVh > 0 ? m_zoomBlurVh
                                    : (viewport() ? viewport()->height() : 0);
    const qint64 keepFrom = slideshowZoomBlurKey(fromPath, vw, vh);
    const qint64 keepTo = slideshowZoomBlurKey(toPath, vw, vh);
    for (int i = 0; i < 2; ++i) {
        const qint64 k = m_zoomBlurSourceKey[i];
        if (k != 0 && k != keepFrom && k != keepTo) {
            m_zoomBlurUnderlay[i] = QPixmap();
            m_zoomBlurSourceKey[i] = 0;
        }
        // Drop in-flight markers for keys we no longer care about so slots
        // free for the new pair (without invalidating generation).
        const qint64 fk = m_zoomBlurInFlightKey[i];
        if (fk != 0 && fk != keepFrom && fk != keepTo
            && m_zoomBlurInFlightGen[i] == m_zoomBlurGeneration) {
            m_zoomBlurInFlightGen[i] = 0;
            m_zoomBlurInFlightKey[i] = 0;
        }
    }
    // Keep lastGood across path changes — previous underlay holds until the
    // new key finishes (paintZoomBlurUnderlay draws it).
}

void ImageView::schedulePhaseZoomBlur(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull() || !viewport()) {
        return;
    }
    const QSize vs = viewport()->size();
    const qint64 key = slideshowZoomBlurKey(path, vs.width(), vs.height());
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
    const QPointF saveA = m_motionBiasA;
    const QPointF saveB = m_motionBiasB;
    const bool saveV = m_motionBiasValid;
    m_motionBiasValid = false;
    pickInterestingMotionBiases(qHash(path), image);
    *outA = m_motionBiasA;
    *outB = m_motionBiasB;
    m_motionBiasA = saveA;
    m_motionBiasB = saveB;
    m_motionBiasValid = saveV;
}

void ImageView::ensureSlideshowMotionTimer()
{
    if (m_motionTimer) {
        return;
    }
    m_motionTimer = new QTimer(this);
    m_motionTimer->setTimerType(Qt::PreciseTimer);
    m_motionTimer->setInterval(16);
    connect(m_motionTimer, &QTimer::timeout, this, &ImageView::tickSlideshowMotion);
}

bool ImageView::shouldPromoteSlideshowToAsFrom(const QString &fromPath) const
{
    // Spec: B moves through transition *and* its following interval.
    // When dwell becomes B after A+B fade, promote B's motion — do not restart at 0.
    return !fromPath.isEmpty() && fromPath == m_ssToPath && m_ssToMotionClockRunning;
}

void ImageView::promoteSlideshowFromToPhase(const QString &fromPath)
{
    m_ssFromImage = !m_ssToImage.isNull() ? m_ssToImage
                                         : slideshowPixelsForPath(fromPath);
    m_motionBiasA = m_ssToBiasA;
    m_motionBiasB = m_ssToBiasB;
    m_motionBiasValid = true;
    m_motionBiasPath = fromPath;
    m_ssFromMotionClock = m_ssToMotionClock;
    m_ssFromMotionClockRunning = true;
    m_ssFromMotionT = m_ssToMotionT;
    m_dwellMotionT = m_ssFromMotionT;
    // Keep the to-atlas as the from/dwell atlas — clearing it forced multi-MP
    // drawImage every frame until rebuild (visible frame drops on promote).
    if (!m_ssToAtlas.isNull()) {
        m_dwellAtlas = m_ssToAtlas;
        m_dwellAtlasScale = m_ssToAtlasScale;
        m_dwellAtlasVw = m_ssToAtlasVw;
        m_dwellAtlasVh = m_ssToAtlasVh;
        m_dwellAtlasRebuildGeneration = m_ssToAtlasRebuildGeneration;
    }
}

void ImageView::startSlideshowFromPhase(const QString &fromPath)
{
    // Unoriented clamp only — orient + atlas run async (see prepareSlideshowFromDwell).
    // Sync orientSlideshowImage of a 2k sample on every ←/→ was dropping frames.
    m_ssFromImage = slideshowSampleUnoriented(fromPath);
    if (m_ssFromImage.isNull() && !fromPath.isEmpty()) {
        m_ssFromImage = ImageCache::clampToMaxEdge(
            slideshowSoftPlaceholder(fromPath), slideshowTargetEdge());
    }
    if (!fromPath.isEmpty()) {
        (void)ensureSlideshowLogicalSize(fromPath);
        if (m_ssFromImage.isNull()
            || ImageCache::longEdge(m_ssFromImage)
                   < slideshowNeedEdge(slideshowTargetEdge())) {
            preloadSlideshowImage(fromPath);
        }
        m_motionBiasValid = false;
        pickInterestingMotionBiases(qHash(fromPath), m_ssFromImage);
        m_motionBiasPath = fromPath;
    }
    m_ssFromMotionClock.start();
    m_ssFromMotionClockRunning = true;
    m_ssFromMotionT = 0.0;
    m_dwellMotionT = 0.0;
}

void ImageView::prepareSlideshowFromDwell(const QString &fromPath)
{
    m_dwellSourceImage = m_ssFromImage;
    if (m_ssFromImage.isNull()) {
        return;
    }
    ++m_ssPhaseUpgradeGeneration; // drop mid-slide upgrades for previous path
    // If promote already transferred the to-atlas, keep it (same image, continuous
    // motion). Only drop when the atlas cannot cover this source / viewport —
    // requestDwellAtlasRebuild is a no-op when coverage is adequate.
    // Fresh startSlideshowFromPhase leaves a wrong-path atlas; clear only then.
    if (m_dwellAtlas.isNull()
        || !dwellAtlasCoversSource(m_dwellAtlas, m_dwellAtlasScale, m_dwellAtlasVw,
                                   m_dwellAtlasVh, dwellAtlasParams(),
                                   m_ssFromImage)) {
        invalidateDwellAtlasRebuilds();
        m_dwellAtlas = QPixmap();
        m_dwellAtlasScale = 0.0;
        m_dwellAtlasVw = 0;
        m_dwellAtlasVh = 0;
    }
    // Async atlas — never scale multi-MP on the GUI during ←/→ or phase arm.
    // paintMotionCover falls back to drawImage until the atlas is ready.
    requestDwellAtlasRebuild();
    schedulePhaseZoomBlur(fromPath, m_ssFromImage);
    // Orient if durable appearance requires it (async, does not block this stack).
    if (!fromPath.isEmpty()) {
        scheduleSlideshowPhaseBufferUpgrade(fromPath, m_ssFromImage);
    }
}

void ImageView::armSlideshowMotionClock(int pathMs)
{
    if (m_slideshowMotion == SlideshowMotion::Off || pathMs < 250) {
        return;
    }
    ensureSlideshowMotionTimer();
    m_slideshowMotionActive = true;
    m_motionDurationMs = pathMs;
    // While paused, arm motion state but do not run the timer.
    if (!m_slideshowMotionPaused) {
        m_motionTimer->start();
    }
}

void ImageView::armSlideshowFromPhase(const QString &fromPath, int pathMs)
{
    const bool promote = shouldPromoteSlideshowToAsFrom(fromPath);
    m_ssFromPath = fromPath;
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
        << " " << m_ssFromImage.width() << "x" << m_ssFromImage.height()
        << (promote ? " (continue)" : " (start)");
}

void ImageView::armSlideshowToPhase(const QString &toPath)
{
    if (toPath.isEmpty()) {
        m_ssToPath.clear();
        m_ssToImage = QImage();
        ++m_ssToAtlasRebuildGeneration;
        m_ssToAtlas = QPixmap();
        m_ssToAtlasScale = 0.0;
        m_ssToAtlasVw = 0;
        m_ssToAtlasVh = 0;
        m_ssToMotionClockRunning = false;
        m_ssToMotionT = 0.0;
        return;
    }
    m_ssToPath = toPath;
    (void)ensureSlideshowLogicalSize(toPath);
    m_ssToImage = slideshowSampleUnoriented(toPath);
    if (m_ssToImage.isNull()) {
        m_ssToImage = ImageCache::clampToMaxEdge(
            slideshowSoftPlaceholder(toPath), slideshowTargetEdge());
    }
    // Cold to-path: kick preload immediately (do not wait for neighbour pump).
    if (m_ssToImage.isNull()
        || ImageCache::longEdge(m_ssToImage) < slideshowNeedEdge(slideshowTargetEdge())) {
        preloadSlideshowImage(toPath);
    }
    captureMotionBiasesForPath(toPath, m_ssToImage, &m_ssToBiasA, &m_ssToBiasB);
    m_ssToMotionClock.start();
    m_ssToMotionClockRunning = true;
    m_ssToMotionT = 0.0;
    if (!m_ssToImage.isNull()) {
        schedulePhaseZoomBlur(toPath, m_ssToImage);
    }
    ++m_ssToAtlasRebuildGeneration; // drop stale to-atlas jobs
    m_ssToAtlas = QPixmap();
    requestToPhaseAtlasRebuild();
    if (!m_ssToImage.isNull()) {
        scheduleSlideshowPhaseBufferUpgrade(toPath, m_ssToImage);
    }
    qCDebug(lcSlideshow).nospace()
        << "[slideshow] phase-to "
        << QFileInfo(toPath).fileName()
        << " " << m_ssToImage.width() << "x" << m_ssToImage.height();
}

int ImageView::slideshowPathDurationMs() const
{
    return qMax(250, m_slideshowProgressIntervalMs
                     + qMax(0, m_slideshowTransitionDurationMs));
}

void ImageView::warmZoomBlurForCurrentPhase()
{
    // Skip while user is key-repeating — builds fight soft decode.
    if (!viewport() || m_slideshowNavHot
        || m_slideshowLetterboxFill != SlideshowLetterboxFill::ZoomBlur) {
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
    warm(m_ssFromPath, m_ssFromImage.isNull() ? m_dwellSourceImage : m_ssFromImage);
    warm(m_ssToPath, m_ssToImage);
}

bool ImageView::applySlideshowFadeProgressOnly(qreal fadeT)
{
    // Pure-phase clock ticks at 16ms with unchanged from/to — only advance fade.
    if (qFuzzyCompare(fadeT, m_ssFadeT) || (fadeT < 0.0 && m_ssFadeT < 0.0)) {
        return false;
    }
    m_ssFadeT = fadeT;
    if (viewport()) {
        viewport()->update();
    }
    return true;
}

void ImageView::updateSlideshowPhaseMotionProgress(int pathMs)
{
    // T ∈ [0,1] is authority; clocks only measure Δt for integration.
    // Interval / pathMs changes alter rate only — progress is not remapped.
    integrateMotionProgress01(&m_ssFromMotionT, &m_ssFromMotionClock,
                              m_ssFromMotionClockRunning, m_slideshowMotionPaused,
                              pathMs);
    if (m_ssFromMotionClockRunning) {
        m_dwellMotionT = m_ssFromMotionT;
    }
    integrateMotionProgress01(&m_ssToMotionT, &m_ssToMotionClock,
                              m_ssToMotionClockRunning, m_slideshowMotionPaused,
                              pathMs);
}

void ImageView::setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT)
{
    if (!m_slideshowProgressActive) {
        return;
    }

    const bool fromChanged = (fromPath != m_ssFromPath);
    const bool toChanged = (toPath != m_ssToPath);
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
    // Keep m_ssRasterPending look-ahead (do not clear — cancelled +2/+3 warm-up).
    if (fromChanged) {
        armSlideshowFromPhase(fromPath, pathMs);
    }
    if (toPath.isEmpty()) {
        armSlideshowToPhase(QString()); // clear
    } else if (toChanged) {
        armSlideshowToPhase(toPath);
    }
    updateSlideshowPhaseMotionProgress(pathMs);
    m_ssFadeT = fadeT;

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
                m_ssFromImage.width(), m_ssFromImage.height(),
                m_ssToImage.width(), m_ssToImage.height());
    }

    warmZoomBlurForCurrentPhase();
    hideSlideshowUnderlay();
    if (viewport()) {
        viewport()->update();
    }
}

qreal ImageView::slideshowMotionHeadroom() const
{
    // Ken Burns / pan-scan sample past 1:1 cover; need extra source pixels or
    // the zoomed region is soft. Off → 1.0. PanZoom uses the configured factor.
    if (!m_slideshowProgressActive
        || m_slideshowMotion == SlideshowMotion::Off) {
        return 1.0;
    }
    if (m_slideshowMotion == SlideshowMotion::PanZoom) {
        return qBound(1.05, m_panZoomFactor, 1.50);
    }
    // PanScan can raise scale when travel is short.
    return 1.25;
}

int ImageView::slideshowTargetEdge() const
{
    // Viewport × DPR × motion headroom, ladder-snapped. Cap at image ladder
    // display max (kImageLadderEdge), not unbounded native — interim until tiles.
    if (!viewport()) {
        return ThumtooCache::kImageLadderEdge;
    }
    const qreal dpr = devicePixelRatioF();
    const QSize vs = viewport()->size();
    const qreal head = slideshowMotionHeadroom();
    const int longPx = int(qCeil(qMax(vs.width(), vs.height()) * dpr * head));
    const int snapped = ThumtooCache::ceilLadderEdge(
        qMax(longPx, ThumtooCache::kGalleryLadderEdge));
    return qMin(snapped, ThumtooCache::kImageLadderEdge);
}

QSize ImageView::logicalSizeForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const auto it = m_imageSizeByPath.constFind(path);
    if (it != m_imageSizeByPath.cend() && isPositiveSize(*it)) {
        return *it;
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
    if (!logical.isValid() || logical.width() < 1 || logical.height() < 1) {
        return 1.0;
    }
    const qreal iw = qreal(logical.width());
    const qreal ih = qreal(logical.height());
    const qreal w = qreal(qMax(1, vw));
    const qreal h = qreal(qMax(1, vh));
    switch (m_slideshowZoom) {
    case SlideshowZoom::Fill:
        return qMax(w / iw, h / ih);
    case SlideshowZoom::Actual:
        return 1.0;
    case SlideshowZoom::Fit:
    default:
        return qMin(w / iw, h / ih);
    }
}

void ImageView::setSlideshowNavHot(bool hot)
{
    if (m_slideshowNavHot == hot) {
        return;
    }
    m_slideshowNavHot = hot;
    // Do NOT invalidateZoomBlurQueue here — keep the previous underlay until a
    // new key's blur is ready (solid flash on every ←/→ was the bug).
}

void ImageView::pumpSlideshowPreloadQueue()
{
    // Start at most one pending path now that an inflight slot freed.
    const int needEdge = slideshowNeedEdge(slideshowTargetEdge());
    while (!m_ssRasterPending.isEmpty()) {
        const QString next = m_ssRasterPending.takeFirst();
        if (m_ssRasterInflight.contains(next)) {
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
    m_ssRasterInflight.remove(path);
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
    const int targetEdge = cappedDisplayEdgeForPath(path, slideshowTargetEdge());
    const int need = slideshowNeedEdge(targetEdge);
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
        // ensure() with EscalateToFull schedules Full once if not yet done.
        // Second call is a no-op once fullDone; still safe to ensure.
    }

    qCDebug(lcSlideshow).nospace()
        << "[slideshow] preload-ensure " << QFileInfo(path).fileName()
        << " edge=" << targetEdge
        << " have=" << haveEdge;

    // Soft → PreferCache → Full (contract ClimbPolicy::EscalateToFull).
    m_pathRaster->ensure(path, targetEdge, native,
                         PathRasterService::ClimbPolicy::EscalateToFull);

    const QImage have = ImageCache::get(path);
    if (!have.isNull()) {
        onSlideshowRasterReady(path, have);
    }
}

ImageView::DwellAtlasParams ImageView::dwellAtlasParams() const
{
    // Atlas size is a function of the *viewport* and motion headroom only —
    // not of the source raster's pixel dimensions. Camera dest is aspect-based;
    // the atlas is just a sharp enough texture to sample under max zoom.
    DwellAtlasParams p;
    if (!viewport()) {
        return p;
    }
    p.vw = qMax(1, viewport()->width());
    p.vh = qMax(1, viewport()->height());
    p.headroom = slideshowMotionHeadroom();
    p.longCap = int(qCeil(qreal(qMax(p.vw, p.vh)) * p.headroom));
    p.keyScale = p.headroom;
    p.valid = p.longCap > 0;
    return p;
}

bool ImageView::dwellAtlasCoversSource(const QPixmap &atlas, qreal atlasScale,
                                       int atlasVw, int atlasVh,
                                       const DwellAtlasParams &params,
                                       const QImage &source) const
{
    if (!params.valid || atlas.isNull() || source.isNull()) {
        return false;
    }
    if (!qFuzzyCompare(atlasScale, params.keyScale) || atlasVw != params.vw
        || atlasVh != params.vh || atlas.width() < params.longCap * 9 / 10) {
        return false;
    }
    const int have = qMax(atlas.width(), atlas.height());
    const int srcLong = qMax(source.width(), source.height());
    // Atlas is always ~longCap (viewport budget). Soft samples are *upscaled*
    // into it, so srcLong << have does NOT mean the atlas is sharp — the old
    // "srcLong <= have*5/4 → cover" test left soft-looking atlases on screen
    // forever while PreferCache delivered 1024/native.
    //
    // Soft band only: keep the soft-upscaled atlas (skip 256↔512 thrash).
    // Above soft: require a rebuild so HQ replaces the soft upsample.
    // At/above longCap: atlas is adequate if it fills the budget.
    if (srcLong >= (params.longCap * 9) / 10) {
        return have >= (params.longCap * 9) / 10;
    }
    if (srcLong <= ThumtooCache::kGalleryLadderEdge) {
        return true;
    }
    // PreferCache mid/high sample while atlas is still a soft upsample.
    return false;
}

void ImageView::invalidateDwellAtlasRebuilds()
{
    ++m_dwellAtlasRebuildGeneration;
}

void ImageView::ensureMotionAtlas(const QImage &image, QPixmap *atlas,
                                  qreal *atlasScale, int *atlasVw, int *atlasVh) const
{
    if (!atlas || !atlasScale || !atlasVw || !atlasVh || image.isNull() || !viewport()) {
        return;
    }
    // Rapid keyboard flip: skip atlas rebuild; paintMotionCover falls back to
    // drawImage. Avoids a scale per key on the GUI thread.
    if (m_slideshowNavHot) {
        return;
    }
    const DwellAtlasParams params = dwellAtlasParams();
    if (!params.valid) {
        return;
    }
    if (dwellAtlasCoversSource(*atlas, *atlasScale, *atlasVw, *atlasVh, params, image)) {
        return;
    }
    const int srcLong = qMax(image.width(), image.height());
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
    if (m_slideshowProgressActive) {
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

/**
 * Tiny cover-frame blur for letterbox underlay. Intentionally low-res: the
 * underlay is out of focus and the GPU scales it to the viewport. Cap the
 * long edge so even a first-build on the GUI thread stays cheap.
 */
QImage makeZoomBlurCover(const QImage &src, int vw, int vh)
{
    if (src.isNull() || vw < 1 || vh < 1) {
        return {};
    }
    const qreal iw = qreal(src.width());
    const qreal ih = qreal(src.height());
    if (iw < 1.0 || ih < 1.0) {
        return {};
    }
    // ~1/16 of the viewport, hard-capped — soft background, not a second slide.
    constexpr int kMaxLongEdge = 128;
    int workW = qMax(8, vw / 16);
    int workH = qMax(8, vh / 16);
    const int longEdge = qMax(workW, workH);
    if (longEdge > kMaxLongEdge) {
        const qreal s = qreal(kMaxLongEdge) / qreal(longEdge);
        workW = qMax(8, int(workW * s));
        workH = qMax(8, int(workH * s));
    }
    const qreal cover = qMax(qreal(workW) / iw, qreal(workH) / ih);
    const int sw = qMax(1, int(std::ceil(iw * cover)));
    const int sh = qMax(1, int(std::ceil(ih * cover)));
    // FastTransformation: underlay is blurred anyway; Smooth is pure CPU cost.
    QImage scaled = src.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // Centre-crop to workW×workH
    const int x0 = qMax(0, (sw - workW) / 2);
    const int y0 = qMax(0, (sh - workH) / 2);
    scaled = scaled.copy(x0, y0, qMin(workW, scaled.width()), qMin(workH, scaled.height()));
    if (scaled.width() != workW || scaled.height() != workH) {
        QImage canvas(workW, workH, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::black);
        QPainter p(&canvas);
        p.drawImage((workW - scaled.width()) / 2, (workH - scaled.height()) / 2, scaled);
        p.end();
        scaled = canvas;
    }

    auto boxBlurPass = [](QImage &img, int radius, bool horizontal) {
        if (radius < 1) {
            return;
        }
        const int w = img.width();
        const int h = img.height();
        QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
        const int diam = radius * 2 + 1;
        if (horizontal) {
            for (int y = 0; y < h; ++y) {
                const QRgb *inLine = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
                int rSum = 0, gSum = 0, bSum = 0, aSum = 0;
                for (int i = -radius; i <= radius; ++i) {
                    const QRgb px = inLine[qBound(0, i, w - 1)];
                    rSum += qRed(px); gSum += qGreen(px); bSum += qBlue(px); aSum += qAlpha(px);
                }
                outLine[0] = qRgba(rSum / diam, gSum / diam, bSum / diam, aSum / diam);
                for (int x = 1; x < w; ++x) {
                    const QRgb leave = inLine[qBound(0, x - radius - 1, w - 1)];
                    const QRgb enter = inLine[qBound(0, x + radius, w - 1)];
                    rSum += qRed(enter) - qRed(leave);
                    gSum += qGreen(enter) - qGreen(leave);
                    bSum += qBlue(enter) - qBlue(leave);
                    aSum += qAlpha(enter) - qAlpha(leave);
                    outLine[x] = qRgba(rSum / diam, gSum / diam, bSum / diam, aSum / diam);
                }
            }
        } else {
            // Vertical: column-wise sliding window
            QVector<int> rSum(w), gSum(w), bSum(w), aSum(w);
            rSum.fill(0); gSum.fill(0); bSum.fill(0); aSum.fill(0);
            for (int i = -radius; i <= radius; ++i) {
                const int yy = qBound(0, i, h - 1);
                const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(yy));
                for (int x = 0; x < w; ++x) {
                    const QRgb px = line[x];
                    rSum[x] += qRed(px); gSum[x] += qGreen(px);
                    bSum[x] += qBlue(px); aSum[x] += qAlpha(px);
                }
            }
            QRgb *out0 = reinterpret_cast<QRgb *>(out.scanLine(0));
            for (int x = 0; x < w; ++x) {
                out0[x] = qRgba(rSum[x] / diam, gSum[x] / diam, bSum[x] / diam, aSum[x] / diam);
            }
            for (int y = 1; y < h; ++y) {
                const int leaveY = qBound(0, y - radius - 1, h - 1);
                const int enterY = qBound(0, y + radius, h - 1);
                const QRgb *leaveLine = reinterpret_cast<const QRgb *>(img.constScanLine(leaveY));
                const QRgb *enterLine = reinterpret_cast<const QRgb *>(img.constScanLine(enterY));
                QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const QRgb leave = leaveLine[x];
                    const QRgb enter = enterLine[x];
                    rSum[x] += qRed(enter) - qRed(leave);
                    gSum[x] += qGreen(enter) - qGreen(leave);
                    bSum[x] += qBlue(enter) - qBlue(leave);
                    aSum[x] += qAlpha(enter) - qAlpha(leave);
                    outLine[x] = qRgba(rSum[x] / diam, gSum[x] / diam, bSum[x] / diam, aSum[x] / diam);
                }
            }
        }
        img = out;
    };

    // Two box passes, modest radius — strong enough once stretched to the
    // viewport; cheaper first-build on the GUI thread.
    constexpr int kRadius = 6;
    for (int pass = 0; pass < 2; ++pass) {
        boxBlurPass(scaled, kRadius, true);
        boxBlurPass(scaled, kRadius, false);
    }
    Q_UNUSED(vw);
    Q_UNUSED(vh);
    return scaled;
}

} // namespace

void ImageView::clearSlideshowZoomBlurSlots()
{
    // Drop cached underlays and cancel in-flight blur jobs (pad/letterbox/viewport).
    m_zoomBlurUnderlay[0] = QPixmap();
    m_zoomBlurUnderlay[1] = QPixmap();
    m_zoomBlurSourceKey[0] = 0;
    m_zoomBlurSourceKey[1] = 0;
    m_zoomBlurLastGood = QPixmap();
    m_zoomBlurLastGoodKey = 0;
    invalidateZoomBlurQueue();
}

void ImageView::invalidateZoomBlurQueue() const
{
    // Drop in-flight work so a page flip cannot leave a backlog of blur jobs.
    ++m_zoomBlurGeneration;
    m_zoomBlurInFlightGen[0] = m_zoomBlurInFlightGen[1] = 0;
    m_zoomBlurInFlightKey[0] = m_zoomBlurInFlightKey[1] = 0;
}

bool ImageView::zoomBlurKeyCached(qint64 key) const
{
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurSourceKey[i] == key && !m_zoomBlurUnderlay[i].isNull()) {
            return true;
        }
    }
    return false;
}

bool ImageView::zoomBlurKeyInFlight(qint64 key) const
{
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurInFlightGen[i] == m_zoomBlurGeneration
            && m_zoomBlurInFlightKey[i] == key) {
            return true;
        }
    }
    return false;
}

int ImageView::claimZoomBlurFlightSlot(qint64 key) const
{
    // Allow up to two concurrent builds (outgoing + incoming underlay). Never
    // cancel the other key mid-transition — that caused ZoomBlur flicker as
    // from/to fought over a single in-flight slot every paint frame.
    int flightSlot = -1;
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurInFlightKey[i] == 0
            || m_zoomBlurInFlightGen[i] != m_zoomBlurGeneration) {
            flightSlot = i;
            break;
        }
    }
    if (flightSlot < 0) {
        return -1; // both slots busy with other keys; try again next frame
    }
    const quint64 gen = m_zoomBlurGeneration;
    m_zoomBlurInFlightGen[flightSlot] = gen;
    m_zoomBlurInFlightKey[flightSlot] = key;
    return flightSlot;
}

void ImageView::installZoomBlurResult(const QImage &blurred, qint64 key, quint64 gen)
{
    if (gen != m_zoomBlurGeneration) {
        return; // page flipped — discard
    }
    int slot = -1;
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurSourceKey[i] == key) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = m_zoomBlurUnderlay[0].isNull() ? 0 : 1;
    }
    m_zoomBlurUnderlay[slot] = QPixmap::fromImage(blurred);
    m_zoomBlurSourceKey[slot] = key;
    m_zoomBlurLastGood = m_zoomBlurUnderlay[slot];
    m_zoomBlurLastGoodKey = key;
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurInFlightGen[i] == gen
            && m_zoomBlurInFlightKey[i] == key) {
            m_zoomBlurInFlightGen[i] = 0;
            m_zoomBlurInFlightKey[i] = 0;
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::scheduleZoomBlurBuild(const QImage &image, int vw, int vh, qint64 key) const
{
    if (m_slideshowNavHot) {
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
    const quint64 gen = m_zoomBlurGeneration;
    // Snapshot pixels for the worker (avoid touching GUI QImage after return).
    const QImage src = image.copy();
    const QPointer<ImageView> guard(const_cast<ImageView *>(this));
    QThreadPool::globalInstance()->start([guard, src, vw, vh, key, gen]() {
        const QImage blurred = makeZoomBlurCover(src, vw, vh);
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
    if (m_zoomBlurVw != vw || m_zoomBlurVh != vh) {
        // Viewport size change: drop sized slots; keep lastGood stretched until
        // async rebuild finishes (still better than solid flash).
        m_zoomBlurUnderlay[0] = QPixmap();
        m_zoomBlurUnderlay[1] = QPixmap();
        m_zoomBlurSourceKey[0] = 0;
        m_zoomBlurSourceKey[1] = 0;
        m_zoomBlurVw = vw;
        m_zoomBlurVh = vh;
        invalidateZoomBlurQueue();
    }
    int slot = -1;
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurSourceKey[i] == key && !m_zoomBlurUnderlay[i].isNull()) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(viewportRect, m_zoomBlurUnderlay[slot]);
        return;
    }
    // Miss: schedule build only when not in a key-repeat burst (pool pressure).
    // Always keep painting the previous underlay until this key is ready —
    // solid pad on every path change was the "discarded blurry background" bug.
    if (!m_slideshowNavHot) {
        scheduleZoomBlurBuild(image, vw, vh, key);
    }
    if (!m_zoomBlurLastGood.isNull()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(viewportRect, m_zoomBlurLastGood);
        return;
    }
    painter->fillRect(viewportRect, slideshowPadColor());
}

QSize ImageView::resolveMotionLogicalSize(const QImage &image, const QString &path) const
{
    // HARD RULE: definitive logical size owns geometry. Soft rasters are sampling
    // only — but while size is still provisional (archive square stand-in, or
    // probe not yet back), dest aspect must follow the *sample*. Otherwise
    // paintMotionCover stretches a 16:9 soft into a 1:1 dest (speed change /
    // seek storms many cold paths before probes finish).
    const QSize logical = logicalSizeForPath(path);
    if (isPositiveSize(logical) && !path.isEmpty() && !isProvisionalImageSize(path)) {
        return logical;
    }
    if (!image.isNull() && isPositiveSize(image.size())) {
        const QSize fromSample =
            scaleToLongEdge(image.size(), kProvisionalLayoutLongEdge);
        if (isPositiveSize(fromSample)) {
            return fromSample;
        }
    }
    if (isPositiveSize(logical)) {
        return logical;
    }
    return QSize(kProvisionalLayoutLongEdge, kProvisionalLayoutLongEdge);
}

QRectF ImageView::computeMotionCoverDestRect(qreal iw, qreal ih, int vw, int vh,
                                             qreal motionT, QPointF biasA, QPointF biasB,
                                             const QString &path) const
{
    motionT = qBound(0.0, motionT, 1.0);
    const QSize logical{int(iw), int(ih)};
    const qreal base = slideshowZoomBaseScale(logical, vw, vh);
    if (base <= 0.0 || !qIsFinite(base)) {
        return QRectF();
    }

    qreal scale = base;
    qreal biasX = 0.0;
    qreal biasY = 0.0;
    qreal destX = 0.0;
    qreal destY = 0.0;
    bool destFromOffset = false;

    if (m_slideshowMotion == SlideshowMotion::PanScan) {
        qreal s = base;
        const bool preferX = iw * qreal(vh) >= ih * qreal(vw);
        {
            const qreal viewW = qreal(vw) / s;
            const qreal viewH = qreal(vh) / s;
            const qreal halfX = qMax(0.0, (iw - viewW) * 0.5);
            const qreal halfY = qMax(0.0, (ih - viewH) * 0.5);
            const qreal travel = preferX ? halfX : halfY;
            const qreal kMinTravel = qMax(2.0, qMax(iw, ih) * 0.015);
            if (travel < kMinTravel) {
                const qreal longSide = preferX ? iw : ih;
                const qreal targetHalf = qMax(kMinTravel, longSide * 0.10);
                const qreal neededView = preferX ? (iw - 2.0 * targetHalf)
                                                 : (ih - 2.0 * targetHalf);
                if (neededView > 1.0) {
                    s = preferX ? (qreal(vw) / neededView) : (qreal(vh) / neededView);
                    s = qMax(s, base);
                }
            }
        }
        scale = s;
        const qreal along = -1.0 + 2.0 * motionT;
        if (preferX) {
            biasX = along;
            biasY = 0.0;
        } else {
            biasX = 0.0;
            biasY = along;
        }
    } else if (m_slideshowMotion == SlideshowMotion::PanZoom) {
        const qreal factor = qBound(1.02, m_panZoomFactor, 1.40);
        qreal motionBase = base;
        {
            constexpr qreal kMinHalf = 32.0;
            for (int i = 0; i < 10; ++i) {
                const qreal hx = qMax(0.0, (iw - qreal(vw) / motionBase) * 0.5);
                const qreal hy = qMax(0.0, (ih - qreal(vh) / motionBase) * 0.5);
                if (hx >= kMinHalf || hy >= kMinHalf) {
                    break;
                }
                motionBase *= 1.08;
            }
        }
        const qreal s0 = motionBase;
        const qreal s1 = motionBase * factor;
        scale = s0 + (s1 - s0) * motionT;

        if (!m_motionBiasValid && biasA == QPointF(-1.0, -1.0)
            && biasB == QPointF(1.0, 1.0)) {
            static const QPointF kBias[8] = {
                QPointF(-1.0, -1.0), QPointF(1.0, -1.0),
                QPointF(-1.0, 1.0), QPointF(1.0, 1.0),
                QPointF(-1.0, 0.0), QPointF(1.0, 0.0),
                QPointF(0.0, -1.0), QPointF(0.0, 1.0),
            };
            uint seed = path.isEmpty() ? 1u : uint(qHash(path));
            if (seed == 0) {
                seed = 1u;
            }
            biasA = kBias[seed % 8];
            biasB = kBias[(seed / 8 + 3) % 8];
        }
        // Linear path only: scale s0→s1, image-space pan off0→off1.
        // dest = viewportCentre − off×scale (no bias encoding, no overflow gate —
        // the old overflow>0 ? bias : 0 snap was a discontinuity when an axis
        // first gained crop room, often at a corner).
        const qreal half0x = qMax(0.0, (iw - qreal(vw) / s0) * 0.5);
        const qreal half0y = qMax(0.0, (ih - qreal(vh) / s0) * 0.5);
        const qreal half1x = qMax(0.0, (iw - qreal(vw) / s1) * 0.5);
        const qreal half1y = qMax(0.0, (ih - qreal(vh) / s1) * 0.5);
        const qreal offX = (biasA.x() * half0x)
            + (biasB.x() * half1x - biasA.x() * half0x) * motionT;
        const qreal offY = (biasA.y() * half0y)
            + (biasB.y() * half1y - biasA.y() * half0y) * motionT;
        const qreal dw = iw * scale;
        const qreal dh = ih * scale;
        destX = (qreal(vw) - dw) * 0.5 - offX * scale;
        destY = (qreal(vh) - dh) * 0.5 - offY * scale;
        destFromOffset = true;
    } else {
        scale = base;
        biasX = 0.0;
        biasY = 0.0;
    }

    qreal dw = iw * scale;
    qreal dh = ih * scale;
    if (!destFromOffset) {
        const qreal overflowX = qMax(0.0, (dw - qreal(vw)) * 0.5);
        const qreal overflowY = qMax(0.0, (dh - qreal(vh)) * 0.5);
        destX = (qreal(vw) - dw) * 0.5 - biasX * overflowX;
        destY = (qreal(vh) - dh) * 0.5 - biasY * overflowY;
    }
    return QRectF(destX, destY, dw, dh);
}

void ImageView::paintMotionCover(QPainter *painter, const QImage &image,
                                 qreal motionT, QPointF biasA, QPointF biasB,
                                 const QString &path) const
{
    if (!painter || image.isNull() || !viewport()) {
        return;
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());

    const QSize logical = resolveMotionLogicalSize(image, path);
    const qreal iw = qreal(logical.width());
    const qreal ih = qreal(logical.height());
    if (iw < 1.0 || ih < 1.0) {
        return;
    }

    const QRectF dest = computeMotionCoverDestRect(iw, ih, vw, vh, motionT, biasA, biasB, path);
    if (!dest.isValid() || dest.isEmpty()) {
        return;
    }

    // Prefer pre-scaled atlases matched by path (not QImage address — pure-phase
    // paint may pass temporaries). From/dwell → m_dwellAtlas; to → m_ssToAtlas.
    const QPixmap *atlas = nullptr;
    if (!path.isEmpty() && path == m_ssFromPath && !m_dwellAtlas.isNull()) {
        atlas = &m_dwellAtlas;
    } else if (!path.isEmpty() && path == m_ssToPath && !m_ssToAtlas.isNull()) {
        atlas = &m_ssToAtlas;
    } else if (path.isEmpty() && !m_dwellAtlas.isNull()
               && (&image == &m_dwellSourceImage || &image == &m_ssFromImage)) {
        atlas = &m_dwellAtlas;
    }

    if (atlas) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawPixmap(dest, *atlas, atlas->rect());
    } else {
        // No atlas yet: smooth when the sample is in the display budget so soft
        // placeholders are not nearest-neighbour. Only skip Smooth for huge
        // native samples (rare during slideshow — atlas should cover those).
        const int srcLong = qMax(image.width(), image.height());
        const int budget = qMax(vw, vh) * 2;
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
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    QImage out(vw, vh, QImage::Format_ARGB32_Premultiplied);
    out.fill(slideshowPadColor());
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // pathHash was historical; recover path from phase when possible.
    QString path;
    if (pathHash != 0 && qHash(m_ssToPath) == pathHash) {
        path = m_ssToPath;
    } else if (!m_ssFromPath.isEmpty()) {
        path = m_ssFromPath;
    }
    paintMotionCover(&painter, image, motionT, m_motionBiasA, m_motionBiasB, path);
    painter.end();
    return QPixmap::fromImage(out);
}

void ImageView::maybeStartSlideshowMotion()
{
    if (m_slideshowMotion == SlideshowMotion::Off || !m_slideshowProgressActive
        || !isImageMode()) {
        return;
    }
    // Already running — do not restart from 0.
    if (m_slideshowMotionActive) {
        return;
    }
    int duration = m_slideshowProgressIntervalMs;
    if (duration < 250) {
        return;
    }
    // Continue from the dwell sample if soft-handoff already set one.
    const qreal initial = qBound(0.0, m_dwellMotionT, 1.0);
    startSlideshowMotion(duration, initial);
}

bool ImageView::prepareSlideshowMotionDwell(ImageItem *item)
{
    // Ken Burns moves the *image* via blit, not the QGraphicsView camera.
    // Prefer path-oriented slideshow pixels (unbaked cache + appearance). Item
    // source may lag durable orientation on the first frame before a full
    // install, or be unbaked soft-only.
    const QString path = item->path();
    QImage dwell = slideshowSampleUnoriented(path);
    if (dwell.isNull()) {
        dwell = ImageCache::clampToMaxEdge(item->sourceImage(), slideshowTargetEdge());
    }
    m_dwellSourceImage = dwell;
    if (m_dwellSourceImage.isNull()) {
        return false;
    }
    // Align underlay camera to slideshow zoom before hiding it so cancel/stop
    // can restore a known static frame.
    applySlideshowZoomFraming(item);
    invalidateDwellAtlasRebuilds();
    requestDwellAtlasRebuild(); // async — do not scale on this stack
    if (!path.isEmpty()) {
        scheduleSlideshowPhaseBufferUpgrade(path, dwell);
    }
    setSlideshowUnderlayVisible(false);
    return true;
}

void ImageView::freezeScrollbarsForMotion()
{
    // Freeze scrollbars so the view cannot re-clamp/centre while the overlay
    // path is the only thing that should move (underlay is hidden).
    if (!m_motionSavedBarPolicies) {
        m_motionSavedHBarPolicy = horizontalScrollBarPolicy();
        m_motionSavedVBarPolicy = verticalScrollBarPolicy();
        m_motionSavedBarPolicies = true;
    }
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
    m_fitMode = false;
    m_fillMode = (m_slideshowZoom == SlideshowZoom::Fill);
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
    if (m_motionBiasValid && m_motionBiasPath != path) {
        m_motionBiasValid = false;
    }
    if (!m_motionBiasValid) {
        const QImage src = item ? item->sourceImage() : QImage();
        pickInterestingMotionBiases(qHash(path), src);
        m_motionBiasPath = path;
    } else if (m_motionBiasPath.isEmpty()) {
        m_motionBiasPath = path;
    }
}

void ImageView::retargetSlideshowMotionDuration(int durationMs)
{
    // Interval change while Ken Burns is running: keep atlas, biases, and
    // normalized progress; only the path duration changes.
    if (!m_slideshowMotionActive || m_slideshowMotion == SlideshowMotion::Off) {
        return;
    }
    if (durationMs < 250) {
        durationMs = 3000;
    }
    qreal progress = 0.0;
    if (m_motionDurationMs > 0) {
        qint64 elapsed = m_motionElapsedOffsetMs;
        if (m_motionClock.isValid() && !m_slideshowMotionPaused) {
            elapsed += m_motionClock.elapsed();
        }
        progress = qBound(0.0, qreal(elapsed) / qreal(m_motionDurationMs), 1.0);
    }
    const int pathMs = durationMs + qMax(0, m_slideshowTransitionDurationMs);
    m_motionDurationMs = qMax(durationMs, pathMs);
    m_motionElapsedOffsetMs = qint64(progress * qreal(m_motionDurationMs));
    m_motionClock.start();
    if (m_motionTimer && !m_slideshowMotionPaused) {
        m_motionTimer->start();
    }
}

void ImageView::startSlideshowMotion(int durationMs, qreal initialProgress)
{
    cancelSlideshowMotion();
    if (m_slideshowMotion == SlideshowMotion::Off || !isImageMode()
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
    m_slideshowMotionActive = true;
    // Path lasts longer than the dwell interval so that when the slideshow
    // advances (and crossfade runs), from-progress is still < 1 and keeps
    // lerping. Previously duration==interval → progress clamped at 1 for the
    // entire transition → motion looked frozen.
    const int pathMs = durationMs + qMax(0, m_slideshowTransitionDurationMs);
    m_motionDurationMs = qMax(durationMs, pathMs);
    initialProgress = qBound(0.0, initialProgress, 1.0);
    m_motionClock.start();
    m_dwellMotionT = qBound(0.0, initialProgress, 1.0);
    m_motionElapsedOffsetMs = (m_motionDurationMs > 0)
        ? qint64(m_dwellMotionT * qreal(m_motionDurationMs))
        : 0;
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
    // Pure dwell motion: integrate Δt into T ∈ [0,1] (same model as phase motion).
    if (m_motionDurationMs <= 0 || m_slideshowMotionPaused) {
        return;
    }
    if (!m_motionClock.isValid()) {
        m_motionClock.start();
        return;
    }
    const qint64 d = m_motionClock.restart();
    if (d > 0) {
        m_dwellMotionT =
            qBound(0.0, m_dwellMotionT + qreal(d) / qreal(m_motionDurationMs), 1.0);
        m_motionElapsedOffsetMs =
            qint64(m_dwellMotionT * qreal(m_motionDurationMs));
    }
}

void ImageView::tickSlideshowMotion()
{
    if (!m_slideshowMotionActive) {
        if (m_motionTimer) {
            m_motionTimer->stop();
        }
        return;
    }
    if (!viewport()) {
        return;
    }

    if (m_slideshowProgressActive
        && (m_ssFromMotionClockRunning || m_ssToMotionClockRunning)) {
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
    auto aspect = [](const QSize &s) -> qreal {
        return qreal(s.width()) / qreal(qMax(1, s.height()));
    };
    const bool aspectChanged =
        !beforeOk || qAbs(aspect(before) - aspect(after)) > 0.02;
    if (aspectChanged) {
        if (m_slideshowProgressActive && m_slideshowMotion == SlideshowMotion::Off) {
            applySlideshowZoomFraming(item);
        } else if (!m_slideshowProgressActive) {
            // Sticky Fill/1:1: reframe + restore pan (fitItem alone recentres).
            if (m_stickyZoomEnabled) {
                applyImageModeFraming(item);
            } else {
                fitItem(item, currentFitAspectMode());
            }
        }
    } else if (beforeOk && before != after && !m_slideshowProgressActive) {
        // Same aspect, larger/smaller logical size: scale the view so the image
        // keeps the same on-screen footprint (soft→native must not zoom).
        // Slideshow pure-phase paints via paintMotionCover (logical size) and
        // does not use the view matrix for framing.
        const qreal factor = qreal(before.width()) / qreal(after.width());
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
    if (m_scene) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }
}

void ImageView::fitItem(ImageItem *item, Qt::AspectRatioMode mode)
{
    if (!item) {
        return;
    }
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
    const QString path = item->path();
    if (!path.isEmpty() && !item->sessionHasCrop()) {
        const QSize logical = ensureLogicalSizeForPath(path);
        if (logical.isValid() && logical.width() > 1 && logical.height() > 1
            && !isProvisionalImageSize(path)) {
            item->setIntrinsicSize(logical);
        }
    }
    if (isImageMode() || m_items.size() == 1) {
        item->setItemScale(1.0);
        if (isImageMode()) {
            item->setPos(0, 0);
        }
        resetTransform();
        fitInView(item, mode);
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
    if (m_sessionTotal > 0 && m_sessionIndex >= 0 && m_sessionIndex < m_sessionTotal) {
        return tr("%1/%2").arg(m_sessionIndex + 1).arg(m_sessionTotal);
    }
    return {};
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
    if (!m_lastLoadError.isEmpty()) {
        return PagePath::displayName(m_lastLoadError);
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
    if (edge <= 0) {
        return tr("Loading…");
    }
    const QSize logical = logicalSizeForPath(item->path());
    const int native = isPositiveSize(logical) ? qMax(logical.width(), logical.height()) : 0;
    if (item->hasDecodedPixels() && native > 0
        && edge >= (native * 9) / 10) {
        return tr("Full resolution");
    }
    QString tier;
    if (item->hasDecodedPixels() && edge >= ThumtooCache::kBatchOverviewEdge) {
        tier = tr("High quality");
    } else if (edge >= ThumtooCache::kBatchOverviewEdge) {
        tier = tr("High quality");
    } else if (edge >= ThumtooCache::kGalleryLadderEdge) {
        tier = tr("Preview");
    } else if (edge >= ThumtooCache::kFilmstripLadderEdge) {
        tier = tr("Thumbnail");
    } else {
        tier = tr("Quick preview");
    }
    if (isGalleryMode()) {
        const int need = galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
        const auto it = m_gallerySoft.constFind(item->path());
        const int have = (it != m_gallerySoft.cend())
            ? qMax(it->have, edge)
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
        if (native > 0 && edge < (native * 9) / 10) {
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
    const QString src = ThumtooCache::lastPixelSourceLabel(item->path());
    if (!src.isEmpty()) {
        *text += tr(" · via %1").arg(src);
    }
    const char *dbg = std::getenv("THUMTOO_DEBUG");
    if (!dbg || !dbg[0] || dbg[0] == '0') {
        return;
    }
    const QString q = ThumtooCache::queueStatsLabel();
    if (!q.isEmpty()) {
        *text += tr(" · %1").arg(q);
    }
}

QString ImageView::statusTextEmpty() const
{
    if (!m_lastLoadError.isEmpty()) {
        return tr("Failed to load “%1”").arg(PagePath::displayName(m_lastLoadError));
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
    const int pending = pendingDecodeCount();
    if (pending > 0) {
        text += tr(" · Loading %1…").arg(pending);
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
    // User-visible activity while soft/PreferCache samples climb to native.
    if (!item || item->path().isEmpty()) {
        return {};
    }
    const QString path = item->path();
    const int have = item->displayPixelLongEdge();
    if (have <= 0) {
        return tr("Loading…");
    }
    if (sampleCoversNativeLogical(path, item->displayImage())) {
        return {};
    }
    if (m_pathRaster && m_pathRaster->isClimbPending(path)) {
        return m_pathRaster->isGaveUp(path) ? tr("Decoding full…")
                                            : tr("Improving quality…");
    }
    if (ThumtooCache::isAvailable()) {
        const int want = cappedDisplayEdgeForPath(path, imageModeOnScreenNeedEdge());
        if (ThumtooCache::isPixelsPending(path, want)
            || ThumtooCache::isPixelsPending(path, ThumtooCache::kGalleryLadderEdge)) {
            return tr("Improving quality…");
        }
    }
    // Soft on screen, climb may be queued but not yet marked inflight.
    if (!item->hasDecodedPixels()
        || !sampleCoversNativeLogical(path, item->displayImage())) {
        return tr("Improving quality…");
    }
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
    if (m_zoomRegionArmed || m_zoomRegionDragging) {
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

