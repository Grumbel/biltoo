// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageloader.h"
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
    viewport()->update();
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
    if (m_hudVisible || m_hudFlashVisible) {
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
    if (changed || m_hudVisible || m_hudFlashVisible || m_hudIdentityPulse) {
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
    }
    viewport()->update();
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
    const QPixmap shot = viewport()->grab();
    if (shot.isNull()) {
        return;
    }
    m_slideshowTransitionPixmap = shot;
    m_slideshowTransitionProgress = 0.0;
    m_slideshowTransitionPending = true;
    m_slideshowTransitionActive = false;
}

void ImageView::cancelSlideshowTransition()
{
    m_slideshowTransitionPending = false;
    m_slideshowTransitionActive = false;
    m_slideshowTransitionProgress = 1.0;
    m_slideshowTransitionPixmap = QPixmap();
    m_slideshowTransitionToPixmap = QPixmap();
    if (m_slideshowTransitionAnim) {
        m_slideshowTransitionAnim->stop();
    }
    m_liveTransitionActive = false;
    m_liveTransitionProgress = 0.0;
    m_liveTransitionMidAdvanced = false;
    if (m_liveTransitionTimer) {
        m_liveTransitionTimer->stop();
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::startSlideshowTransitionAnimation()
{
    if (!m_slideshowTransitionPending || m_slideshowTransitionPixmap.isNull()) {
        m_slideshowTransitionPending = false;
        return;
    }
    m_slideshowTransitionPending = false;

    m_slideshowTransitionToPixmap = QPixmap();
    if (m_slideshowTransition == SlideshowTransition::Slide && viewport()) {
        m_slideshowTransitionActive = false;
        m_slideshowTransitionProgress = 1.0;
        m_slideshowTransitionToPixmap = viewport()->grab();
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
            if (viewport()) {
                viewport()->update();
            }
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
    }
}

void ImageView::setPanZoomFactor(qreal factor)
{
    m_panZoomFactor = qBound(1.02, factor, 1.5);
}

void ImageView::cancelSlideshowMotion()
{
    m_slideshowMotionActive = false;
    if (m_motionTimer) {
        m_motionTimer->stop();
    }
}

bool ImageView::beginLiveSlideshowTransition(const QString &nextPath)
{
    if (m_slideshowMotion == SlideshowMotion::Off
        || m_slideshowTransition == SlideshowTransition::None
        || m_slideshowTransitionDurationMs <= 0
        || !isImageMode()
        || !viewport()
        || nextPath.isEmpty()) {
        return false;
    }
    cancelSlideshowTransition(); // no frozen snapshot path
    m_liveTransitionNextPath = nextPath;
    m_liveTransitionMidAdvanced = false;
    const QString path = nextPath;
    const int maxEdge = qMax(viewport()->width(), viewport()->height());
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, maxEdge]() {
        // Prefer a large decode so cover-scaling looks sharp.
        QImage img = ImageLoader::loadThumbnail(path, qMax(maxEdge * 2, 512));
        if (img.isNull()) {
            img = ImageLoader::load(path);
        }
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, img]() {
            if (!guard) {
                return;
            }
            guard->startLiveTransitionWithImage(img);
        }, Qt::QueuedConnection);
    });
    return true;
}

QPixmap ImageView::renderCoverPixmap(const QImage &image) const
{
    if (image.isNull() || !viewport()) {
        return {};
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    QImage scaled = image.scaled(vw, vh, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return {};
    }
    // Centre-crop to exact viewport size.
    const int x = qMax(0, (scaled.width() - vw) / 2);
    const int y = qMax(0, (scaled.height() - vh) / 2);
    scaled = scaled.copy(x, y, qMin(vw, scaled.width()), qMin(vh, scaled.height()));
    if (scaled.size() != QSize(vw, vh)) {
        scaled = scaled.scaled(vw, vh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return QPixmap::fromImage(scaled);
}

void ImageView::startLiveTransitionWithImage(const QImage &nextImage)
{
    if (!isImageMode() || !viewport()) {
        emit slideshowLiveTransitionFinished();
        return;
    }
    if (nextImage.isNull()) {
        emit slideshowLiveTransitionFinished();
        return;
    }
    m_slideshowTransitionToPixmap = renderCoverPixmap(nextImage);
    if (m_slideshowTransitionToPixmap.isNull()) {
        emit slideshowLiveTransitionFinished();
        return;
    }
    if (!m_liveTransitionTimer) {
        m_liveTransitionTimer = new QTimer(this);
        m_liveTransitionTimer->setTimerType(Qt::PreciseTimer);
        m_liveTransitionTimer->setInterval(16);
        connect(m_liveTransitionTimer, &QTimer::timeout, this, &ImageView::tickLiveTransition);
    }
    m_liveTransitionActive = true;
    m_liveTransitionProgress = 0.0;
    m_liveTransitionDurationMs = m_slideshowTransitionDurationMs;
    m_liveTransitionClock.start();
    m_liveTransitionTimer->start();
    viewport()->update();
}

void ImageView::tickLiveTransition()
{
    if (!m_liveTransitionActive) {
        if (m_liveTransitionTimer) {
            m_liveTransitionTimer->stop();
        }
        return;
    }
    const qreal t = m_liveTransitionDurationMs > 0
        ? qreal(m_liveTransitionClock.elapsed()) / qreal(m_liveTransitionDurationMs)
        : 1.0;

    // Fade-through-black: swap the underlying image at full black so the
    // second half reveals the next slide under the lifting veil.
    if (m_slideshowTransition == SlideshowTransition::FadeBlack
        && !m_liveTransitionMidAdvanced && t >= 0.5) {
        m_liveTransitionMidAdvanced = true;
        emit slideshowLiveTransitionFinished();
        // Keep the veil running; host goNext() loads the next image under black.
    }

    if (t >= 1.0) {
        m_liveTransitionProgress = 1.0;
        m_liveTransitionActive = false;
        if (m_liveTransitionTimer) {
            m_liveTransitionTimer->stop();
        }
        m_slideshowTransitionToPixmap = QPixmap();
        if (viewport()) {
            viewport()->update();
        }
        // Crossfade / Slide / None-path: advance at the end (FadeBlack already did).
        if (!m_liveTransitionMidAdvanced) {
            emit slideshowLiveTransitionFinished();
        }
        return;
    }
    m_liveTransitionProgress = t;
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::maybeStartSlideshowMotion()
{
    if (m_slideshowMotion == SlideshowMotion::Off || !m_slideshowProgressActive
        || !isImageMode()) {
        return;
    }
    int duration = m_slideshowProgressIntervalMs;
    if (duration < 250) {
        return;
    }
    startSlideshowMotion(duration);
}

void ImageView::startSlideshowMotion(int durationMs)
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

    // Cover framing from the first frame.
    m_fitMode = false;
    m_fillMode = true;
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    fitItem(item, Qt::KeepAspectRatioByExpanding);

    m_motionStartScale = viewScale();
    if (m_motionStartScale <= 0.0) {
        return;
    }

    if (m_slideshowMotion == SlideshowMotion::PanZoom) {
        m_motionEndScale = m_motionStartScale * m_panZoomFactor;
    } else {
        // PanScan: constant scale, travel the full overflow of the cover frame.
        m_motionEndScale = m_motionStartScale;
    }

    const QRectF br = item->sceneBoundingRect();
    const qreal endS = m_motionEndScale;
    const qreal viewW = qreal(viewport()->width()) / endS;
    const qreal viewH = qreal(viewport()->height()) / endS;
    const qreal maxDx = qMax(0.0, (br.width() - viewW) * 0.5);
    const qreal maxDy = qMax(0.0, (br.height() - viewH) * 0.5);
    const QPointF mid = br.center();

    if (m_slideshowMotion == SlideshowMotion::PanScan) {
        // Show as much of the long axis as the dwell allows: full overflow.
        if (br.width() >= br.height()) {
            m_motionStartCenter = QPointF(mid.x() - maxDx, mid.y());
            m_motionEndCenter = QPointF(mid.x() + maxDx, mid.y());
        } else {
            m_motionStartCenter = QPointF(mid.x(), mid.y() - maxDy);
            m_motionEndCenter = QPointF(mid.x(), mid.y() + maxDy);
        }
    } else {
        // PanZoom: shorter travel so the zoom stays comfortable.
        constexpr qreal travel = 0.85;
        if (br.width() >= br.height()) {
            m_motionStartCenter = QPointF(mid.x() - maxDx * travel, mid.y());
            m_motionEndCenter = QPointF(mid.x() + maxDx * travel, mid.y());
        } else {
            m_motionStartCenter = QPointF(mid.x(), mid.y() - maxDy * travel);
            m_motionEndCenter = QPointF(mid.x(), mid.y() + maxDy * travel);
        }
    }

    if (!m_motionTimer) {
        m_motionTimer = new QTimer(this);
        m_motionTimer->setTimerType(Qt::PreciseTimer);
        m_motionTimer->setInterval(16);
        connect(m_motionTimer, &QTimer::timeout, this, &ImageView::tickSlideshowMotion);
    }
    m_slideshowMotionActive = true;
    m_motionDurationMs = durationMs;
    m_motionClock.start();
    applySlideshowMotionProgress(0.0);
    m_motionTimer->start();
}

void ImageView::tickSlideshowMotion()
{
    if (!m_slideshowMotionActive) {
        if (m_motionTimer) {
            m_motionTimer->stop();
        }
        return;
    }
    const qreal t = m_motionDurationMs > 0
        ? qreal(m_motionClock.elapsed()) / qreal(m_motionDurationMs)
        : 1.0;
    if (t >= 1.0) {
        applySlideshowMotionProgress(1.0);
        m_slideshowMotionActive = false;
        if (m_motionTimer) {
            m_motionTimer->stop();
        }
        return;
    }
    applySlideshowMotionProgress(t);
}

void ImageView::applySlideshowMotionProgress(qreal t)
{
    t = qBound(0.0, t, 1.0);
    const qreal s = m_motionStartScale + (m_motionEndScale - m_motionStartScale) * t;
    const QPointF c(
        m_motionStartCenter.x() + (m_motionEndCenter.x() - m_motionStartCenter.x()) * t,
        m_motionStartCenter.y() + (m_motionEndCenter.y() - m_motionStartCenter.y()) * t);
    if (s <= 0.0 || !viewport()) {
        return;
    }
    // Keep anchor centred so mouse position cannot bias setTransform.
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    const qreal vcX = qreal(viewport()->width()) * 0.5;
    const qreal vcY = qreal(viewport()->height()) * 0.5;
    QTransform xform;
    xform.translate(vcX, vcY);
    xform.scale(s, s);
    xform.translate(-c.x(), -c.y());
    setTransform(xform);
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
