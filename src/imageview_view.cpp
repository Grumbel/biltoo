// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include <cstdlib>
#include <cstdio>
#include "thumtoocache.h"
#include "imagecache.h"
#include "imageloader.h"
#include "imageloader.h"
#include "imagecache.h"
#include "archivepath.h"
#include "pagepath.h"
#include <QPixmap>
#include "imageitem.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QTimer>
#include <QPainter>
#include <QThreadPool>
#include <QMetaObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <QRubberBand>
#include <QDebug>
#include <QtMath>
#include <QVector>
#include <cmath>
#include "biltoo_logging.h"


/** Linear motion progress in [0,1]. Caller must size duration so the slideshow
 *  does not advance at progress==1 (see startSlideshowMotion).
 */
static qreal motionProgress01(qreal wallMs, qreal durationMs)
{
    if (durationMs <= 0.0) {
        return 0.0;
    }
    return qBound(0.0, wallMs / durationMs, 1.0);
}

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
    m_zoomBlurUnderlay[0] = QPixmap();
    m_zoomBlurUnderlay[1] = QPixmap();
    m_zoomBlurSourceKey[0] = 0;
    m_zoomBlurSourceKey[1] = 0;
    m_zoomBlurLastGood = QPixmap();
    m_zoomBlurLastGoodKey = 0;
    ++m_zoomBlurGeneration;
    m_zoomBlurInFlightGen[0] = m_zoomBlurInFlightGen[1] = 0;
    m_zoomBlurInFlightKey[0] = m_zoomBlurInFlightKey[1] = 0;
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
    m_zoomBlurUnderlay[0] = QPixmap();
    m_zoomBlurUnderlay[1] = QPixmap();
    m_zoomBlurSourceKey[0] = 0;
    m_zoomBlurSourceKey[1] = 0;
    m_zoomBlurLastGood = QPixmap();
    m_zoomBlurLastGoodKey = 0;
    ++m_zoomBlurGeneration;
    m_zoomBlurInFlightGen[0] = m_zoomBlurInFlightGen[1] = 0;
    m_zoomBlurInFlightKey[0] = m_zoomBlurInFlightKey[1] = 0;
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
        m_ssRasterByPath.clear();
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
                         m_motionBiasA, m_motionBiasB, 0);
    } else if (ImageItem *item = targetItem()) {
        // Still frame: draw source (or displayed pixmap) with cover/fit framing.
        const QImage src = item->hasDecodedPixels() ? item->sourceImage()
                                                    : item->pixmap().toImage();
        if (!src.isNull()) {
            paintMotionCover(&painter, src, 0.0, QPointF(0, 0), QPointF(0, 0), 0);
        }
    }
    painter.end();
    return pm;
}

void ImageView::prepareSlideshowTransition()
{
    cancelSlideshowTransition();
    // Snapshot transitions when dwell motion is off. With motion on, the host
    // uses beginLiveSlideshowTransition so the outgoing pan never freezes.
    if (m_slideshowTransition == SlideshowTransition::None
        || m_slideshowTransitionDurationMs <= 0
        || !isImageMode()
        || !viewport()) {
        return;
    }
    const QPixmap shot = captureSlideshowFrame();
    if (shot.isNull()) {
        return;
    }
    m_slideshowTransitionPixmap = shot;
    m_slideshowTransitionProgress = 0.0;
    m_slideshowTransitionPending = true;
    m_slideshowTransitionActive = false;
}

bool ImageView::isSlideshowTransitionBusy() const
{
    return m_liveTransitionActive || m_liveTransitionHold || m_liveTransitionAwaitingLoad
        || m_slideshowTransitionActive || m_slideshowTransitionPending;
}

void ImageView::cancelSlideshowTransition()
{
    m_slideshowTransitionPending = false;
    m_slideshowTransitionActive = false;
    m_slideshowTransitionProgress = 1.0;
    m_slideshowTransitionPixmap = QPixmap();
    m_slideshowTransitionToPixmap = QPixmap();
    m_slideshowTransitionFromPixmap = QPixmap();
    m_liveTransitionSourceImage = QImage();
    m_liveFromSourceImage = QImage();
    m_liveTransitionPathHash = 0;
    m_liveTransitionMotionProgress = 0.0;
    if (m_slideshowTransitionAnim) {
        m_slideshowTransitionAnim->stop();
    }
    m_liveTransitionActive = false;
    m_liveTransitionProgress = 0.0;
    m_liveTransitionMidAdvanced = false;
    m_liveTransitionHold = false;
    m_liveTransitionAwaitingLoad = false;
    m_liveTransitionElapsedBaseMs = 0;
    m_toLayerWallMs = -1.0;
    if (m_liveTransitionTimer) {
        m_liveTransitionTimer->stop();
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::releaseLiveTransitionHold()
{
    if (!m_liveTransitionHold && !m_liveTransitionAwaitingLoad) {
        // Fade-end already armed dwell in tickSlideshowMotion; LoadReplace only
        // updates the hidden underlay. Upgrade dwell if the canvas has better pixels.
        if (m_slideshowProgressActive) {
            if (ImageItem *item = targetItem()) {
                if (!item->sourceImage().isNull()) {
                    QImage next = item->sourceImage();
                    const int edge = slideshowTargetEdge();
                    if (qMax(next.width(), next.height()) > edge) {
                        next = next.scaled(edge, edge, Qt::KeepAspectRatio,
                                           Qt::FastTransformation);
                    }
                    const int nextLong = qMax(next.width(), next.height());
                    const int curLong = qMax(m_dwellSourceImage.width(),
                                             m_dwellSourceImage.height());
                    // Only rebuild atlas when long edge grows meaningfully.
                    if (m_dwellSourceImage.isNull()
                        || nextLong > curLong * 5 / 4) {
                        m_dwellSourceImage = next;
                        ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas,
                                          &m_dwellAtlasScale, &m_dwellAtlasVw,
                                          &m_dwellAtlasVh);
                        qCDebug(lcSlideshow).nospace()
                            << "[slideshow] dwell-upgrade "
                            << m_dwellSourceImage.width() << "x"
                            << m_dwellSourceImage.height()
                            << " path=" << QFileInfo(item->path()).fileName();
                    }
                    hideSlideshowUnderlay();
                }
            }
        }
        return;
    }
    if (m_liveTransitionAwaitingLoad
        && m_slideshowTransition == SlideshowTransition::FadeBlack
        && m_liveTransitionActive) {
        m_liveTransitionAwaitingLoad = false;
        m_liveTransitionElapsedBaseMs = m_liveTransitionDurationMs / 2;
        m_liveTransitionClock.restart();
        m_liveTransitionProgress = 0.5;
        if (viewport()) {
            viewport()->update();
        }
        return;
    }

    const QImage toSource = m_liveTransitionSourceImage;
    const QString nextPath = m_liveTransitionNextPath;

    // Biases for the incoming slide.
    m_motionBiasA = m_liveToBiasA;
    m_motionBiasB = m_liveToBiasB;
    m_motionBiasValid = true;
    m_motionBiasPath = nextPath.isEmpty()
        ? (targetItem() ? targetItem()->path() : QString())
        : nextPath;

    // Dwell buffer: canvas full decode if it matches the target, else fade to-frame.
    QImage dwell;
    if (ImageItem *item = targetItem()) {
        if (!item->sourceImage().isNull()
            && (nextPath.isEmpty() || item->path() == nextPath)) {
            dwell = item->sourceImage();
        }
    }
    if (dwell.isNull()) {
        dwell = toSource;
    }
    m_dwellSourceImage = dwell;
    m_dwellMotionT = 0.0;
    if (!dwell.isNull()) {
        m_dwellAtlas = m_liveToAtlas;
        m_dwellAtlasScale = m_liveToAtlasScale;
        m_dwellAtlasVw = m_liveToAtlasVw;
        m_dwellAtlasVh = m_liveToAtlasVh;
        ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas, &m_dwellAtlasScale,
                          &m_dwellAtlasVw, &m_dwellAtlasVh);
    }

    // Start path at 0 while hold still covers the viewport (no underlay gap).
    hideSlideshowUnderlay();
    if (m_slideshowProgressActive
        && m_slideshowMotion != SlideshowMotion::Off
        && m_slideshowProgressIntervalMs >= 250
        && !m_dwellSourceImage.isNull()) {
        startSlideshowMotion(m_slideshowProgressIntervalMs, 0.0);
        m_dwellSourceImage = dwell;
        if (!dwell.isNull()) {
            ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas, &m_dwellAtlasScale,
                              &m_dwellAtlasVw, &m_dwellAtlasVh);
        }
        m_dwellMotionT = 0.0;
        m_motionElapsedOffsetMs = 0;
        m_motionClock.start();
        m_slideshowMotionActive = true;
        qCDebug(lcSlideshow).nospace()
            << "[slideshow] dwell-start "
            << m_dwellSourceImage.width() << "x" << m_dwellSourceImage.height()
            << " path=" << QFileInfo(nextPath).fileName();
    }

    // Clear live composite only after dwell is armed.
    m_liveTransitionHold = false;
    m_liveTransitionAwaitingLoad = false;
    m_liveTransitionActive = false;
    m_liveTransitionProgress = 1.0;
    m_slideshowTransitionToPixmap = QPixmap();
    m_slideshowTransitionFromPixmap = QPixmap();
    m_liveFromSourceImage = QImage();
    m_liveTransitionSourceImage = QImage();
    m_liveFromAtlas = QPixmap();
    m_liveToAtlas = QPixmap();
    m_liveTransitionPathHash = 0;
    m_liveTransitionMotionProgress = 0.0;
    m_liveTransitionNextPath.clear();
    m_toLayerWallMs = -1.0;

    emit slideshowDwellResumeRequested();
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::startSlideshowTransitionAnimation()
{
    if (!m_slideshowTransitionPending || m_slideshowTransitionPixmap.isNull()) {
        m_slideshowTransitionPending = false;
        // Host may be waiting to re-arm the advance timer after goNext.
        emit slideshowDwellResumeRequested();
        return;
    }
    m_slideshowTransitionPending = false;

    m_slideshowTransitionToPixmap = QPixmap();
    if (m_slideshowTransition == SlideshowTransition::Slide && viewport()) {
        m_slideshowTransitionActive = false;
        m_slideshowTransitionProgress = 1.0;
        // New slide is already loaded into the scene; capture without GL grab.
        m_slideshowTransitionToPixmap = captureSlideshowFrame();
        // Projector slide is two static frames; pause dwell blit for the swap.
        cancelSlideshowMotion();
    }

    m_slideshowTransitionActive = true;
    m_slideshowTransitionProgress = 0.0;

    if (!m_slideshowTransitionAnim) {
        m_slideshowTransitionAnim = new QVariantAnimation(this);
        m_slideshowTransitionAnim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(m_slideshowTransitionAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) {
                    m_slideshowTransitionProgress = v.toReal();
                    if (viewport()) {
                        viewport()->update();
                    }
                });
        connect(m_slideshowTransitionAnim, &QVariantAnimation::finished, this, [this]() {
            m_slideshowTransitionActive = false;
            m_slideshowTransitionProgress = 1.0;
            m_slideshowTransitionPixmap = QPixmap();
            m_slideshowTransitionToPixmap = QPixmap();
            // Resume dwell blit after a static Slide swap.
            maybeStartSlideshowMotion();
            if (viewport()) {
                viewport()->update();
            }
            // Interval may equal transition duration — host must not arm the
            // advance timer until this finishes (avoids tick/transition races).
            emit slideshowDwellResumeRequested();
        });
    }
    m_slideshowTransitionAnim->stop();
    m_slideshowTransitionAnim->setStartValue(0.0);
    m_slideshowTransitionAnim->setEndValue(1.0);
    m_slideshowTransitionAnim->setDuration(m_slideshowTransitionDurationMs);
    m_slideshowTransitionAnim->start();
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
    // Explicit uniform scale for static slideshow framing.
    item->setItemShear(0.0);
    item->setItemRotation(0.0);
    item->setItemScale(1.0);
    if (isImageMode()) {
        item->setPos(0, 0);
    }
    const QRectF content = item->contentRect();
    if (content.width() < 1.0 || content.height() < 1.0) {
        return;
    }
    const qreal iw = content.width();
    const qreal ih = content.height();
    const qreal vw = qreal(qMax(1, viewport()->width()));
    const qreal vh = qreal(qMax(1, viewport()->height()));
    const QPointF mid = item->mapToScene(content.center());

    qreal scale = 1.0;
    switch (m_slideshowZoom) {
    case SlideshowZoom::Fill:
        m_fitMode = false;
        m_fillMode = true;
        scale = qMax(vw / iw, vh / ih);
        break;
    case SlideshowZoom::Actual:
        m_fitMode = false;
        m_fillMode = false;
        scale = 1.0;
        break;
    case SlideshowZoom::Fit:
    default:
        m_fitMode = true;
        m_fillMode = false;
        scale = qMin(vw / iw, vh / ih);
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
    // Never restart dwell under an in-flight live fade (underlay flash / stuck busy).
    if (m_liveTransitionActive || m_liveTransitionHold || m_liveTransitionAwaitingLoad) {
        return;
    }
    if (m_slideshowMotion != SlideshowMotion::Off) {
        // Restart dwell Ken Burns from the zoom base + current interval.
        int duration = m_slideshowProgressIntervalMs;
        if (duration < 250) {
            duration = 3000;
        }
        startSlideshowMotion(duration);
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
            m_motionElapsedOffsetMs += m_motionClock.elapsed();
            m_motionTimer->stop();
        }
        if (m_ssFromMotionClockRunning && m_ssFromMotionClock.isValid()) {
            m_ssFromMotionBaseMs += m_ssFromMotionClock.elapsed();
        }
        if (m_ssToMotionClockRunning && m_ssToMotionClock.isValid()) {
            m_ssToMotionBaseMs += m_ssToMotionClock.elapsed();
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
    m_dwellCoverPixmap = QPixmap();
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

void ImageView::pickInterestingMotionBiases(uint seed, const QImage &source)
{
    // Prefer content-aware centres (libvips attention) when the decoded frame
    // is available; otherwise path-hash corners/edges as before.
    if (seed == 0) {
        seed = 1;
    }

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
    if (haveAtt) {
        // Map normalized focus to bias space [-1, 1] (same as corner table).
        QPointF subject((att01.x() - 0.5) * 2.0, (att01.y() - 0.5) * 2.0);
        subject.setX(qBound(-1.0, subject.x(), 1.0));
        subject.setY(qBound(-1.0, subject.y(), 1.0));
        // Near-centre attention still needs travel — fall through to geometry.
        if (qAbs(subject.x()) > 0.12 || qAbs(subject.y()) > 0.12) {
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
            return;
        }
    }

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


void ImageView::putSlideshowRaster(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    const int incoming = qMax(image.width(), image.height());
    const auto it = m_ssRasterByPath.constFind(path);
    if (it != m_ssRasterByPath.cend() && !it->isNull()) {
        const int have = qMax(it->width(), it->height());
        // Keep the sharper buffer; equal size keeps the existing one.
        if (have >= incoming) {
            return;
        }
    }
    m_ssRasterByPath.insert(path, image);
    constexpr int kMaxSsRaster = 12;
    if (m_ssRasterByPath.size() <= kMaxSsRaster) {
        return;
    }
    // Evict oldest non-active paths (QHash order is arbitrary — drop any excess
    // that are not the current phase pair).
    QStringList drop;
    drop.reserve(m_ssRasterByPath.size() - kMaxSsRaster);
    for (auto it = m_ssRasterByPath.cbegin();
         it != m_ssRasterByPath.cend()
         && drop.size() < m_ssRasterByPath.size() - kMaxSsRaster; ++it) {
        if (it.key() != path && it.key() != m_ssFromPath && it.key() != m_ssToPath) {
            drop.append(it.key());
        }
    }
    for (const QString &k : drop) {
        m_ssRasterByPath.remove(k);
    }
}

QImage ImageView::slideshowRaster(const QString &path) const
{
    if (path.isEmpty()) {
        return QImage();
    }
    // Unoriented disk/preload pixels only — not item->sourceImage() (may already
    // have content orientation baked in).
    const auto it = m_ssRasterByPath.constFind(path);
    if (it != m_ssRasterByPath.cend() && !it->isNull()) {
        return *it;
    }
    return QImage();
}

QImage ImageView::slideshowFullIfReady(const QString &path) const
{
    return slideshowRaster(path);
}

QImage ImageView::slideshowSoftPlaceholder(const QString &path)
{
    if (path.isEmpty()) {
        return QImage();
    }
    // Already have something in the unified map.
    QImage have = slideshowRaster(path);
    if (!have.isNull()) {
        return have;
    }
    // Prefer target-edge cache, then any soft. Never upscale to native size.
    const int edge = slideshowTargetEdge();
    QImage soft = ImageCache::get(path, edge);
    if (soft.isNull()) {
        soft = ImageCache::get(path);
    }
    if (soft.isNull()) {
        return QImage();
    }
    if (qMax(soft.width(), soft.height()) > edge) {
        soft = soft.scaled(edge, edge, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    // Non-const put from const path: softPlaceholder is non-const method.
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
    if (raw.isNull() || path.isEmpty()) {
        return raw;
    }
    QImage oriented = imageWithSessionAppearance(raw, sessionIdForPath(path), path);
    return oriented.isNull() ? raw : oriented;
}

QImage ImageView::slideshowPixelsForPath(const QString &path)
{
    const int edge = slideshowTargetEdge();
    QImage img = slideshowRaster(path);
    if (img.isNull()) {
        img = slideshowSoftPlaceholder(path);
    }
    if (!img.isNull() && qMax(img.width(), img.height()) > edge) {
        img = img.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return orientSlideshowImage(img, path);
}

void ImageView::setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT)
{
    if (!m_slideshowProgressActive) {
        return;
    }

    const bool fromChanged = (fromPath != m_ssFromPath);
    const bool toChanged = (toPath != m_ssToPath);
    // Pure-phase clock ticks at 16ms and used to call us every frame even when
    // from/to were unchanged — each call hideSlideshowUnderlay()'d the whole
    // scene and forced a full viewport repaint (GUI appeared dead under load).
    if (!fromChanged && !toChanged) {
        if (qFuzzyCompare(fadeT, m_ssFadeT)
            || (fadeT < 0.0 && m_ssFadeT < 0.0)) {
            return;
        }
        // Fade progress only — keep buffers, just repaint.
        m_ssFadeT = fadeT;
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    // Do NOT bump ZoomBlur generation here — that cancelled the incoming
    // slide's underlay build every transition and caused letterbox flicker.
    // Evict only slots that are neither from nor to; in-flight jobs for the
    // new pair keep running.
    if (fromChanged || toChanged) {
        const int vw = m_zoomBlurVw > 0 ? m_zoomBlurVw
            : (viewport() ? viewport()->width() : 0);
        const int vh = m_zoomBlurVh > 0 ? m_zoomBlurVh
            : (viewport() ? viewport()->height() : 0);
        auto pathKey = [vw, vh](const QString &path) -> qint64 {
            if (path.isEmpty() || vw < 1 || vh < 1) {
                return 0;
            }
            return qint64(qHash(path)) ^ (qint64(vw) << 16) ^ qint64(vh);
        };
        const qint64 keepFrom = pathKey(fromPath);
        const qint64 keepTo = pathKey(toPath);
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
        if (m_zoomBlurLastGoodKey != 0
            && m_zoomBlurLastGoodKey != keepFrom
            && m_zoomBlurLastGoodKey != keepTo) {
            m_zoomBlurLastGood = QPixmap();
            m_zoomBlurLastGoodKey = 0;
        }
    }
    const int pathMs = qMax(250, m_slideshowProgressIntervalMs
                            + qMax(0, m_slideshowTransitionDurationMs));

    // Drop queued raster work on phase change — rapid ←/→ was draining the
    // pending list into work for slides already left behind.
    if (fromChanged) {
        m_ssRasterPending.clear();
    }

    // --- From (A) ---
    if (fromChanged) {
        // Spec: B moves through transition *and* its following interval.
        // When dwell becomes B after A+B fade, promote B's motion — do not restart at 0.
        const bool promoteB = (!fromPath.isEmpty() && fromPath == m_ssToPath
                               && m_ssToMotionClockRunning);
        m_ssFromPath = fromPath;
        if (promoteB) {
            m_ssFromImage = !m_ssToImage.isNull() ? m_ssToImage
                                                 : slideshowPixelsForPath(fromPath);
            m_motionBiasA = m_ssToBiasA;
            m_motionBiasB = m_ssToBiasB;
            m_motionBiasValid = true;
            m_motionBiasPath = fromPath;
            m_ssFromMotionClock = m_ssToMotionClock;
            m_ssFromMotionBaseMs = m_ssToMotionBaseMs;
            m_ssFromMotionClockRunning = true;
            m_ssFromMotionT = m_ssToMotionT;
            m_dwellMotionT = m_ssFromMotionT;
        } else {
            m_ssFromImage = slideshowPixelsForPath(fromPath);
            if (!fromPath.isEmpty()) {
                m_motionBiasValid = false;
                pickInterestingMotionBiases(qHash(fromPath), m_ssFromImage);
                m_motionBiasPath = fromPath;
            }
            m_ssFromMotionClock.start();
            m_ssFromMotionClockRunning = true;
            m_ssFromMotionBaseMs = 0;
            m_ssFromMotionT = 0.0;
            m_dwellMotionT = 0.0;
        }
        m_dwellSourceImage = m_ssFromImage;
        if (!m_ssFromImage.isNull()) {
            ensureMotionAtlas(m_ssFromImage, &m_dwellAtlas, &m_dwellAtlasScale,
                              &m_dwellAtlasVw, &m_dwellAtlasVh);
        }
        if (m_slideshowMotion != SlideshowMotion::Off && pathMs >= 250) {
            if (!m_motionTimer) {
                m_motionTimer = new QTimer(this);
                m_motionTimer->setTimerType(Qt::PreciseTimer);
                m_motionTimer->setInterval(16);
                connect(m_motionTimer, &QTimer::timeout, this, &ImageView::tickSlideshowMotion);
            }
            m_slideshowMotionActive = true;
            m_motionDurationMs = pathMs;
            // While the show is paused, arm motion state but do not run the timer.
            if (!m_slideshowMotionPaused) {
                m_motionTimer->start();
            }
        }
        qCDebug(lcSlideshow).nospace()
            << "[slideshow] phase-from "
            << QFileInfo(fromPath).fileName()
            << " " << m_ssFromImage.width() << "x" << m_ssFromImage.height()
            << (promoteB ? " (continue)" : " (start)");
    } else {
        // Upgrade only when a full buffer is ready — never re-scale soft every tick.
        const QImage full = slideshowFullIfReady(fromPath);
        if (!full.isNull()
            && (m_ssFromImage.isNull()
                || full.width() * full.height()
                    > m_ssFromImage.width() * m_ssFromImage.height())) {
            // full cache is unbaked disk pixels — orient for paint buffers.
            const QImage oriented = orientSlideshowImage(full, fromPath);
            m_ssFromImage = oriented;
            m_dwellSourceImage = oriented;
            ensureMotionAtlas(m_ssFromImage, &m_dwellAtlas, &m_dwellAtlasScale,
                              &m_dwellAtlasVw, &m_dwellAtlasVh);
        }
    }
    if (m_ssFromMotionClockRunning && pathMs > 0) {
        qint64 ms = m_ssFromMotionBaseMs;
        if (!m_slideshowMotionPaused && m_ssFromMotionClock.isValid()) {
            ms += m_ssFromMotionClock.elapsed();
        }
        m_ssFromMotionT = qBound(0.0, qreal(ms) / qreal(pathMs), 1.0);
        m_dwellMotionT = m_ssFromMotionT;
    }

    // --- To (B) ---
    if (toPath.isEmpty()) {
        m_ssToPath.clear();
        m_ssToImage = QImage();
        m_ssToMotionClockRunning = false;
        m_ssToMotionT = 0.0;
    } else if (toChanged) {
        m_ssToPath = toPath;
        m_ssToImage = slideshowPixelsForPath(toPath);
        {
            const QPointF saveA = m_motionBiasA;
            const QPointF saveB = m_motionBiasB;
            const bool saveV = m_motionBiasValid;
            m_motionBiasValid = false;
            pickInterestingMotionBiases(qHash(toPath), m_ssToImage);
            m_ssToBiasA = m_motionBiasA;
            m_ssToBiasB = m_motionBiasB;
            m_motionBiasA = saveA;
            m_motionBiasB = saveB;
            m_motionBiasValid = saveV;
        }
        m_ssToMotionClock.start();
        m_ssToMotionClockRunning = true;
        m_ssToMotionBaseMs = 0;
        m_ssToMotionT = 0.0;
        // Prefetch ZoomBlur underlay for B so the first transition frames
        // already have a matching key (avoids painting A's blur under B).
        if (!m_ssToImage.isNull() && viewport()) {
            const QSize vs = viewport()->size();
            if (vs.width() > 0 && vs.height() > 0) {
                const qint64 key = qint64(qHash(toPath))
                    ^ (qint64(vs.width()) << 16) ^ qint64(vs.height());
                scheduleZoomBlurBuild(m_ssToImage, vs.width(), vs.height(), key);
            }
        }
        qCDebug(lcSlideshow).nospace()
            << "[slideshow] phase-to "
            << QFileInfo(toPath).fileName()
            << " " << m_ssToImage.width() << "x" << m_ssToImage.height();
    } else {
        const QImage full = slideshowFullIfReady(toPath);
        if (!full.isNull()
            && (m_ssToImage.isNull()
                || full.width() * full.height()
                    > m_ssToImage.width() * m_ssToImage.height())) {
            m_ssToImage = orientSlideshowImage(full, toPath);
        }
    }
    if (m_ssToMotionClockRunning && pathMs > 0) {
        qint64 ms = m_ssToMotionBaseMs;
        if (!m_slideshowMotionPaused && m_ssToMotionClock.isValid()) {
            ms += m_ssToMotionClock.elapsed();
        }
        m_ssToMotionT = qBound(0.0, qreal(ms) / qreal(pathMs), 1.0);
    }

    m_ssFadeT = fadeT;

    if (fromChanged || toChanged) {
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
    }

    // Keep underlays warm for the active pair (dwell from, or both in fade).
    // Skip while user is key-repeating — builds fight soft decode.
    if (viewport() && !m_slideshowNavHot
        && m_slideshowLetterboxFill == SlideshowLetterboxFill::ZoomBlur) {
        const QSize vs = viewport()->size();
        if (vs.width() > 0 && vs.height() > 0) {
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
    }

    // Pure phase owns the composite — clear legacy live + snapshot flags so
    // residual Slide projector cards / animations cannot paint over us.
    m_liveTransitionActive = false;
    m_liveTransitionHold = false;
    m_liveTransitionAwaitingLoad = false;
    m_liveFromSourceImage = QImage();
    m_liveTransitionSourceImage = QImage();
    m_slideshowTransitionPending = false;
    m_slideshowTransitionActive = false;
    m_slideshowTransitionProgress = 1.0;
    if (m_slideshowTransitionAnim) {
        m_slideshowTransitionAnim->stop();
    }
    m_slideshowTransitionPixmap = QPixmap();
    m_slideshowTransitionToPixmap = QPixmap();
    m_slideshowTransitionFromPixmap = QPixmap();

    hideSlideshowUnderlay();
    if (viewport()) {
        viewport()->update();
    }
}

bool ImageView::beginLiveSlideshowTransition(const QString &nextPath)
{
    // CONTRACT: two FULL-RES images, both moving, opacity crossfade.
    // Never open the incoming frame on a thumbnail — thumb→full always jumps
    // because motion cover uses the image's real pixel size (travel thresholds
    // and atlas scale are not resolution-invariant in absolute pixels).
    if (m_slideshowMotion == SlideshowMotion::Off
        || m_slideshowTransition == SlideshowTransition::None
        || m_slideshowTransition == SlideshowTransition::Slide
        || m_slideshowTransitionDurationMs <= 0
        || !isImageMode()
        || !viewport()
        || nextPath.isEmpty()) {
        qCDebug(lcSlideshow) << "[slideshow] beginLive declined: mode/duration/path";
        return false;
    }

    cancelSlideshowTransition();
    m_liveTransitionNextPath = nextPath;
    m_liveTransitionMidAdvanced = false;

    // Outgoing: dwell cover or canvas full decode only (no cache thumb).
    m_liveFromSourceImage = QImage();
    m_liveFromMotionProgress0 = 0.0;
    m_liveFromBiasA = m_motionBiasA;
    m_liveFromBiasB = m_motionBiasB;
    if (!m_dwellSourceImage.isNull()) {
        m_liveFromSourceImage = m_dwellSourceImage;
    } else if (ImageItem *item = targetItem()) {
        m_liveFromSourceImage = item->sourceImage();
        if (m_liveFromSourceImage.isNull() && !item->pixmap().isNull()) {
            m_liveFromSourceImage = item->pixmap().toImage();
        }
    }
    if (m_slideshowMotionActive && m_motionDurationMs > 0) {
        m_liveFromMotionProgress0 = qBound(
            0.0,
            qreal(m_motionElapsedOffsetMs + m_motionClock.elapsed())
                / qreal(m_motionDurationMs),
            1.0);
    }

    if (m_liveFromSourceImage.isNull()) {
        qCDebug(lcSlideshow) << "[slideshow] beginLive declined: no from full pixels";
        return false;
    }

    m_liveTransitionAwaitingLoad = true;

    // Fast path: raster map already has pixels for next.
    {
        const QImage img = slideshowRaster(nextPath);
        if (!img.isNull()) {
            qCDebug(lcSlideshow).nospace()
                << "[slideshow] beginLive raster-hit "
                << QFileInfo(nextPath).fileName()
                << " " << img.width() << "x" << img.height()
                << " from=" << m_liveFromSourceImage.width() << "x"
                << m_liveFromSourceImage.height();
            const QPointer<ImageView> guard(this);
            QMetaObject::invokeMethod(this, [guard, img]() {
                if (guard) {
                    guard->startLiveTransitionWithImage(img);
                }
            }, Qt::QueuedConnection);
            return true;
        }
    }

    // Soft/cache hit is fine: camera is resolution-invariant. Never upscale to
    // native (that created multi-MP phase buffers). Climb via preload later.
    {
        QImage img = slideshowRaster(nextPath);
        if (img.isNull()) {
            img = ImageCache::get(nextPath, slideshowTargetEdge());
            if (img.isNull()) {
                img = ImageCache::get(nextPath);
            }
            if (!img.isNull()) {
                putSlideshowRaster(nextPath, img);
            }
        }
        if (!img.isNull()) {
            qCDebug(lcSlideshow).nospace()
                << "[slideshow] beginLive cache-hit "
                << QFileInfo(nextPath).fileName()
                << " " << img.width() << "x" << img.height()
                << " from=" << m_liveFromSourceImage.width() << "x"
                << m_liveFromSourceImage.height();
            const QPointer<ImageView> guard(this);
            QMetaObject::invokeMethod(this, [guard, img]() {
                if (guard) {
                    guard->startLiveTransitionWithImage(img);
                }
            }, Qt::QueuedConnection);
            preloadSlideshowImage(nextPath);
            return true;
        }
    }

    // Preload already decoding this path — stay awaiting; preload-ready will
    // start the fade (avoid a second full disk load of the same file).
    if (m_ssRasterInflight.contains(nextPath)) {
        qCDebug(lcSlideshow).nospace()
            << "[slideshow] beginLive wait-preload "
            << QFileInfo(nextPath).fileName();
        return true;
    }

    // Kick preload and wait. preload-ready starts the fade when done.
    qCDebug(lcSlideshow).nospace()
        << "[slideshow] beginLive request-preload "
        << QFileInfo(nextPath).fileName()
        << " from=" << m_liveFromSourceImage.width() << "x"
        << m_liveFromSourceImage.height();
    preloadSlideshowImage(nextPath);
    return true;
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
    // (2048), not native — enough for zoomed Ken Burns without full extract.
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

void ImageView::setSlideshowNavHot(bool hot)
{
    if (m_slideshowNavHot == hot) {
        return;
    }
    m_slideshowNavHot = hot;
    // Do NOT invalidateZoomBlurQueue here — keep the previous underlay until a
    // new key's blur is ready (solid flash on every ←/→ was the bug).
}

void ImageView::preloadSlideshowImage(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Already have target-edge (or better) pixels — nothing to do.
    {
        const QImage have = slideshowRaster(path);
        if (!have.isNull()) {
            const int need = slideshowTargetEdge() * 7 / 10;
            if (qMax(have.width(), have.height()) >= need) {
                return;
            }
        }
    }
    if (m_ssRasterInflight.contains(path)) {
        return;
    }
    constexpr int kMaxInflight = 1;
    if (m_ssRasterInflight.size() >= kMaxInflight) {
        if (!m_ssRasterPending.contains(path)) {
            m_ssRasterPending.append(path);
            while (m_ssRasterPending.size() > 1) {
                m_ssRasterPending.removeFirst();
            }
        }
        return;
    }

    m_ssRasterInflight.insert(path);
    const QString loadPath = path;
    const QPointer<ImageView> guard(this);
    const int targetEdge = slideshowTargetEdge();
    qCDebug(lcSlideshow).nospace()
        << "[slideshow] preload-start " << QFileInfo(loadPath).fileName()
        << " edge=" << targetEdge;

    QThreadPool::globalInstance()->start([guard, loadPath, targetEdge]() {
        // PreferCache / soft at target edge only — never full native extract.
        QImage img = ImageCache::get(loadPath, targetEdge);
        if (img.isNull()
            || qMax(img.width(), img.height()) < targetEdge * 7 / 10) {
            const QImage soft = ImageLoader::loadThumbnail(loadPath, targetEdge);
            if (!soft.isNull()
                && (img.isNull()
                    || qMax(soft.width(), soft.height())
                        > qMax(img.width(), img.height()))) {
                img = soft;
            }
        }
        if ((img.isNull()
             || qMax(img.width(), img.height()) < targetEdge * 7 / 10)
            && ThumtooCache::isAvailable()) {
            (void)ThumtooCache::scheduleDisplayPixels(loadPath, targetEdge);
            // One more cache look after request (may still miss — async).
            const QImage again = ImageCache::get(loadPath, targetEdge);
            if (!again.isNull()
                && (img.isNull()
                    || qMax(again.width(), again.height())
                        > qMax(img.width(), img.height()))) {
                img = again;
            }
        }
        if (!img.isNull() && qMax(img.width(), img.height()) > targetEdge) {
            img = img.scaled(targetEdge, targetEdge, Qt::KeepAspectRatio,
                             Qt::FastTransformation);
        }
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, loadPath, img]() {
            ImageView *view = guard.data();
            if (!view) {
                return;
            }
            view->m_ssRasterInflight.remove(loadPath);
            if (!img.isNull()) {
                view->putSlideshowRaster(loadPath, img);
                view->rememberImageSize(loadPath, img.size());
                qCDebug(lcSlideshow).nospace()
                    << "[slideshow] preload-ready "
                    << QFileInfo(loadPath).fileName()
                    << " " << img.width() << "x" << img.height();
                if (view->m_slideshowProgressActive) {
                    if (loadPath == view->m_ssFromPath) {
                        const QImage oriented =
                            view->orientSlideshowImage(img, loadPath);
                        view->m_ssFromImage = oriented;
                        view->m_dwellSourceImage = oriented;
                        view->ensureMotionAtlas(
                            oriented, &view->m_dwellAtlas,
                            &view->m_dwellAtlasScale, &view->m_dwellAtlasVw,
                            &view->m_dwellAtlasVh);
                    }
                    if (loadPath == view->m_ssToPath) {
                        view->m_ssToImage =
                            view->orientSlideshowImage(img, loadPath);
                    }
                    // Legacy live-transition path still consumes upgrades.
                    if (view->m_liveTransitionNextPath == loadPath
                        && (view->m_liveTransitionActive
                            || view->m_liveTransitionHold
                            || view->m_liveTransitionAwaitingLoad)) {
                        const QImage oriented =
                            view->orientSlideshowImage(img, loadPath);
                        view->m_liveTransitionSourceImage = oriented;
                        view->ensureMotionAtlas(
                            oriented, &view->m_liveToAtlas,
                            &view->m_liveToAtlasScale, &view->m_liveToAtlasVw,
                            &view->m_liveToAtlasVh);
                    }
                    if (view->viewport()) {
                        view->viewport()->update();
                    }
                }
            }
            while (!view->m_ssRasterPending.isEmpty()) {
                const QString next = view->m_ssRasterPending.takeFirst();
                if (view->slideshowRaster(next).isNull()
                    && !view->m_ssRasterInflight.contains(next)) {
                    view->preloadSlideshowImage(next);
                    break;
                }
            }
        }, Qt::QueuedConnection);
    });
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
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    // Atlas size is a function of the *viewport* and motion headroom only —
    // not of the source raster's pixel dimensions. Camera dest is aspect-based;
    // the atlas is just a sharp enough texture to sample under max zoom.
    const qreal head = slideshowMotionHeadroom();
    const int longCap = int(qCeil(qreal(qMax(vw, vh)) * head));
    // Key atlas by viewport + headroom, not image×maxScale (that made soft→sharp
    // rebuilds change texture scale and feel like a camera jump).
    const qreal keyScale = head;
    if (!atlas->isNull() && qFuzzyCompare(*atlasScale, keyScale)
        && *atlasVw == vw && *atlasVh == vh
        && atlas->width() >= longCap * 9 / 10) {
        // Still rebuild when source long edge grew enough to matter.
        const int have = qMax(atlas->width(), atlas->height());
        const int srcLong = qMax(image.width(), image.height());
        if (srcLong <= have * 5 / 4) {
            return;
        }
    }
    QImage scaled = image.scaled(longCap, longCap, Qt::KeepAspectRatio,
                                 m_slideshowProgressActive
                                     ? Qt::FastTransformation
                                     : Qt::SmoothTransformation);
    if (scaled.isNull()) {
        *atlas = QPixmap();
        *atlasScale = 0.0;
        return;
    }
    *atlas = QPixmap::fromImage(std::move(scaled));
    *atlasScale = keyScale;
    *atlasVw = vw;
    *atlasVh = vh;
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

void ImageView::invalidateZoomBlurQueue() const
{
    // Drop in-flight work so a page flip cannot leave a backlog of blur jobs.
    ++m_zoomBlurGeneration;
    m_zoomBlurInFlightGen[0] = m_zoomBlurInFlightGen[1] = 0;
    m_zoomBlurInFlightKey[0] = m_zoomBlurInFlightKey[1] = 0;
}

void ImageView::scheduleZoomBlurBuild(const QImage &image, int vw, int vh, qint64 key) const
{
    if (m_slideshowNavHot) {
        return;
    }
    if (image.isNull() || vw < 1 || vh < 1 || key == 0) {
        return;
    }
    // Already cached?
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurSourceKey[i] == key && !m_zoomBlurUnderlay[i].isNull()) {
            return;
        }
    }
    // Already building this key?
    for (int i = 0; i < 2; ++i) {
        if (m_zoomBlurInFlightGen[i] == m_zoomBlurGeneration
            && m_zoomBlurInFlightKey[i] == key) {
            return;
        }
    }
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
        return; // both slots busy with other keys; try again next frame
    }
    const quint64 gen = m_zoomBlurGeneration;
    m_zoomBlurInFlightGen[flightSlot] = gen;
    m_zoomBlurInFlightKey[flightSlot] = key;
    // Snapshot pixels for the worker (avoid touching GUI QImage after return).
    const QImage src = image.copy();
    const QPointer<ImageView> guard(const_cast<ImageView *>(this));
    QThreadPool::globalInstance()->start([guard, src, vw, vh, key, gen]() {
        const QImage blurred = makeZoomBlurCover(src, vw, vh);
        if (blurred.isNull()) {
            return;
        }
        ImageView *target = guard.data();
        if (!target) {
            return;
        }
        QMetaObject::invokeMethod(target, [guard, blurred, key, gen]() {
            ImageView *self = guard.data();
            if (!self) {
                return;
            }
            if (gen != self->m_zoomBlurGeneration) {
                return; // page flipped — discard
            }
            int slot = -1;
            for (int i = 0; i < 2; ++i) {
                if (self->m_zoomBlurSourceKey[i] == key) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                slot = self->m_zoomBlurUnderlay[0].isNull() ? 0 : 1;
            }
            self->m_zoomBlurUnderlay[slot] = QPixmap::fromImage(blurred);
            self->m_zoomBlurSourceKey[slot] = key;
            self->m_zoomBlurLastGood = self->m_zoomBlurUnderlay[slot];
            self->m_zoomBlurLastGoodKey = key;
            for (int i = 0; i < 2; ++i) {
                if (self->m_zoomBlurInFlightGen[i] == gen
                    && self->m_zoomBlurInFlightKey[i] == key) {
                    self->m_zoomBlurInFlightGen[i] = 0;
                    self->m_zoomBlurInFlightKey[i] = 0;
                }
            }
            if (self->viewport()) {
                self->viewport()->update();
            }
        }, Qt::QueuedConnection);
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

void ImageView::paintMotionCover(QPainter *painter, const QImage &image,

                                 qreal motionT, QPointF biasA, QPointF biasB,
                                 uint pathHash) const
{
    if (!painter || image.isNull() || !viewport()) {
        return;
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    // Camera is resolution-invariant (SLIDESHOW.md): dest/bias are functions of
    // aspect ratio only. Soft and sharp rasters with the same aspect share one
    // camera path; only sampling sharpness changes when better pixels arrive.
    const qreal rawW = qreal(image.width());
    const qreal rawH = qreal(image.height());
    if (rawW < 1.0 || rawH < 1.0) {
        return;
    }
    constexpr qreal kRefLong = 1000.0;
    qreal iw;
    qreal ih;
    if (rawW >= rawH) {
        iw = kRefLong;
        ih = kRefLong * (rawH / rawW);
    } else {
        ih = kRefLong;
        iw = kRefLong * (rawW / rawH);
    }

    motionT = qBound(0.0, motionT, 1.0);

    const qreal cover = qMax(qreal(vw) / iw, qreal(vh) / ih);
    const qreal fit = qMin(qreal(vw) / iw, qreal(vh) / ih);
    qreal base = cover;
    switch (m_slideshowZoom) {
    case SlideshowZoom::Fill:
        base = cover;
        break;
    case SlideshowZoom::Actual:
        base = 1.0;
        break;
    case SlideshowZoom::Fit:
    default:
        base = fit;
        break;
    }
    if (base <= 0.0 || !qIsFinite(base)) {
        return;
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
            uint seed = pathHash ? pathHash : 1u;
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

    // Prefer pre-scaled atlas (one Smooth scale per slide/resize). Fall back to
    // drawImage only if no atlas matches this source (should be rare).
    const QPixmap *atlas = nullptr;
    if (&image == &m_dwellSourceImage && !m_dwellAtlas.isNull()) {
        atlas = &m_dwellAtlas;
    } else if (&image == &m_liveFromSourceImage && !m_liveFromAtlas.isNull()) {
        atlas = &m_liveFromAtlas;
    } else if (&image == &m_liveTransitionSourceImage && !m_liveToAtlas.isNull()) {
        atlas = &m_liveToAtlas;
    }

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (atlas) {
        painter->drawPixmap(QRectF(destX, destY, dw, dh), *atlas, atlas->rect());
    } else {
        painter->drawImage(QRectF(destX, destY, dw, dh), image);
    }
}

QPixmap ImageView::renderMotionCoverPixmap(const QImage &image, qreal motionT,
                                           uint pathHash) const
{
    // Snapshot helper (Slide transition / rare paths). Prefer paintMotionCover
    // on the live viewport painter for the animated slideshow.
    if (image.isNull() || !viewport()) {
        return {};
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    QImage out(vw, vh, QImage::Format_ARGB32_Premultiplied);
    out.fill(slideshowPadColor());
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    paintMotionCover(&painter, image, motionT, m_motionBiasA, m_motionBiasB, pathHash);
    painter.end();
    return QPixmap::fromImage(out);
}

void ImageView::startLiveTransitionWithImage(const QImage &nextImage)
{
    if (!isImageMode() || !viewport()) {
        m_liveTransitionAwaitingLoad = false;
        emit slideshowLiveTransitionFinished();
        return;
    }
    if (nextImage.isNull()) {
        m_liveTransitionAwaitingLoad = false;
        emit slideshowLiveTransitionFinished();
        return;
    }
    // Preload/full handoff is usually unbaked disk pixels — apply *next path*
    // session content once here. Never use m_currentSessionId: that is still
    // the dwell ("from") image while the transition loads the next path, so
    // using it painted the incoming frame with the previous slide's rotation.
    {
        m_liveTransitionSourceImage =
            orientSlideshowImage(nextImage, m_liveTransitionNextPath);
    }
    if (m_liveTransitionSourceImage.isNull()) {
        m_liveTransitionSourceImage = nextImage;
    }
    m_liveTransitionPathHash = qHash(m_liveTransitionNextPath);

    // Incoming path biases only — never overwrite dwell m_motionBiasA/B.
    {
        const QPointF saveA = m_motionBiasA;
        const QPointF saveB = m_motionBiasB;
        const bool saveValid = m_motionBiasValid;
        pickInterestingMotionBiases(m_liveTransitionPathHash
                                        ? m_liveTransitionPathHash
                                        : 1u,
                                    nextImage);
        m_liveToBiasA = m_motionBiasA;
        m_liveToBiasB = m_motionBiasB;
        m_motionBiasA = saveA;
        m_motionBiasB = saveB;
        m_motionBiasValid = saveValid;
    }

    // From keeps the running dwell wall clock. To starts its own path at 0
    // (toLayerWallMs = wallMs ⇒ toT starts at 0). Soft-handoff then starts a
    // fresh path at 0 for the new slide — one progress domain per image.
    const qreal wallMs = (m_slideshowMotionActive && m_motionDurationMs > 0)
        ? qreal(m_motionElapsedOffsetMs + m_motionClock.elapsed())
        : 0.0;
    m_toLayerWallMs = wallMs;
    m_liveFromMotionProgress0 = (m_motionDurationMs > 0)
        ? motionProgress01(wallMs, qreal(m_motionDurationMs))
        : 0.0;

    // Frames are drawn in paintEvent via atlas-backed paintMotionCover.
    m_dwellMotionT = m_liveFromMotionProgress0;
    m_liveTransitionMotionProgress = 0.0;
    ensureMotionAtlas(m_liveFromSourceImage, &m_liveFromAtlas, &m_liveFromAtlasScale,
                      &m_liveFromAtlasVw, &m_liveFromAtlasVh);
    ensureMotionAtlas(m_liveTransitionSourceImage, &m_liveToAtlas, &m_liveToAtlasScale,
                      &m_liveToAtlasVw, &m_liveToAtlasVh);
    setSlideshowUnderlayVisible(false);

    // Opacity timeline only. Motion stays on m_motionTimer.
    m_liveTransitionActive = true;
    m_liveTransitionHold = false;
    m_liveTransitionAwaitingLoad = false;
    m_liveTransitionMidAdvanced = false;
    m_liveTransitionElapsedBaseMs = 0;
    m_liveTransitionProgress = 0.0;
    m_liveTransitionMotionProgress = 0.0;
    m_liveTransitionDurationMs = m_slideshowTransitionDurationMs;
    m_liveTransitionClock.start();

    // Ensure the single motion timer is running (must already be for dwell).
    if (!m_slideshowMotionActive && m_slideshowProgressIntervalMs >= 250) {
        // Degenerate: start motion if somehow inactive.
        m_slideshowMotionActive = true;
        m_motionDurationMs = m_slideshowProgressIntervalMs;
        m_motionClock.start();
        m_motionElapsedOffsetMs = 0;
        if (!m_motionTimer) {
            m_motionTimer = new QTimer(this);
            m_motionTimer->setTimerType(Qt::PreciseTimer);
            m_motionTimer->setInterval(16);
            connect(m_motionTimer, &QTimer::timeout, this, &ImageView::tickSlideshowMotion);
        }
        m_motionTimer->start();
    }
    viewport()->update();
}

void ImageView::tickLiveTransition()
{
    // Obsolete: opacity + dual-frame sampling live in tickSlideshowMotion
    // so the motion clock can never be stopped by a transition.
}

void ImageView::maybeStartSlideshowMotion()
{
    if (m_slideshowMotion == SlideshowMotion::Off || !m_slideshowProgressActive
        || !isImageMode()) {
        return;
    }
    // Live transition owns motion via the single motion timer + soft handoff
    // in releaseLiveTransitionHold. startSlideshowMotion() cancels the timer —
    // calling it here would stop motion mid-crossfade.
    if (m_liveTransitionActive || m_liveTransitionHold || m_liveTransitionAwaitingLoad) {
        return;
    }
    // Already running — do not restart from 0 (that caused dwellT 0.75→0.01
    // after every live soft-handoff when a late maybeStart fired).
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

    // Ken Burns moves the *image* via blit, not the QGraphicsView camera.
    // Prefer path-oriented slideshow pixels (unbaked cache + appearance). Item
    // source may lag durable orientation on the first frame before a full
    // install, or be unbaked soft-only.
    {
        const QString path = item->path();
        QImage dwell = slideshowPixelsForPath(path);
        if (dwell.isNull()) {
            dwell = orientSlideshowImage(item->sourceImage(), path);
        }
        m_dwellSourceImage = dwell;
    }
    if (m_dwellSourceImage.isNull()) {
        return;
    }
    // Align underlay camera to slideshow zoom before hiding it so cancel/stop
    // can restore a known static frame.
    applySlideshowZoomFraming(item);
    ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas, &m_dwellAtlasScale,
                      &m_dwellAtlasVw, &m_dwellAtlasVh);
    setSlideshowUnderlayVisible(false);
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

    m_fitMode = false;
    m_fillMode = (m_slideshowZoom == SlideshowZoom::Fill);
    item->setItemShear(0.0);
    item->setItemRotation(0.0);
    item->setItemScale(1.0);
    if (isImageMode()) {
        item->setPos(0, 0);
    }

    const QString path = item->path();
    // Biases are per-image. Manual next/prev (and any LoadReplace) must not keep
    // the previous slide's A/B — that made the first post-nav transition glitch.
    // Live handoff installs to-path biases + path before calling here.
    if (m_motionBiasValid && m_motionBiasPath != path
        && !(m_liveTransitionHold || m_liveTransitionActive)) {
        m_motionBiasValid = false;
    }
    if (!m_motionBiasValid
        && !(m_liveTransitionHold || m_liveTransitionActive)) {
        const QImage src = item ? item->sourceImage() : QImage();
        pickInterestingMotionBiases(qHash(path), src);
        m_motionBiasPath = path;
    } else if (m_motionBiasValid && m_motionBiasPath.isEmpty()) {
        m_motionBiasPath = path;
    }
    // Biases + slideshow zoom are read by renderMotionCoverPixmap.

    if (!m_motionTimer) {
        m_motionTimer = new QTimer(this);
        m_motionTimer->setTimerType(Qt::PreciseTimer);
        m_motionTimer->setInterval(16);
        connect(m_motionTimer, &QTimer::timeout, this, &ImageView::tickSlideshowMotion);
    }
    m_slideshowMotionActive = true;
    // Path lasts longer than the dwell interval so that when the slideshow
    // advances (and crossfade runs), from-progress is still < 1 and keeps
    // lerping. Previously duration==interval → progress clamped at 1 for the
    // entire transition → motion looked frozen.
    const int pathMs = durationMs + qMax(0, m_slideshowTransitionDurationMs);
    m_motionDurationMs = qMax(durationMs, pathMs);
    initialProgress = qBound(0.0, initialProgress, 1.0);
    m_motionClock.start();
    m_motionElapsedOffsetMs = (initialProgress > 0.0 && m_motionDurationMs > 0)
        ? qint64(initialProgress * qreal(m_motionDurationMs))
        : 0;
    m_dwellCoverPixmap = QPixmap(); // painted live via paintMotionCover
    m_motionTimer->start();
    if (viewport()) {
        viewport()->update();
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

    // Pure-phase path: advance A/B motion from their own clocks (spec: both
    // move during transition; B moves through transition + its interval).
    if (m_slideshowProgressActive && (m_ssFromMotionClockRunning || m_ssToMotionClockRunning)) {
        const int pathMs = qMax(250, m_slideshowProgressIntervalMs
                                + qMax(0, m_slideshowTransitionDurationMs));
        if (m_ssFromMotionClockRunning) {
            qint64 ms = m_ssFromMotionBaseMs;
            if (!m_slideshowMotionPaused && m_ssFromMotionClock.isValid()) {
                ms += m_ssFromMotionClock.elapsed();
            }
            m_ssFromMotionT = qBound(0.0, qreal(ms) / qreal(pathMs), 1.0);
            m_dwellMotionT = m_ssFromMotionT;
        }
        if (m_ssToMotionClockRunning) {
            qint64 ms = m_ssToMotionBaseMs;
            if (!m_slideshowMotionPaused && m_ssToMotionClock.isValid()) {
                ms += m_ssToMotionClock.elapsed();
            }
            m_ssToMotionT = qBound(0.0, qreal(ms) / qreal(pathMs), 1.0);
        }
        // Upgrade only from full buffers (preload) — orient unbaked disk pixels.
        if (!m_ssFromPath.isEmpty()) {
            const QImage full = slideshowFullIfReady(m_ssFromPath);
            if (!full.isNull()
                && (m_ssFromImage.isNull()
                    || full.width() * full.height()
                        > m_ssFromImage.width() * m_ssFromImage.height())) {
                const QImage oriented = orientSlideshowImage(full, m_ssFromPath);
                m_ssFromImage = oriented;
                m_dwellSourceImage = oriented;
            }
        }
        if (!m_ssToPath.isEmpty()) {
            const QImage full = slideshowFullIfReady(m_ssToPath);
            if (!full.isNull()
                && (m_ssToImage.isNull()
                    || full.width() * full.height()
                        > m_ssToImage.width() * m_ssToImage.height())) {
                m_ssToImage = orientSlideshowImage(full, m_ssToPath);
            }
        }
        viewport()->update();
        return;
    }

    if (m_motionDurationMs <= 0) {
        return;
    }

    const qreal wallMs = qreal(m_motionElapsedOffsetMs + m_motionClock.elapsed());
    const qreal dwellT = motionProgress01(wallMs, qreal(m_motionDurationMs));
    m_dwellMotionT = dwellT;
    if (!m_dwellSourceImage.isNull()) {
        ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas, &m_dwellAtlasScale,
                          &m_dwellAtlasVw, &m_dwellAtlasVh);
    }
    if (m_liveTransitionActive || m_liveTransitionHold || m_liveTransitionAwaitingLoad) {
        if (!m_liveFromSourceImage.isNull()) {
            ensureMotionAtlas(m_liveFromSourceImage, &m_liveFromAtlas,
                              &m_liveFromAtlasScale, &m_liveFromAtlasVw, &m_liveFromAtlasVh);
        }
        if (!m_liveTransitionSourceImage.isNull()) {
            ensureMotionAtlas(m_liveTransitionSourceImage, &m_liveToAtlas,
                              &m_liveToAtlasScale, &m_liveToAtlasVw, &m_liveToAtlasVh);
        }
    }

    const bool inLive = m_liveTransitionActive || m_liveTransitionHold
        || m_liveTransitionAwaitingLoad;

    if (inLive) {
        // Progress only — paintMotionCover draws sources on the GL viewport.
        qreal toT = 0.0;
        if (m_toLayerWallMs >= 0.0 && m_motionDurationMs > 0) {
            toT = motionProgress01(wallMs - m_toLayerWallMs, qreal(m_motionDurationMs));
        }
        m_liveTransitionMotionProgress = toT;

        // --- opacity / veil timeline ---
        if (m_liveTransitionAwaitingLoad) {
            m_liveTransitionProgress = 0.5;
        } else if (m_liveTransitionHold) {
            m_liveTransitionProgress = 1.0;
        } else if (m_liveTransitionActive) {
            const qreal elapsed = qreal(m_liveTransitionElapsedBaseMs
                                        + m_liveTransitionClock.elapsed());
            const qreal t = m_liveTransitionDurationMs > 0
                ? elapsed / qreal(m_liveTransitionDurationMs)
                : 1.0;
            if (m_slideshowTransition == SlideshowTransition::FadeBlack
                && !m_liveTransitionMidAdvanced && t >= 0.5) {
                m_liveTransitionMidAdvanced = true;
                m_liveTransitionAwaitingLoad = true;
                m_liveTransitionProgress = 0.5;
                emit slideshowLiveTransitionFinished();
            } else if (t >= 1.0) {
                m_liveTransitionProgress = 1.0;
                if (!m_liveTransitionHold) {
                    m_liveTransitionHold = true;
                    // Arm dwell HERE from the to-frame at progress 0. Do not wait
                    // for LoadReplace → onImageLoaded → releaseLiveTransitionHold:
                    // that path was often skipped or late, so dwellT never reset
                    // (logs: 0.66 → 0.70 → 1.0 frozen) and the dwell buffer kept
                    // the previous image across orientation changes.
                    {
                        const QImage toFrame = m_liveTransitionSourceImage;
                        m_motionBiasA = m_liveToBiasA;
                        m_motionBiasB = m_liveToBiasB;
                        m_motionBiasValid = true;
                        m_motionBiasPath = m_liveTransitionNextPath;
                        if (!toFrame.isNull()) {
                            m_dwellSourceImage = toFrame;
                            m_dwellAtlas = m_liveToAtlas;
                            m_dwellAtlasScale = m_liveToAtlasScale;
                            m_dwellAtlasVw = m_liveToAtlasVw;
                            m_dwellAtlasVh = m_liveToAtlasVh;
                            ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas,
                                              &m_dwellAtlasScale, &m_dwellAtlasVw,
                                              &m_dwellAtlasVh);
                        }
                        m_dwellMotionT = 0.0;
                        hideSlideshowUnderlay();
                        if (m_slideshowProgressActive
                            && m_slideshowMotion != SlideshowMotion::Off
                            && m_slideshowProgressIntervalMs >= 250
                            && !m_dwellSourceImage.isNull()) {
                            startSlideshowMotion(m_slideshowProgressIntervalMs, 0.0);
                            if (!toFrame.isNull()) {
                                m_dwellSourceImage = toFrame;
                                ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas,
                                                  &m_dwellAtlasScale, &m_dwellAtlasVw,
                                                  &m_dwellAtlasVh);
                            }
                            m_dwellMotionT = 0.0;
                            m_motionElapsedOffsetMs = 0;
                            m_motionClock.start();
                            m_slideshowMotionActive = true;
                        }
                        qCDebug(lcSlideshow).nospace()
                            << "[slideshow] dwell-start "
                            << m_dwellSourceImage.width() << "x"
                            << m_dwellSourceImage.height()
                            << " path="
                            << QFileInfo(m_liveTransitionNextPath).fileName()
                            << " (at fade-end)";
                        // Live composite done — dwell owns the viewport.
                        m_liveTransitionActive = false;
                        m_liveTransitionHold = false;
                        m_liveFromSourceImage = QImage();
                        m_liveTransitionSourceImage = QImage();
                        m_liveFromAtlas = QPixmap();
                        m_liveToAtlas = QPixmap();
                        m_liveTransitionMotionProgress = 0.0;
                        m_toLayerWallMs = -1.0;
                    }
                    if (m_slideshowTransition != SlideshowTransition::FadeBlack) {
                        emit slideshowLiveTransitionFinished();
                    } else {
                        releaseLiveTransitionHold();
                    }
                }
            } else {
                m_liveTransitionProgress = t;
            }
        }
    } else {
        // Pure dwell: paint path samples m_dwellSourceImage at wall progress.
    }

    viewport()->update();
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
    if (item->hasDecodedPixels()) {
        return tr("Full resolution");
    }
    const int edge = item->displayPixelLongEdge();
    if (edge <= 0) {
        return tr("Loading…");
    }
    QString tier;
    if (edge >= ThumtooCache::kBatchOverviewEdge) {
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
    }
    return tier;
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

    const QString quality = pixelQualityLabel(item);
    const int edge = item->displayPixelLongEdge();
    const QSize native = item->imageSize();

    if (isMultiItemMode()) {
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
        if (const char *dbg = std::getenv("THUMTOO_DEBUG");
            dbg && dbg[0] && dbg[0] != '0') {
            const QString q = ThumtooCache::queueStatsLabel();
            if (!q.isEmpty()) {
                text += tr(" · %1").arg(q);
            }
            const QString src = ThumtooCache::lastPixelSourceLabel(item->path());
            if (!src.isEmpty()) {
                text += tr(" · via %1").arg(src);
            }
        }
        return text;
    }

    // Image mode
    QString text = tr("%1×%2 · Zoom %3%")
                       .arg(native.width())
                       .arg(native.height())
                       .arg(qRound(viewScale() * 100));
    if (!quality.isEmpty()) {
        if (edge > 0 && !item->hasDecodedPixels()) {
            text += tr(" · %1 (%2px)").arg(quality).arg(edge);
        } else {
            text += tr(" · %1").arg(quality);
        }
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
    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] && dbg[0] != '0') {
        const QString q = ThumtooCache::queueStatsLabel();
        if (!q.isEmpty()) {
            text += tr(" · %1").arg(q);
        }
        const QString src = ThumtooCache::lastPixelSourceLabel(item->path());
        if (!src.isEmpty()) {
            text += tr(" · via %1").arg(src);
        }
    }
    return text;
}
