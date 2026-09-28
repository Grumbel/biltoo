// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "annotation/annotationcontroller.h"

#include "content/contentxform.h"
#include "host/thumtoocache.h"
#include "imageitem.h"
#include "imageview.h"
#include "session/sessionappearance.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

AnnotationController::AnnotationController(ImageView *view)
    : m_view(view)
{
}

void AnnotationController::setToolActive(bool on)
{
    if (m_toolActive == on) {
        return;
    }
    m_toolActive = on;
    m_drawing = false;
    m_draftPoints.clear();
    if (m_view) {
        m_view->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}

ImageItem *AnnotationController::targetItem() const
{
    if (!m_view || !m_view->isImageMode()) {
        return nullptr;
    }
    return m_view->primaryItem();
}

SessionImageId AnnotationController::targetSid(ImageItem *item) const
{
    if (!item) {
        return kInvalidSessionImageId;
    }
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && m_view) {
        sid = m_view->hostSessionId().currentIdValue();
    }
    return sid;
}

bool AnnotationController::pageSpaceForItem(ImageItem *item, QRectF *boundsOut,
                                            bool *yUpOut, QSize *sourceSizeOut) const
{
    if (!item || !m_view || !boundsOut || !yUpOut || !sourceSizeOut) {
        return false;
    }
    const QString path = item->path();
    QSize sourceSize = ThumtooCache::cachedSize(path);
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        sourceSize = m_view->hostSizeBook().known(path);
    }
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        sourceSize = item->imageSize();
    }
    if (sourceSize.width() < 1 || sourceSize.height() < 1) {
        return false;
    }
    *sourceSizeOut = sourceSize;

    // Prefer native text layer page box when present; else full source (Y-down).
    const auto layer = ThumtooCache::cachedPageTextLayer(path);
    if (layer.pageBounds.isValid() && layer.pageBounds.width() > 1
        && layer.pageBounds.height() > 1) {
        *boundsOut = layer.pageBounds;
        *yUpOut = layer.pageYUp;
    } else {
        *boundsOut = QRectF(0, 0, sourceSize.width(), sourceSize.height());
        *yUpOut = false;
    }
    return true;
}

QPointF AnnotationController::viewToPage(ImageItem *item, const QPoint &viewPos,
                                         const QRectF &pageBounds, bool pageYUp,
                                         const QSize &sourceSize) const
{
    if (!item || !m_view) {
        return {};
    }
    const QPointF local =
        item->mapFromScene(m_view->mapToScene(viewPos)) - item->offset();
    SessionImageId sid = targetSid(item);
    const WorkspaceItemState st =
        m_view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
    const ContentXform::Value x = ContentXform::Value::fromState(st);

    // Item local is display/crop-local pixels.
    const QSize itemSz = item->imageSize();
    QPointF disp = local;
    const QSize logical = ContentXform::layoutSize(sourceSize, x);
    if (logical.width() > 0 && logical.height() > 0 && itemSz.width() > 0
        && itemSz.height() > 0
        && (logical.width() != itemSz.width() || logical.height() != itemSz.height())) {
        disp = QPointF(local.x() * qreal(logical.width()) / qreal(itemSz.width()),
                       local.y() * qreal(logical.height()) / qreal(itemSz.height()));
    }
    const QRectF srcRect = ContentXform::mapDisplayRectToSource(
        QRectF(disp, QSizeF(1, 1)), sourceSize, x);
    if (srcRect.isEmpty()) {
        return {};
    }
    const QRectF pageRect = ThumtooCache::imageRectToPageRect(
        srcRect, pageBounds, sourceSize, pageYUp);
    return pageRect.center();
}

QPointF AnnotationController::pageToItemLocal(ImageItem *item, const QPointF &pagePt,
                                              const QRectF &pageBounds, bool pageYUp,
                                              const QSize &sourceSize) const
{
    if (!item || !m_view) {
        return {};
    }
    SessionImageId sid = targetSid(item);
    const WorkspaceItemState st =
        m_view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
    const ContentXform::Value x = ContentXform::Value::fromState(st);

    const QRectF pageRect(pagePt, QSizeF(0.01, 0.01));
    const QRectF inSource = ThumtooCache::pageRectToImageRect(
        pageRect, pageBounds, sourceSize, pageYUp);
    if (inSource.isEmpty()) {
        return {};
    }
    QRectF disp = ContentXform::mapSourceRectToDisplay(inSource, sourceSize, x);
    const QSize logical = ContentXform::layoutSize(sourceSize, x);
    const QSize itemSz = item->imageSize();
    if (logical.width() > 0 && logical.height() > 0 && itemSz.width() > 0
        && itemSz.height() > 0
        && (logical.width() != itemSz.width() || logical.height() != itemSz.height())) {
        disp = QRectF(
            disp.x() * qreal(itemSz.width()) / qreal(logical.width()),
            disp.y() * qreal(itemSz.height()) / qreal(logical.height()),
            disp.width() * qreal(itemSz.width()) / qreal(logical.width()),
            disp.height() * qreal(itemSz.height()) / qreal(logical.height()));
    }
    return disp.center() + item->offset();
}

void AnnotationController::paintStroke(QPainter &painter, ImageItem *item,
                                       const Annotation::Object &obj,
                                       const QRectF &pageBounds, bool pageYUp,
                                       const QSize &sourceSize) const
{
    if (!item || obj.points.size() < 1) {
        return;
    }
    QPainterPath path;
    bool first = true;
    for (const QPointF &pp : obj.points) {
        const QPointF local = pageToItemLocal(item, pp, pageBounds, pageYUp, sourceSize);
        const QPointF scene = item->mapToScene(local);
        const QPointF view = m_view->mapFromScene(scene);
        if (first) {
            path.moveTo(view);
            first = false;
        } else {
            path.lineTo(view);
        }
    }
    if (obj.points.size() == 1) {
        // Dot
        const QPointF local =
            pageToItemLocal(item, obj.points.first(), pageBounds, pageYUp, sourceSize);
        const QPointF view = m_view->mapFromScene(item->mapToScene(local));
        const qreal r = qMax(1.0, obj.width * 0.5 * m_view->transform().m11());
        painter.setPen(Qt::NoPen);
        painter.setBrush(obj.color);
        painter.drawEllipse(view, r, r);
        return;
    }

    QPen pen(obj.color);
    // Approximate page-space width in view pixels.
    const qreal scale = m_view->transform().m11();
    // Map a unit page step for rough scale
    qreal pageUnit = 1.0;
    if (pageBounds.width() > 1 && sourceSize.width() > 0) {
        pageUnit = qreal(sourceSize.width()) / pageBounds.width();
    }
    pen.setWidthF(qMax(1.0, obj.width * pageUnit * scale));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void AnnotationController::paintOverlay(QPainter &painter)
{
    if (!m_view || !m_session.isVisible()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item) {
        return;
    }
    SessionImageId sid = targetSid(item);
    QRectF bounds;
    bool yUp = false;
    QSize sourceSize;
    if (!pageSpaceForItem(item, &bounds, &yUp, &sourceSize)) {
        return;
    }

    painter.save();
    // Viewport device space (caller already resetTransform + DPR scale).

    auto drawPage = [&](const Annotation::Page *pg) {
        if (!pg) {
            return;
        }
        const QRectF pb = pg->pageBounds.isValid() ? pg->pageBounds : bounds;
        const bool py = pg->pageBounds.isValid() ? pg->pageYUp : yUp;
        for (const Annotation::Object &obj : pg->objects) {
            painter.save();
            if (obj.blend == Annotation::Blend::Multiply) {
                painter.setCompositionMode(QPainter::CompositionMode_Multiply);
            } else {
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
            paintStroke(painter, item, obj, pb, py, sourceSize);
            painter.restore();
        }
    };

    drawPage(m_session.page(sid));

    if (m_drawing && !m_draftPoints.isEmpty() && m_draftSid == sid) {
        Annotation::Object draft;
        draft.kind = Annotation::Kind::HighlighterStroke;
        draft.blend = Annotation::Blend::Multiply;
        draft.color = m_color;
        draft.width = m_width;
        draft.points = m_draftPoints;
        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        paintStroke(painter, item, draft, m_draftBounds.isValid() ? m_draftBounds : bounds,
                    m_draftYUp, m_draftSourceSize.isValid() ? m_draftSourceSize : sourceSize);
        painter.restore();
    }
    painter.restore();
}

bool AnnotationController::tryMousePress(QMouseEvent *event)
{
    if (!m_toolActive || !m_view || !event || event->button() != Qt::LeftButton) {
        return false;
    }
    if (!m_view->isImageMode()) {
        return false;
    }
    ImageItem *item = targetItem();
    if (!item) {
        return false;
    }
    QRectF bounds;
    bool yUp = false;
    QSize sourceSize;
    if (!pageSpaceForItem(item, &bounds, &yUp, &sourceSize)) {
        return false;
    }
    const QPointF pagePt = viewToPage(item, event->pos(), bounds, yUp, sourceSize);
    if (pagePt.isNull() && !bounds.contains(pagePt)) {
        // Still accept; null only if mapping failed completely
    }
    m_drawing = true;
    m_draftPoints.clear();
    m_draftPoints.append(pagePt);
    m_draftSid = targetSid(item);
    m_draftBounds = bounds;
    m_draftYUp = yUp;
    m_draftSourceSize = sourceSize;
    event->accept();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return true;
}

bool AnnotationController::tryMouseMove(QMouseEvent *event)
{
    if (!m_drawing || !m_toolActive || !event) {
        return false;
    }
    ImageItem *item = targetItem();
    if (!item) {
        return false;
    }
    const QPointF pagePt = viewToPage(item, event->pos(), m_draftBounds, m_draftYUp,
                                      m_draftSourceSize);
    if (!m_draftPoints.isEmpty()) {
        const QPointF &last = m_draftPoints.last();
        const qreal dx = pagePt.x() - last.x();
        const qreal dy = pagePt.y() - last.y();
        if (dx * dx + dy * dy < 0.25) {
            return true; // thin samples
        }
    }
    m_draftPoints.append(pagePt);
    event->accept();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return true;
}

bool AnnotationController::tryMouseRelease(QMouseEvent *event)
{
    if (!m_drawing) {
        return false;
    }
    if (event && event->button() != Qt::LeftButton) {
        return false;
    }
    m_drawing = false;
    if (m_draftPoints.size() >= 1 && m_draftSid != kInvalidSessionImageId) {
        Annotation::Object obj;
        obj.id = m_session.nextId();
        obj.kind = Annotation::Kind::HighlighterStroke;
        obj.blend = Annotation::Blend::Multiply;
        obj.color = m_color;
        obj.width = m_width;
        obj.points = m_draftPoints;
        m_session.addObject(m_draftSid, obj, m_draftBounds, m_draftYUp);
    }
    m_draftPoints.clear();
    if (event) {
        event->accept();
    }
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
    return true;
}

void AnnotationController::clearCurrentPage()
{
    ImageItem *item = targetItem();
    const SessionImageId sid = targetSid(item);
    if (sid != kInvalidSessionImageId) {
        m_session.clearPage(sid);
        if (m_view && m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}
