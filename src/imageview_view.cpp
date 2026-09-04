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
    m_liveTransitionSourceImage = QImage();
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
        return;
    }
    // Fade-through-black: load finished under solid black — resume the second
    // half so the veil lifts over the already-fitted next slide.
    if (m_liveTransitionAwaitingLoad
        && m_slideshowTransition == SlideshowTransition::FadeBlack
        && m_liveTransitionActive) {
        m_liveTransitionAwaitingLoad = false;
        m_liveTransitionElapsedBaseMs = m_liveTransitionDurationMs / 2;
        m_liveTransitionClock.restart();
        m_liveTransitionProgress = 0.5;
        if (m_liveTransitionTimer && !m_liveTransitionTimer->isActive()) {
            m_liveTransitionTimer->start();
        }
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    m_liveTransitionHold = false;
    m_liveTransitionAwaitingLoad = false;
    m_liveTransitionActive = false;
    m_liveTransitionProgress = 1.0;
    m_slideshowTransitionToPixmap = QPixmap();
    m_liveTransitionSourceImage = QImage();
    m_liveTransitionPathHash = 0;
    m_liveTransitionMotionProgress = 0.0;
    m_liveTransitionNextPath.clear();
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
        // Projector slide is two static frames; freeze dwell camera for the swap.
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
            // Resume dwell camera after a static Slide swap.
            maybeStartSlideshowMotion();
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
    // Zoom is the base scale for the dwell camera as well as static framing.
    if (m_slideshowProgressActive) {
        reapplySlideshowFraming();
    }
}

void ImageView::applySlideshowZoomFraming(ImageItem *item)
{
    if (!item || !viewport()) {
        return;
    }
    // Explicit uniform scale (same approach as the dwell camera).
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
    if (m_slideshowMotion != SlideshowMotion::Off) {
        // Restart the dwell camera from the zoom base + current interval.
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

void ImageView::cancelSlideshowMotion()
{
    m_slideshowMotionActive = false;
    m_motionElapsedOffsetMs = 0;
    if (m_motionTimer) {
        m_motionTimer->stop();
    }
}

bool ImageView::beginLiveSlideshowTransition(const QString &nextPath)
{
    // Slide (projector) is a hard cut of two full frames — incompatible with a
    // live moving underlay. Use the snapshot transition path instead.
    if (m_slideshowMotion == SlideshowMotion::Off
        || m_slideshowTransition == SlideshowTransition::None
        || m_slideshowTransition == SlideshowTransition::Slide
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
    // Static centre cover — used when motion is Off or as a fallback.
    return renderMotionCoverPixmap(image, 0.0, 0);
}

QPixmap ImageView::renderMotionCoverPixmap(const QImage &image, qreal motionT,
                                           uint pathHash) const
{
    if (image.isNull() || !viewport()) {
        return {};
    }
    const int vw = qMax(1, viewport()->width());
    const int vh = qMax(1, viewport()->height());
    const qreal iw = qreal(image.width());
    const qreal ih = qreal(image.height());
    if (iw < 1.0 || ih < 1.0) {
        return {};
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
        return {};
    }

    qreal scale = base;
    qreal biasX = 0.0;
    qreal biasY = 0.0;

    if (m_slideshowMotion == SlideshowMotion::PanScan) {
        qreal s = base;
        const bool preferX = iw * qreal(vh) >= ih * qreal(vw);
        {
            const qreal viewW = qreal(vw) / s;
            const qreal viewH = qreal(vh) / s;
            const qreal halfX = qMax(0.0, (iw - viewW) * 0.5);
            const qreal halfY = qMax(0.0, (ih - viewH) * 0.5);
            const qreal travel = preferX ? halfX : halfY;
            constexpr qreal kMinTravel = 8.0;
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
        const bool flip = (pathHash & 1u) != 0;
        if (preferX) {
            biasX = flip ? (1.0 - 2.0 * motionT) : (-1.0 + 2.0 * motionT);
            biasY = 0.0;
        } else {
            biasX = 0.0;
            biasY = flip ? (1.0 - 2.0 * motionT) : (-1.0 + 2.0 * motionT);
        }
    } else if (m_slideshowMotion == SlideshowMotion::PanZoom) {
        const qreal factor = qBound(1.02, m_panZoomFactor, 1.40);
        scale = base * (1.0 + (factor - 1.0) * motionT);
        static const QPointF kBias[8] = {
            QPointF(-1.0, -1.0), QPointF(1.0, -1.0),
            QPointF(-1.0, 1.0), QPointF(1.0, 1.0),
            QPointF(-1.0, 0.0), QPointF(1.0, 0.0),
            QPointF(0.0, -1.0), QPointF(0.0, 1.0),
        };
        uint seed = pathHash ? pathHash : 1u;
        const QPointF biasA = kBias[seed % 8];
        const QPointF biasB = kBias[(seed / 8 + 3) % 8];
        biasX = biasA.x() + (biasB.x() - biasA.x()) * motionT;
        biasY = biasA.y() + (biasB.y() - biasA.y()) * motionT;
    } else {
        scale = base;
        biasX = 0.0;
        biasY = 0.0;
    }

    const int dw = qMax(1, int(qRound(iw * scale)));
    const int dh = qMax(1, int(qRound(ih * scale)));

    // Uniform scale only — never IgnoreAspectRatio-stretch into the viewport.
    QImage scaled = image.scaled(dw, dh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return {};
    }

    // Compose onto an exact viewport-sized canvas. Overflow → negative dest
    // (clips); undersize → padding. Bias shifts the image for pan samples.
    QImage out(vw, vh, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const qreal overflowX = qMax(0.0, (qreal(dw) - qreal(vw)) * 0.5);
    const qreal overflowY = qMax(0.0, (qreal(dh) - qreal(vh)) * 0.5);
    const int destX = int(qRound((qreal(vw) - qreal(dw)) * 0.5 - biasX * overflowX));
    const int destY = int(qRound((qreal(vh) - qreal(dh)) * 0.5 - biasY * overflowY));
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(destX, destY, scaled);
    painter.end();
    return QPixmap::fromImage(out);
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
    m_liveTransitionSourceImage = nextImage;
    m_liveTransitionPathHash = qHash(m_liveTransitionNextPath);
    // First frame at motion t=0; subsequent ticks re-render along the path.
    m_slideshowTransitionToPixmap = renderMotionCoverPixmap(
        m_liveTransitionSourceImage, 0.0, m_liveTransitionPathHash);
    if (m_slideshowTransitionToPixmap.isNull()) {
        m_liveTransitionSourceImage = QImage();
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
    m_liveTransitionHold = false;
    m_liveTransitionAwaitingLoad = false;
    m_liveTransitionElapsedBaseMs = 0;
    m_liveTransitionProgress = 0.0;
    m_liveTransitionMotionProgress = 0.0;
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
    // Frozen at mid-black until LoadReplace has fitted the next slide.
    if (m_liveTransitionAwaitingLoad) {
        m_liveTransitionProgress = 0.5;
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    const qreal elapsed = qreal(m_liveTransitionElapsedBaseMs + m_liveTransitionClock.elapsed());
    const qreal t = m_liveTransitionDurationMs > 0
        ? elapsed / qreal(m_liveTransitionDurationMs)
        : 1.0;

    // Fade-through-black: advance (goNext) at full black, then freeze the veil
    // until the next image is fitted so the lift never reveals the old frame.
    if (m_slideshowTransition == SlideshowTransition::FadeBlack
        && !m_liveTransitionMidAdvanced && t >= 0.5) {
        m_liveTransitionMidAdvanced = true;
        m_liveTransitionAwaitingLoad = true;
        m_liveTransitionProgress = 0.5;
        if (viewport()) {
            viewport()->update();
        }
        emit slideshowLiveTransitionFinished();
        return;
    }

    if (t >= 1.0) {
        m_liveTransitionProgress = 1.0;
        if (m_liveTransitionTimer) {
            m_liveTransitionTimer->stop();
        }
        // Crossfade / Slide: hold the final composite until LoadReplace fits.
        // FadeBlack already advanced at mid-black; the next slide is fitted, so
        // clear the veil immediately (no hold needed).
        if (!m_liveTransitionMidAdvanced) {
            m_liveTransitionHold = true;
            m_liveTransitionActive = false;
            // Keep final to-pixmap + motion progress for camera handoff.
            m_liveTransitionSourceImage = QImage();
            if (viewport()) {
                viewport()->update();
            }
            emit slideshowLiveTransitionFinished();
        } else {
            m_liveTransitionActive = false;
            m_liveTransitionHold = false;
            m_slideshowTransitionToPixmap = QPixmap();
            m_liveTransitionSourceImage = QImage();
            m_liveTransitionPathHash = 0;
            m_liveTransitionMotionProgress = 0.0;
            if (viewport()) {
                viewport()->update();
            }
        }
        return;
    }
    m_liveTransitionProgress = t;
    // Sample the dwell path in real time so the overlay matches the camera
    // after LoadReplace (compressed 0→1 caused a jump from path-end to start).
    if ((m_slideshowTransition == SlideshowTransition::Crossfade
         || m_slideshowTransition == SlideshowTransition::Slide)
        && !m_liveTransitionSourceImage.isNull()
        && m_slideshowMotion != SlideshowMotion::Off) {
        qreal motionT = 0.0;
        if (m_slideshowProgressIntervalMs > 0) {
            motionT = qBound(0.0,
                elapsed / qreal(m_slideshowProgressIntervalMs), 1.0);
        }
        m_liveTransitionMotionProgress = motionT;
        const QPixmap frame = renderMotionCoverPixmap(
            m_liveTransitionSourceImage, motionT, m_liveTransitionPathHash);
        if (!frame.isNull()) {
            m_slideshowTransitionToPixmap = frame;
        }
    }
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
    // Continue from live-transition overlay sample when present.
    qreal initial = 0.0;
    if (m_liveTransitionHold || m_liveTransitionActive || m_liveTransitionAwaitingLoad) {
        initial = m_liveTransitionMotionProgress;
    }
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

    // Identity item pose — camera is entirely the view matrix. Residual
    // Workspace shear/rotation would look like stretch under the view scale.
    // m_fitMode must stay false so resizeEvent cannot fitItem over the camera.
    m_fitMode = false;
    m_fillMode = (m_slideshowZoom == SlideshowZoom::Fill);
    item->setItemShear(0.0);
    item->setItemRotation(0.0);
    item->setItemScale(1.0);
    if (isImageMode()) {
        item->setPos(0, 0);
    }

    // Content size (source aspect), not scene AABB which can inflate under
    // a non-identity local transform.
    const QRectF content = item->contentRect();
    if (content.width() < 1.0 || content.height() < 1.0) {
        return;
    }
    const qreal iw = content.width();
    const qreal ih = content.height();
    const QPointF mid = item->mapToScene(content.center());
    const qreal vw = qreal(qMax(1, viewport()->width()));
    const qreal vh = qreal(qMax(1, viewport()->height()));

    // Room for the camera to pan without scrollbar clamping.
    if (m_scene) {
        const qreal pad = qMax(vw, vh) * 2.0;
        m_scene->setSceneRect(QRectF(mid.x() - iw * 0.5 - pad, mid.y() - ih * 0.5 - pad,
                                     iw + 2.0 * pad, ih + 2.0 * pad));
    }

    const qreal coverScale = qMax(vw / iw, vh / ih);
    const qreal fitScale = qMin(vw / iw, vh / ih);
    qreal baseScale = coverScale;
    switch (m_slideshowZoom) {
    case SlideshowZoom::Fill:
        baseScale = coverScale;
        break;
    case SlideshowZoom::Actual:
        baseScale = 1.0;
        break;
    case SlideshowZoom::Fit:
    default:
        baseScale = fitScale;
        break;
    }
    if (baseScale <= 0.0 || !qIsFinite(baseScale)) {
        return;
    }

    // Half the room the camera may move at a given scale (scene units).
    // When scale is below cover the view is larger than the image → no overflow
    // (letterbox / padding); clamp keeps the image centred.
    auto halfOverflow = [&](qreal scale) -> QPointF {
        if (scale <= 0.0) {
            return QPointF(0, 0);
        }
        const qreal viewW = vw / scale;
        const qreal viewH = vh / scale;
        return QPointF(qMax(0.0, (iw - viewW) * 0.5),
                       qMax(0.0, (ih - viewH) * 0.5));
    };
    auto clampCenter = [&](QPointF c, qreal scale) -> QPointF {
        const QPointF half = halfOverflow(scale);
        return QPointF(qBound(mid.x() - half.x(), c.x(), mid.x() + half.x()),
                       qBound(mid.y() - half.y(), c.y(), mid.y() + half.y()));
    };

    if (m_slideshowMotion == SlideshowMotion::PanScan) {
        // Constant scale at the zoom base. Overscan only when that base leaves
        // almost no overflow (e.g. Fit, or Fill on near-matching aspect).
        qreal scale = baseScale;
        QPointF half = halfOverflow(scale);
        const bool preferX = iw * vh >= ih * vw; // wider than view
        qreal travel = preferX ? half.x() : half.y();
        constexpr qreal kMinTravelScene = 8.0; // px of motion at least
        if (travel < kMinTravelScene) {
            // Target ~20% of the long image side as total travel (10% each side).
            const qreal longSide = preferX ? iw : ih;
            const qreal wantHalf = qMax(kMinTravelScene, longSide * 0.10);
            if (preferX) {
                const qreal viewW = qMax(vw * 0.5, iw - 2.0 * wantHalf);
                scale = vw / viewW;
            } else {
                const qreal viewH = qMax(vh * 0.5, ih - 2.0 * wantHalf);
                scale = vh / viewH;
            }
            scale = qMax(scale, baseScale);
            half = halfOverflow(scale);
            travel = preferX ? half.x() : half.y();
        }
        m_motionStartScale = scale;
        m_motionEndScale = scale;
        if (preferX && half.x() > 0.5) {
            m_motionStartCenter = QPointF(mid.x() - half.x(), mid.y());
            m_motionEndCenter = QPointF(mid.x() + half.x(), mid.y());
        } else if (!preferX && half.y() > 0.5) {
            m_motionStartCenter = QPointF(mid.x(), mid.y() - half.y());
            m_motionEndCenter = QPointF(mid.x(), mid.y() + half.y());
        } else if (half.x() >= half.y() && half.x() > 0.5) {
            m_motionStartCenter = QPointF(mid.x() - half.x(), mid.y());
            m_motionEndCenter = QPointF(mid.x() + half.x(), mid.y());
        } else if (half.y() > 0.5) {
            m_motionStartCenter = QPointF(mid.x(), mid.y() - half.y());
            m_motionEndCenter = QPointF(mid.x(), mid.y() + half.y());
        } else {
            m_motionStartCenter = mid;
            m_motionEndCenter = mid;
        }
        m_motionStartCenter = clampCenter(m_motionStartCenter, m_motionStartScale);
        m_motionEndCenter = clampCenter(m_motionEndCenter, m_motionEndScale);
    } else {
        // Ken Burns: zoom from the slideshow-zoom base toward base*factor while
        // panning between two distinct centres (rule-of-thirds / corners).
        m_motionStartScale = baseScale;
        m_motionEndScale = baseScale * m_panZoomFactor;
        const QPointF halfStart = halfOverflow(m_motionStartScale);
        const QPointF halfEnd = halfOverflow(m_motionEndScale);

        const QString path = item->path();
        uint seed = qHash(path);
        if (seed == 0) {
            seed = 1;
        }
        // Prefer strong corner/edge biases so the pan is obvious under zoom.
        static const QPointF kBias[8] = {
            QPointF(-1.0, -1.0), QPointF(1.0, -1.0),
            QPointF(-1.0, 1.0), QPointF(1.0, 1.0),
            QPointF(-1.0, 0.0), QPointF(1.0, 0.0),
            QPointF(0.0, -1.0), QPointF(0.0, 1.0),
        };
        const QPointF biasA = kBias[seed % 8];
        const QPointF biasB = kBias[(seed / 8 + 3) % 8]; // force different slot

        m_motionStartCenter = QPointF(mid.x() + biasA.x() * halfStart.x(),
                                      mid.y() + biasA.y() * halfStart.y());
        m_motionEndCenter = QPointF(mid.x() + biasB.x() * halfEnd.x(),
                                    mid.y() + biasB.y() * halfEnd.y());
        if (halfStart.x() < 0.5 && halfStart.y() < 0.5 && (halfEnd.x() > 0.5 || halfEnd.y() > 0.5)) {
            m_motionStartCenter = mid;
            m_motionEndCenter = QPointF(mid.x() + biasB.x() * halfEnd.x(),
                                        mid.y() + biasB.y() * halfEnd.y());
        }
        m_motionStartCenter = clampCenter(m_motionStartCenter, m_motionStartScale);
        m_motionEndCenter = clampCenter(m_motionEndCenter, m_motionEndScale);
    }

    if (!m_motionTimer) {
        m_motionTimer = new QTimer(this);
        m_motionTimer->setTimerType(Qt::PreciseTimer);
        m_motionTimer->setInterval(16);
        connect(m_motionTimer, &QTimer::timeout, this, &ImageView::tickSlideshowMotion);
    }
    m_slideshowMotionActive = true;
    m_motionDurationMs = durationMs;
    initialProgress = qBound(0.0, initialProgress, 1.0);
    // Pretend the clock already advanced so the first frame matches the
    // live-transition overlay (avoids path-end → path-start jump).
    m_motionClock.start();
    if (initialProgress > 0.0 && durationMs > 0) {
        const qint64 already = qint64(initialProgress * qreal(durationMs));
        // QElapsedTimer cannot be set backward; offset via restart + stored base.
        m_motionElapsedOffsetMs = already;
    } else {
        m_motionElapsedOffsetMs = 0;
    }
    applySlideshowMotionProgress(initialProgress);
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
        ? qreal(m_motionElapsedOffsetMs + m_motionClock.elapsed())
              / qreal(m_motionDurationMs)
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
    if (s <= 0.0 || !qIsFinite(s) || !viewport()) {
        return;
    }
    // Uniform scale only. Bake the camera centre into the matrix and zero
    // scrollbars so QGraphicsView cannot cancel the pan or introduce a
    // non-uniform effective mapping via scrollbar ranges.
    const qreal vx = qreal(viewport()->width()) * 0.5;
    const qreal vy = qreal(viewport()->height()) * 0.5;
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    QTransform xform;
    xform.translate(vx, vy);
    xform.scale(s, s);
    xform.translate(-c.x(), -c.y());
    setTransform(xform);
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setValue(0);
    }
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(0);
    }
    if (viewport()) {
        viewport()->update();
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
