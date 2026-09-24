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

    if (m_shell.viewport().setMouseInfo(info)) {
        emit mouseInfoChanged(m_shell.viewport().currentMouseInfo());
    }
}
void ImageView::wheelEvent(QWheelEvent *event)
{
    if (m_gallery.tryWheelGalleryZoom(event) || m_gallery.tryWheelGalleryScroll(event)) {
        return;
    }
    m_image.wheelZoomAboutCursor(event);
}
void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (hostLayoutApply().active()) {
        return;
    }
    if (isGalleryMode()) {
        m_gallery.onViewResized();
        return;
    }
    if (isWorkspaceMode()) {
        m_displayPipeline->ensureWorkspaceQualityClimb();
        return;
    }
    // Image mode
    if (m_slideshow.hud().isProgressActive()) {
        return;
    }
    if (m_slideshow.dwell().isMotionActive()) {
        m_slideshow.onViewResizedDuringDwell();
        return;
    }
    m_image.onViewResized();
}
bool ImageView::tryMousePressZoomRegion(QMouseEvent *event)
{
    return m_image.tryMousePressZoomRegion(event);
}
bool ImageView::tryMousePressImageLink(QMouseEvent *event)
{
    return m_textCtrl.tryMousePressLink(event);
}
bool ImageView::tryMousePressPan(QMouseEvent *event)
{
    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (!m_slideshow.dwell().isMotionActive()
        && (event->button() == Qt::MiddleButton
            || (event->button() == Qt::LeftButton
                && ((isImageMode() && m_shell.viewport().isImageModeLeftDragPan())
                    || (isWorkspaceMode() && m_workspace.currentTool() == Tool::Pan)
                    || (isGalleryMode() && (event->modifiers() & Qt::AltModifier))
                    || (event->modifiers() & Qt::AltModifier))))) {
        if (!(isWorkspaceMode() && (event->modifiers() & Qt::ShiftModifier)
              && event->button() == Qt::LeftButton)) {
            m_shell.viewport().beginPan(event->pos());
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return true;
        }
    }
    if (event->button() == Qt::MiddleButton && !m_slideshow.dwell().isMotionActive()) {
        m_shell.viewport().beginPan(event->pos());
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
