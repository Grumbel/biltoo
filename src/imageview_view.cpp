// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "archivepath.h"
#include <QPixmap>
#include "imageitem.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QTimer>
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
        cancelKenBurns();
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
    if (m_slideshowTransition == SlideshowTransition::None
        || m_slideshowTransitionDurationMs <= 0
        || !isImageMode()
        || !viewport()) {
        cancelKenBurns();
        return;
    }
    // Grab first so a mid-Ken-Burns frame is preserved in the transition.
    const QPixmap shot = viewport()->grab();
    cancelKenBurns();
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

    // Slide needs both frames. Grab the *new* view while the transition overlay
    // is still inactive — otherwise paintEvent draws the old snapshot on top and
    // grab() captures the outgoing frame twice.
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
            maybeStartKenBurns();
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


void ImageView::setKenBurnsEnabled(bool on)
{
    m_kenBurnsEnabled = on;
    if (!on) {
        cancelKenBurns();
    }
}

void ImageView::setKenBurnsZoomFactor(qreal factor)
{
    m_kenBurnsZoomFactor = qBound(1.02, factor, 1.5);
}

void ImageView::cancelKenBurns()
{
    m_kenBurnsActive = false;
    if (m_kenBurnsAnim) {
        m_kenBurnsAnim->stop();
    }
}

void ImageView::maybeStartKenBurns()
{
    if (!m_kenBurnsEnabled || !m_slideshowProgressActive || !isImageMode()) {
        return;
    }
    if (m_slideshowTransitionPending || m_slideshowTransitionActive) {
        return; // transition finished handler will call again
    }
    int duration = m_slideshowProgressIntervalMs;
    if (duration < 250) {
        return;
    }
    // Leave a little idle at the end of the dwell so the last frame holds.
    duration = qMax(250, duration - 80);
    startKenBurns(duration);
}

void ImageView::startKenBurns(int durationMs)
{
    cancelKenBurns();
    if (!m_kenBurnsEnabled || !isImageMode() || durationMs < 250 || !viewport()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->boundingRect().isEmpty()) {
        return;
    }

    // Cover the viewport so pan/zoom never letterboxes mid-slide.
    m_fitMode = false;
    m_fillMode = true;
    fitItem(item, Qt::KeepAspectRatioByExpanding);

    m_kenBurnsStartScale = viewScale();
    if (m_kenBurnsStartScale <= 0.0) {
        return;
    }
    m_kenBurnsEndScale = m_kenBurnsStartScale * m_kenBurnsZoomFactor;
    m_kenBurnsStartCenter = mapToScene(viewport()->rect().center());

    // Drift toward a random corner of the image (classic documentary feel).
    const QRectF br = item->sceneBoundingRect();
    const QPointF corners[4] = {
        br.topLeft(), br.topRight(), br.bottomLeft(), br.bottomRight()
    };
    const int corner = QRandomGenerator::global()->bounded(4);
    const QPointF toward = corners[corner];
    // Blend only part-way so the frame stays mostly on the image at end scale.
    m_kenBurnsEndCenter = m_kenBurnsStartCenter + (toward - m_kenBurnsStartCenter) * 0.28;

    if (!m_kenBurnsAnim) {
        m_kenBurnsAnim = new QVariantAnimation(this);
        m_kenBurnsAnim->setEasingCurve(QEasingCurve::InOutSine);
        connect(m_kenBurnsAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) {
                    applyKenBurnsProgress(v.toReal());
                });
        connect(m_kenBurnsAnim, &QVariantAnimation::finished, this, [this]() {
            m_kenBurnsActive = false;
        });
    }
    m_kenBurnsActive = true;
    m_kenBurnsAnim->stop();
    m_kenBurnsAnim->setStartValue(0.0);
    m_kenBurnsAnim->setEndValue(1.0);
    m_kenBurnsAnim->setDuration(durationMs);
    applyKenBurnsProgress(0.0);
    m_kenBurnsAnim->start();
}

void ImageView::applyKenBurnsProgress(qreal t)
{
    t = qBound(0.0, t, 1.0);
    const qreal s = m_kenBurnsStartScale + (m_kenBurnsEndScale - m_kenBurnsStartScale) * t;
    const QPointF c = m_kenBurnsStartCenter
        + (m_kenBurnsEndCenter - m_kenBurnsStartCenter) * t;
    if (s <= 0.0 || !viewport()) {
        return;
    }
    // Build a view transform that places scene point c at the viewport centre
    // with uniform scale s (same convention as fitInView / viewScale).
    const QPointF vc = QPointF(viewport()->width() * 0.5, viewport()->height() * 0.5);
    QTransform xform;
    xform.translate(vc.x(), vc.y());
    xform.scale(s, s);
    xform.translate(-c.x(), -c.y());
    setTransform(xform);
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
