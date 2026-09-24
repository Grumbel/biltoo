// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallery/gallerydecodesm.h"
#include "image/toolpolicy.h"
#include "workspace/workspacenavgeometry.h"
#include "workspace/grouptransformgeometry.h"
#include "item/selectiongeometry.h"
#include "view/viewtransform.h"
#include "item/placementlinear.h"
#include "attention/attentiongeometry.h"
#include "image/edgenavpolicy.h"
#include "host/pagepath.h"
#include "gallery/gallerylayout.h"
#include "imageitem.h"
#include "host/imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QSet>
#include <QThreadPool>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QWheelEvent>
#include <QRubberBand>
#include <QtMath>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <QGraphicsItem>
#include "host/thumtoocache.h"

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    return edgeZoneFromPolicy(m_image.edgeZoneAt(viewPos));
}
void ImageView::updateMouseInfo(const QPoint &viewPos)

{
    ImageMouseInfo info;
    const QPointF scenePos = mapToScene(viewPos);

    // Prefer the topmost item under the cursor
    ImageItem *hit = nullptr;
    const QList<QGraphicsItem *> hits = m_scene->items(scenePos);
    for (QGraphicsItem *gi : hits) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = item;
            break;
        }
    }

    if (hit) {
        const QPoint pixel = hit->pixelAtScenePos(scenePos);
        if (pixel.x() >= 0) {
            info.valid = true;
            info.imagePos = pixel;
            info.pixelColor = hit->colorAtPixel(pixel);
            info.path = hit->path();
        }
    }

    if (m_chrome.setMouseInfo(info)) {
        emit mouseInfoChanged(m_chrome.currentMouseInfo());
    }
}
void ImageView::wheelZoomViewAboutCursor(QWheelEvent *event)
{
    const qreal factor = ViewTransform::wheelZoomFactor(event->angleDelta().y());
    // Image mode and free-form Workspace: zoom the view about the cursor.
    // Do not touch selected-item geometry here — prepareGeometryChange on
    // handle pads was expanding AABBs and fighting the user's pan/zoom.
    m_slideshow.cancelSlideshowMotion();
    releaseStickyZoom();
    m_framing.releaseFit();
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    // Workspace: keep sceneRect covering all free-form tiles after zoom so
    // items are not clipped when the viewport-in-scene halo shrinks.
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    // Soft / PreferCache / tile LOD: coalesce continuous wheel notches.
    // Per-notch climb+tick was heavy on the GUI thread (set_viewport, cancel,
    // issue_requests). Paint uses the last plan + soft until the debounce fires.
    m_displayPipeline->scheduleTileLodAfterInteraction(50);
    viewport()->update(); // refresh viewport-space chrome at the new scale
    emit statusChanged();
    event->accept();
}
void ImageView::wheelEvent(QWheelEvent *event)
{
    if (m_gallery.tryWheelGalleryZoom(event) || m_gallery.tryWheelGalleryScroll(event)) {
        return;
    }
    wheelZoomViewAboutCursor(event);
}
void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (hostLayoutApply().active()) {
        return;
    }
    if (isImageMode() && !m_slideshow.hud().isProgressActive()) {
        m_displayPipeline->maybeClimbImageModePixelsForView();
    } else if (isWorkspaceMode()) {
        m_displayPipeline->ensureWorkspaceQualityClimb();
    }
    // Gallery: never repack from resize (session delete looked like auto-layout).
    // Still refresh the decode window: open often packs at 0×0, soft arrives
    // into ImageCache, and without this pulse cells stay blank until F5/relayout.
    if (isGalleryMode()) {
        if (viewport() && viewport()->width() > 1 && viewport()->height() > 1) {
            m_gallery.scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowRearmMs);
        }
        return;
    }
    // Dwell cover owns framing — never refit the underlay over it.
    // Invalidate atlas viewport keys so the next tick rebuilds at new size.
    if (m_slideshow.dwell().isMotionActive()) {
        m_slideshow.dwell().invalidateAtlasViewport();
        m_slideshow.zoomBlur().clearUnderlays();
        if (viewport()) {
            viewport()->update();
        }
        return;
    }
    if (m_framing.isFitMode() && m_items.size() == 1) {
        fitItem(m_items.first(), currentFitAspectMode());
    }
}
bool ImageView::tryMousePressZoomRegion(QMouseEvent *event)
{
    return m_image.tryMousePressZoomRegion(event);
}
bool ImageView::tryMousePressImageLink(QMouseEvent *event)
{
    if (!isImageMode() || m_cropCtrl.session().active() || m_attentionCtrl.session().active()
        || event->button() != Qt::LeftButton
        || event->modifiers() != Qt::NoModifier
        || !PagePath::isPageRef(m_image.classicPath())) {
        return false;
    }
    if (!m_textCtrl.session().hasLayerRegions() || m_textCtrl.session().layerPathRef() != m_image.classicPath()) {
        const bool hadShow = m_textCtrl.session().showsRegions();
        m_textCtrl.session().setShowRegions(true);
        refreshTextLayer();
        m_textCtrl.session().setShowRegions(hadShow);
    }
    int page = 0;
    QString uri;
    if (!hitTextLinkAt(event->pos(), &page, &uri)) {
        return false;
    }
    emit linkActivated(page, uri);
    event->accept();
    return true;
}
bool ImageView::tryMousePressPan(QMouseEvent *event)
{
    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (!m_slideshow.dwell().isMotionActive()
        && (event->button() == Qt::MiddleButton
            || (event->button() == Qt::LeftButton
                && ((isImageMode() && m_chrome.isImageModeLeftDragPan())
                    || (isWorkspaceMode() && m_workspace.currentTool() == Tool::Pan)
                    || (isGalleryMode() && (event->modifiers() & Qt::AltModifier))
                    || (event->modifiers() & Qt::AltModifier))))) {
        if (!(isWorkspaceMode() && (event->modifiers() & Qt::ShiftModifier)
              && event->button() == Qt::LeftButton)) {
            m_chrome.beginPan(event->pos());
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return true;
        }
    }
    if (event->button() == Qt::MiddleButton && !m_slideshow.dwell().isMotionActive()) {
        m_chrome.beginPan(event->pos());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::setHoverEdge(EdgeZone zone)
{
    return m_image.setHoverEdge(edgeZoneToPolicy(zone));
}
