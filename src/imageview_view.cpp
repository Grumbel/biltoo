// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imagecache.h"
#include "imageloader.h"
#include "imageloader.h"
#include "imagecache.h"
#include "archivepath.h"
#include <QPixmap>
#include "imageitem.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QTimer>
#include <QPainter>
#include <QThreadPool>
#include <QPointer>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <QRubberBand>
#include <QDebug>
#include <QtMath>


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
    if (m_bgColor.isValid()) {
        return m_bgColor;
    }
    const QBrush b = backgroundBrush();
    if (b.style() != Qt::NoBrush && b.color().isValid()) {
        return b.color();
    }
    return QColor(42, 42, 42);
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
    emit statusChanged();
    if (m_hudVisible || m_hudFlashVisible || m_slideshowPausedHud) {
        viewport()->update();
    }
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

void ImageView::zoomReset()
{
    m_fitMode = false;
    m_fillMode = false;
    if (isMultiItemMode()) {
        resetTransform();
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
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
                      Qt::KeepAspectRatio);
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        fitItem(item, Qt::KeepAspectRatio);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
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
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
                      Qt::KeepAspectRatioByExpanding);
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        fitItem(item, Qt::KeepAspectRatioByExpanding);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatioByExpanding);
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
        m_slideshowProgressElapsed.start();
        if (m_hudVisible && m_slideshowProgressIntervalMs > 0 && m_slideshowProgressTimer) {
            m_slideshowProgressTimer->start();
        } else if (m_slideshowProgressTimer) {
            m_slideshowProgressTimer->stop();
        }
    } else if (m_slideshowProgressTimer) {
        m_slideshowProgressTimer->stop();
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
        m_ssSoftByPath.clear();
        m_ssFullByPath.clear();
    }
    viewport()->update();
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
    m_slideshowTransitionDurationMs = qBound(0, ms, 5000);
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
                if (!item->sourceImage().isNull()
                    && (m_dwellSourceImage.isNull()
                        || (item->sourceImage().width() * item->sourceImage().height()
                            > m_dwellSourceImage.width() * m_dwellSourceImage.height()))) {
                    m_dwellSourceImage = item->sourceImage();
                    ensureMotionAtlas(m_dwellSourceImage, &m_dwellAtlas, &m_dwellAtlasScale,
                                      &m_dwellAtlasVw, &m_dwellAtlasVh);
                    hideSlideshowUnderlay();
                    qDebug().nospace()
                        << "[slideshow] dwell-upgrade "
                        << m_dwellSourceImage.width() << "x"
                        << m_dwellSourceImage.height()
                        << " path=" << QFileInfo(item->path()).fileName();
                } else {
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
        qDebug().nospace()
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
        m_slideshowMotionPaused = true;
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    m_slideshowMotionPaused = false;
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
    if (!source.isNull() && ImageLoader::attentionPoint(source, &att01)) {
        // Map normalized focus to bias space [-1, 1] (same as corner table).
        QPointF subject((att01.x() - 0.5) * 2.0, (att01.y() - 0.5) * 2.0);
        subject.setX(qBound(-1.0, subject.x(), 1.0));
        subject.setY(qBound(-1.0, subject.y(), 1.0));
        // Near-centre attention still needs travel — fall through to geometry.
        if (qAbs(subject.x()) > 0.12 || qAbs(subject.y()) > 0.12) {
            const QPointF opposite(-subject.x() * 0.65, -subject.y() * 0.65);
            // Odd seed: start on subject and ease away; even: travel toward it.
            if (seed & 1u) {
                m_motionBiasA = subject;
                m_motionBiasB = opposite;
            } else {
                m_motionBiasA = opposite;
                m_motionBiasB = subject;
            }
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


QImage ImageView::slideshowFullIfReady(const QString &path) const
{
    if (path.isEmpty()) {
        return QImage();
    }
    const auto fullIt = m_ssFullByPath.constFind(path);
    if (fullIt != m_ssFullByPath.cend() && !fullIt->isNull()) {
        return *fullIt;
    }
    if (path == m_preloadPath && !m_preloadImage.isNull()) {
        return m_preloadImage;
    }
    if (path == m_handoffPath && !m_handoffImage.isNull()) {
        return m_handoffImage;
    }
    for (ImageItem *item : m_items) {
        if (item && item->path() == path && !item->sourceImage().isNull()) {
            return item->sourceImage();
        }
    }
    return QImage();
}

QImage ImageView::slideshowSoftPlaceholder(const QString &path)
{
    if (path.isEmpty()) {
        return QImage();
    }
    const auto softIt = m_ssSoftByPath.constFind(path);
    if (softIt != m_ssSoftByPath.cend() && !softIt->isNull()) {
        return *softIt;
    }
    const QImage cached = ImageCache::get(path);
    if (cached.isNull()) {
        return QImage();
    }
    QSize fullSz;
    const auto it = m_imageSizeByPath.constFind(path);
    if (it != m_imageSizeByPath.cend() && it->isValid() && it->width() > 0) {
        fullSz = *it;
    } else {
        fullSz = ImageLoader::probeSize(path);
        if (fullSz.isValid() && fullSz.width() > 0) {
            rememberImageSize(path, fullSz);
        }
    }
    QImage soft = cached;
    if (fullSz.isValid() && fullSz.width() > 0
        && (soft.width() != fullSz.width() || soft.height() != fullSz.height())) {
        soft = soft.scaled(fullSz, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    m_ssSoftByPath.insert(path, soft);
    return soft;
}

QImage ImageView::slideshowPixelsForPath(const QString &path)
{
    const QImage full = slideshowFullIfReady(path);
    if (!full.isNull()) {
        return full;
    }
    return slideshowSoftPlaceholder(path);
}

void ImageView::setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT)
{
    if (!m_slideshowProgressActive) {
        return;
    }

    const bool fromChanged = (fromPath != m_ssFromPath);
    const bool toChanged = (toPath != m_ssToPath);
    const int pathMs = qMax(250, m_slideshowProgressIntervalMs
                            + qMax(0, m_slideshowTransitionDurationMs));

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
        qDebug().nospace()
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
            m_ssFromImage = full;
            m_dwellSourceImage = full;
            ensureMotionAtlas(m_ssFromImage, &m_dwellAtlas, &m_dwellAtlasScale,
                              &m_dwellAtlasVw, &m_dwellAtlasVh);
        }
    }
    if (m_ssFromMotionClockRunning && pathMs > 0) {
        m_ssFromMotionT = qBound(0.0, qreal(m_ssFromMotionClock.elapsed()) / qreal(pathMs), 1.0);
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
        m_ssToMotionT = 0.0;
        qDebug().nospace()
            << "[slideshow] phase-to "
            << QFileInfo(toPath).fileName()
            << " " << m_ssToImage.width() << "x" << m_ssToImage.height();
    } else {
        const QImage full = slideshowFullIfReady(toPath);
        if (!full.isNull()
            && (m_ssToImage.isNull()
                || full.width() * full.height()
                    > m_ssToImage.width() * m_ssToImage.height())) {
            m_ssToImage = full;
        }
    }
    if (m_ssToMotionClockRunning && pathMs > 0) {
        m_ssToMotionT = qBound(0.0, qreal(m_ssToMotionClock.elapsed()) / qreal(pathMs), 1.0);
    }

    m_ssFadeT = fadeT;

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
        qDebug() << "[slideshow] beginLive declined: mode/duration/path";
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
        qDebug() << "[slideshow] beginLive declined: no from full pixels";
        return false;
    }

    m_liveTransitionAwaitingLoad = true;

    // Fast path: full decode already preloaded.
    if (m_preloadPath == nextPath && !m_preloadImage.isNull()) {
        const QImage img = m_preloadImage;
        m_preloadPath.clear();
        m_preloadImage = QImage();
        m_handoffPath = nextPath;
        m_handoffImage = img;
        qDebug().nospace()
            << "[slideshow] beginLive preload-hit "
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

    // Shared preview cache (warmed at slideshow start). Paint the transition
    // immediately so superfast / interval≈transition does not stall waiting for
    // a full multi-megapixel decode. Do NOT write m_handoff* — handoff must
    // remain full-res only so LoadReplace is not locked onto a thumbnail.
    //
    // Scale the preview up to the path's native full pixel size so motion-cover
    // cover scale and travel thresholds match the full-res frame. Without that,
    // a 512px "to" against a multi-MP "from" jumps when the real decode lands.
    {
        const QImage cached = ImageCache::get(nextPath);
        if (!cached.isNull()) {
            QSize fullSz;
            const auto it = m_imageSizeByPath.constFind(nextPath);
            if (it != m_imageSizeByPath.cend() && it->isValid()
                && it->width() > 0 && it->height() > 0) {
                fullSz = *it;
            } else {
                fullSz = ImageLoader::probeSize(nextPath);
                if (fullSz.isValid() && fullSz.width() > 0 && fullSz.height() > 0) {
                    rememberImageSize(nextPath, fullSz);
                }
            }
            QImage img = cached;
            if (fullSz.isValid() && fullSz.width() > 0 && fullSz.height() > 0
                && (img.width() != fullSz.width() || img.height() != fullSz.height())) {
                img = img.scaled(fullSz, Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
            }
            qDebug().nospace()
                << "[slideshow] beginLive cache-hit "
                << QFileInfo(nextPath).fileName()
                << " cache=" << cached.width() << "x" << cached.height()
                << " paint=" << img.width() << "x" << img.height()
                << " from=" << m_liveFromSourceImage.width() << "x"
                << m_liveFromSourceImage.height();
            const QPointer<ImageView> guard(this);
            QMetaObject::invokeMethod(this, [guard, img]() {
                if (guard) {
                    guard->startLiveTransitionWithImage(img);
                }
            }, Qt::QueuedConnection);
            // Still warm a full decode for later cycles / post-transition load.
            if (m_preloadPath != nextPath && m_preloadInFlightPath != nextPath) {
                preloadSlideshowImage(nextPath);
            }
            return true;
        }
    }

    // Preload already decoding this path — stay awaiting; preload-ready will
    // start the fade (avoid a second full disk load of the same file).
    if (m_preloadInFlightPath == nextPath) {
        qDebug().nospace()
            << "[slideshow] beginLive wait-preload "
            << QFileInfo(nextPath).fileName();
        return true;
    }

    // Kick preload and wait. preload-ready starts the fade when done.
    qDebug().nospace()
        << "[slideshow] beginLive request-preload "
        << QFileInfo(nextPath).fileName()
        << " from=" << m_liveFromSourceImage.width() << "x"
        << m_liveFromSourceImage.height();
    preloadSlideshowImage(nextPath);
    return true;
}

void ImageView::preloadSlideshowImage(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // Already have full pixels for this path (retained map or live slots).
    if (m_ssFullByPath.contains(path) && !m_ssFullByPath.value(path).isNull()) {
        return;
    }
    if (path == m_preloadPath && !m_preloadImage.isNull()) {
        m_ssFullByPath.insert(path, m_preloadImage);
        return;
    }
    if (path == m_handoffPath && !m_handoffImage.isNull()) {
        m_ssFullByPath.insert(path, m_handoffImage);
        return;
    }
    if (path == m_preloadInFlightPath) {
        return;
    }
    // Promote any ready preload of another path into the full map so we can
    // start decoding @p path without discarding those pixels.
    if (!m_preloadPath.isEmpty() && !m_preloadImage.isNull() && m_preloadPath != path) {
        m_ssFullByPath.insert(m_preloadPath, m_preloadImage);
        m_preloadPath.clear();
        m_preloadImage = QImage();
    }
    // One in-flight decode at a time; do not cancel a different path mid-load.
    if (!m_preloadInFlightPath.isEmpty() && m_preloadInFlightPath != path) {
        return;
    }
    const quint64 gen = ++m_preloadGeneration;
    if (m_preloadPath == path) {
        m_preloadPath.clear();
        m_preloadImage = QImage();
    }
    m_preloadInFlightPath = path;
    const QString loadPath = path;
    const QPointer<ImageView> guard(this);
    qDebug().nospace()
        << "[slideshow] preload-start " << QFileInfo(loadPath).fileName();
    QThreadPool::globalInstance()->start([guard, loadPath, gen]() {
        const QImage img = ImageLoader::load(loadPath);
        if (img.isNull()) {
            return;
        }
        if (ImageLoader::cachedThumbnail(loadPath).isNull()) {
            const int maxEdge = 512;
            QImage thumb = img;
            if (thumb.width() > maxEdge || thumb.height() > maxEdge) {
                thumb = thumb.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
            }
            ImageLoader::putCachedThumbnail(loadPath, thumb);
        }
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, loadPath, img, gen]() {
            ImageView *view = guard.data();
            if (!view || img.isNull()) {
                return;
            }
            if (gen != view->m_preloadGeneration) {
                return;
            }
            view->m_preloadPath = loadPath;
            view->m_preloadImage = img;
            view->m_preloadInFlightPath.clear();
            view->m_ssFullByPath.insert(loadPath, img);
            qDebug().nospace()
                << "[slideshow] preload-ready " << QFileInfo(loadPath).fileName()
                << " " << img.width() << "x" << img.height();
            view->rememberImageSize(loadPath, img.size());
            if (view->m_slideshowProgressActive) {
                if (loadPath == view->m_ssFromPath) {
                    view->m_ssFromImage = img;
                    view->m_dwellSourceImage = img;
                }
                if (loadPath == view->m_ssToPath) {
                    view->m_ssToImage = img;
                }
                if (view->viewport()) {
                    view->viewport()->update();
                }
            }
            // Legacy live-fade upgrade path.
            if (view->m_liveTransitionNextPath == loadPath
                && (view->m_liveTransitionActive || view->m_liveTransitionHold
                    || view->m_liveTransitionAwaitingLoad)) {
                view->m_liveTransitionSourceImage = img;
                view->m_handoffPath = loadPath;
                view->m_handoffImage = img;
                view->m_preloadPath.clear();
                view->m_preloadImage = QImage();
                view->ensureMotionAtlas(img, &view->m_liveToAtlas,
                                        &view->m_liveToAtlasScale,
                                        &view->m_liveToAtlasVw,
                                        &view->m_liveToAtlasVh);
                qDebug().nospace()
                    << "[slideshow] live-upgrade "
                    << QFileInfo(loadPath).fileName()
                    << " " << img.width() << "x" << img.height();
                if (view->m_liveTransitionAwaitingLoad
                    && !view->m_liveTransitionActive
                    && !view->m_liveTransitionHold) {
                    view->startLiveTransitionWithImage(img);
                }
                if (view->viewport()) {
                    view->viewport()->update();
                }
                return;
            }
            // beginLive is waiting on this path — open the fade now.
            if (view->m_liveTransitionAwaitingLoad
                && view->m_liveTransitionNextPath == loadPath
                && !view->m_liveTransitionActive
                && !view->m_liveTransitionHold) {
                const QImage full = img;
                view->m_preloadPath.clear();
                view->m_preloadImage = QImage();
                view->m_handoffPath = loadPath;
                view->m_handoffImage = full;
                qDebug().nospace()
                    << "[slideshow] beginLive preload-hit "
                    << QFileInfo(loadPath).fileName()
                    << " " << full.width() << "x" << full.height();
                view->startLiveTransitionWithImage(full);
            }
        }, Qt::QueuedConnection);
    });
}

QPixmap ImageView::renderCoverPixmap(const QImage &image) const
{
    // Static centre cover — used when motion is Off or as a fallback.
    return renderMotionCoverPixmap(image, 0.0, 0);
}

qreal ImageView::motionPathMaxScale(const QImage &image) const
{
    if (image.isNull() || !viewport()) {
        return 1.0;
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    const qreal iw = qreal(image.width());
    const qreal ih = qreal(image.height());
    if (iw < 1.0 || ih < 1.0) {
        return 1.0;
    }
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
        return 1.0;
    }

    if (m_slideshowMotion == SlideshowMotion::PanScan) {
        qreal s = base;
        const bool preferX = iw * qreal(vh) >= ih * qreal(vw);
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
        return s;
    }
    if (m_slideshowMotion == SlideshowMotion::PanZoom) {
        const qreal factor = qBound(1.02, m_panZoomFactor, 1.40);
        qreal motionBase = base;
        constexpr qreal kMinHalf = 32.0;
        for (int i = 0; i < 10; ++i) {
            const qreal hx = qMax(0.0, (iw - qreal(vw) / motionBase) * 0.5);
            const qreal hy = qMax(0.0, (ih - qreal(vh) / motionBase) * 0.5);
            if (hx >= kMinHalf || hy >= kMinHalf) {
                break;
            }
            motionBase *= 1.08;
        }
        return motionBase * factor; // largest scale along the path
    }
    return base;
}

void ImageView::ensureMotionAtlas(const QImage &image, QPixmap *atlas,
                                  qreal *atlasScale, int *atlasVw, int *atlasVh) const
{
    if (!atlas || !atlasScale || !atlasVw || !atlasVh || image.isNull() || !viewport()) {
        return;
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    const qreal maxScale = motionPathMaxScale(image);
    if (!atlas->isNull() && qFuzzyCompare(*atlasScale, maxScale)
        && *atlasVw == vw && *atlasVh == vh) {
        return;
    }
    const int dw = qMax(1, int(qRound(qreal(image.width()) * maxScale)));
    const int dh = qMax(1, int(qRound(qreal(image.height()) * maxScale)));
    // One Smooth scale per source/viewport change — not per frame.
    QImage scaled = image.scaled(dw, dh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        *atlas = QPixmap();
        *atlasScale = 0.0;
        return;
    }
    *atlas = QPixmap::fromImage(std::move(scaled));
    *atlasScale = maxScale;
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

void ImageView::paintMotionCover(QPainter *painter, const QImage &image,
                                 qreal motionT, QPointF biasA, QPointF biasB,
                                 uint pathHash) const
{
    if (!painter || image.isNull() || !viewport()) {
        return;
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    const qreal iw = qreal(image.width());
    const qreal ih = qreal(image.height());
    if (iw < 1.0 || ih < 1.0) {
        return;
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
    m_liveTransitionSourceImage = nextImage;
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
    m_dwellSourceImage = item->sourceImage();
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
            m_ssFromMotionT = qBound(0.0, qreal(m_ssFromMotionClock.elapsed()) / qreal(pathMs), 1.0);
            m_dwellMotionT = m_ssFromMotionT;
        }
        if (m_ssToMotionClockRunning) {
            m_ssToMotionT = qBound(0.0, qreal(m_ssToMotionClock.elapsed()) / qreal(pathMs), 1.0);
        }
        // Upgrade only from full buffers (preload/item) — no soft scale per tick.
        if (!m_ssFromPath.isEmpty()) {
            const QImage full = slideshowFullIfReady(m_ssFromPath);
            if (!full.isNull()
                && (m_ssFromImage.isNull()
                    || full.width() * full.height()
                        > m_ssFromImage.width() * m_ssFromImage.height())) {
                m_ssFromImage = full;
                m_dwellSourceImage = full;
            }
        }
        if (!m_ssToPath.isEmpty()) {
            const QImage full = slideshowFullIfReady(m_ssToPath);
            if (!full.isNull()
                && (m_ssToImage.isNull()
                    || full.width() * full.height()
                        > m_ssToImage.width() * m_ssToImage.height())) {
                m_ssToImage = full;
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
                        qDebug().nospace()
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
        return ArchivePath::displayName(m_gallery.hoverPath());
    }
    // Empty Gallery/Workspace canvas: only the drop invite is shown — never a
    // stale classicPath, load error, or leftover session subject filename.
    if (m_items.isEmpty() && isMultiItemMode()) {
        return {};
    }
    if (!m_lastLoadError.isEmpty()) {
        return ArchivePath::displayName(m_lastLoadError);
    }
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (item) {
        return ArchivePath::displayName(item->path());
    }
    if (hasClassicPath() && isImageMode()) {
        return ArchivePath::displayName(classicPath());
    }
    return {};
}

QString ImageView::statusText() const
{
    if (m_zoomRegionArmed || m_zoomRegionDragging) {
        return tr("Zoom region: drag a rectangle · Esc cancels");
    }
    // Technical status only (no index badge, no bare filename — those live in
    // dedicated HUD corners). Used as secondary bottom line when the HUD is pinned.
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }

    if (!item) {
        if (!m_lastLoadError.isEmpty()) {
            return tr("Failed to load “%1”").arg(ArchivePath::displayName(m_lastLoadError));
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

    if (isMultiItemMode()) {
        const QString modeLabel = isGalleryMode() ? tr("Gallery") : tr("Workspace");
        QString text = tr("%1: %2 images  |  View zoom: %3%  |  %4×%5")
                           .arg(modeLabel)
                           .arg(m_items.size())
                           .arg(qRound(viewScale() * 100))
                           .arg(item->imageSize().width())
                           .arg(item->imageSize().height());
        const int pending = pendingDecodeCount();
        if (pending > 0) {
            text += tr("  |  Decoding: %n", "status pending decodes", pending);
        }
        if (item->isSelected()) {
            if (qAbs(item->itemScaleX() - item->itemScaleY()) < 0.005) {
                text += tr("  |  Item: %1%  |  Rot: %2°")
                            .arg(qRound(item->itemScaleX() * 100))
                            .arg(qRound(item->itemRotation()));
            } else {
                text += tr("  |  Item: %1%×%2%  |  Rot: %3°")
                            .arg(qRound(item->itemScaleX() * 100))
                            .arg(qRound(item->itemScaleY() * 100))
                            .arg(qRound(item->itemRotation()));
            }
            if (qAbs(item->itemShear()) > 1e-3) {
                text += tr("  |  Shear: %1").arg(item->itemShear(), 0, 'f', 2);
            }
        }
        if (item->itemOpacity() < 0.999) {
            text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
        }
        {
            const auto it = m_itemStates.constFind(item->path());
            if (it != m_itemStates.cend() && it->hasCrop && !it->cropRect.isEmpty()) {
                text += tr("  |  Cropped: %1×%2")
                            .arg(it->cropRect.width())
                            .arg(it->cropRect.height());
            }
        }
        return text;
    }

    // Image mode: zoom is view-level
    QString text = tr("%1×%2  |  Zoom: %3%  |  Rotation: %4°")
                       .arg(item->imageSize().width())
                       .arg(item->imageSize().height())
                       .arg(qRound(viewScale() * 100))
                       .arg(qRound(item->itemRotation()));
    if (qAbs(item->itemShear()) > 1e-3) {
        text += tr("  |  Shear: %1").arg(item->itemShear(), 0, 'f', 2);
    }
    if (item->itemHFlip() || item->itemVFlip()) {
        QStringList flips;
        if (item->itemHFlip()) {
            flips << tr("H");
        }
        if (item->itemVFlip()) {
            flips << tr("V");
        }
        text += tr("  |  Flip: %1").arg(flips.join(QLatin1Char('+')));
    }
    if (item->itemOpacity() < 0.999) {
        text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
    }
    {
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend() && it->hasCrop && !it->cropRect.isEmpty()) {
            // cropRect is original-space size of the kept region.
            text += tr("  |  Cropped: %1×%2")
                        .arg(it->cropRect.width())
                        .arg(it->cropRect.height());
        }
    }
    return text;
}
