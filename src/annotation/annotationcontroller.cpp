// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "annotation/annotationcontroller.h"

#include "annotation/annotationcommand.h"
#include "content/contentxform.h"
#include "host/thumtoocache.h"
#include "imageitem.h"
#include "imageview.h"
#include "session/sessionappearance.h"
#include "text/textlayergeometry.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUndoStack>
#include <QtMath>

AnnotationController::AnnotationController(ImageView *view)
    : m_view(view)
{
}

void AnnotationController::setTool(Annotation::Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    m_drawing = false;
    m_draftPoints.clear();
    m_rubberView = {};
    if (m_view) {
        if (m_tool == Annotation::Tool::None) {
            m_view->restoreToolCursor();
        } else {
            m_view->setCursor(Qt::CrossCursor);
        }
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}

void AnnotationController::setToolActive(bool on)
{
    setTool(on ? Annotation::Tool::FreehandHighlighter : Annotation::Tool::None);
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

    // Prefer live text-layer page box when the controller has one for this path.
    if (m_view->hostText().session().hasLayerRegions()
        && m_view->hostText().session().pageBoundsValid()) {
        *boundsOut = m_view->hostText().session().pageBounds();
        *yUpOut = m_view->hostText().pageYUp();
        return true;
    }

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

QRectF AnnotationController::pageRectToView(ImageItem *item, const QRectF &pageRect,
                                            const QRectF &pageBounds, bool pageYUp,
                                            const QSize &sourceSize) const
{
    if (!item || !m_view || pageRect.isEmpty()) {
        return {};
    }
    SessionImageId sid = targetSid(item);
    const WorkspaceItemState st =
        m_view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
    const ContentXform::Value x = ContentXform::Value::fromState(st);

    const QRectF inSource =
        ThumtooCache::pageRectToImageRect(pageRect, pageBounds, sourceSize, pageYUp);
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
    disp.translate(item->offset());
    const QRectF scene = item->mapToScene(disp).boundingRect();
    return m_view->mapFromScene(scene).boundingRect();
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
        const QPointF view = m_view->mapFromScene(item->mapToScene(local));
        if (first) {
            path.moveTo(view);
            first = false;
        } else {
            path.lineTo(view);
        }
    }
    if (obj.points.size() == 1) {
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
    const qreal scale = m_view->transform().m11();
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

void AnnotationController::paintQuads(QPainter &painter, ImageItem *item,
                                      const Annotation::Object &obj,
                                      const QRectF &pageBounds, bool pageYUp,
                                      const QSize &sourceSize) const
{
    if (!item || obj.quads.isEmpty()) {
        return;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(obj.color);
    for (const QRectF &q : obj.quads) {
        const QRectF view = pageRectToView(item, q, pageBounds, pageYUp, sourceSize);
        if (!view.isEmpty()) {
            painter.drawRect(view);
        }
    }
}

void AnnotationController::paintObject(QPainter &painter, ImageItem *item,
                                       const Annotation::Object &obj,
                                       const QRectF &pageBounds, bool pageYUp,
                                       const QSize &sourceSize) const
{
    painter.save();
    if (obj.blend == Annotation::Blend::Multiply) {
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    if (obj.kind == Annotation::Kind::HighlightQuad) {
        paintQuads(painter, item, obj, pageBounds, pageYUp, sourceSize);
    } else {
        paintStroke(painter, item, obj, pageBounds, pageYUp, sourceSize);
    }
    painter.restore();
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

    if (const Annotation::Page *pg = m_session.page(sid)) {
        const QRectF pb = pg->pageBounds.isValid() ? pg->pageBounds : bounds;
        const bool py = pg->pageBounds.isValid() ? pg->pageYUp : yUp;
        for (const Annotation::Object &obj : pg->objects) {
            paintObject(painter, item, obj, pb, py, sourceSize);
        }
    }

    if (m_drawing && m_draftSid == sid) {
        if (m_tool == Annotation::Tool::FreehandHighlighter && !m_draftPoints.isEmpty()) {
            Annotation::Object draft;
            draft.kind = Annotation::Kind::HighlighterStroke;
            draft.blend = Annotation::Blend::Multiply;
            draft.color = m_color;
            draft.width = m_width;
            draft.points = m_draftPoints;
            paintObject(painter, item, draft,
                        m_draftBounds.isValid() ? m_draftBounds : bounds, m_draftYUp,
                        m_draftSourceSize.isValid() ? m_draftSourceSize : sourceSize);
        } else if (m_tool == Annotation::Tool::TextHighlighter && !m_rubberView.isEmpty()) {
            painter.save();
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            QColor c = m_color;
            c.setAlpha(80);
            painter.fillRect(m_rubberView, c);
            painter.setPen(QPen(m_color, 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(m_rubberView);
            painter.restore();
        }
    }
    painter.restore();
}

void AnnotationController::commitObject(SessionImageId sid, const Annotation::Object &obj,
                                        const QRectF &pageBounds, bool pageYUp,
                                        const QString &undoText)
{
    if (!m_view || sid == kInvalidSessionImageId) {
        return;
    }
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        auto *cmd = new AnnotationAddCommand(m_view, sid, obj, pageBounds, pageYUp, undoText);
        stack->push(cmd); // push calls redo()
    } else {
        m_session.addObject(sid, obj, pageBounds, pageYUp);
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}

void AnnotationController::finishFreehand()
{
    if (m_draftPoints.isEmpty() || m_draftSid == kInvalidSessionImageId) {
        return;
    }
    Annotation::Object obj;
    obj.id = m_session.nextId();
    obj.kind = Annotation::Kind::HighlighterStroke;
    obj.blend = Annotation::Blend::Multiply;
    obj.color = m_color;
    obj.width = m_width;
    // ~0.5 page unit epsilon; for pixel pages that is half a pixel at 1:1
    const qreal eps = qMax(0.4, m_width * 0.04);
    obj.points = Annotation::simplifyPolyline(m_draftPoints, eps);
    if (obj.points.isEmpty()) {
        obj.points = m_draftPoints;
    }
    commitObject(m_draftSid, obj, m_draftBounds, m_draftYUp,
                 QObject::tr("Highlight stroke"));
}

void AnnotationController::finishTextHighlight()
{
    if (!m_view || m_draftSid == kInvalidSessionImageId || m_rubberView.isEmpty()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item) {
        return;
    }
    // Ensure text layer is loaded for hit-test.
    if (!m_view->hostText().session().hasRegions()) {
        m_view->hostText().refresh();
    }
    const auto &layer = m_view->hostText().session().layerRef();
    if (layer.regions.isEmpty()) {
        return;
    }

    // Build region rects in *image/display* space (same as rubber from text controller).
    QVector<QRectF> regionRects;
    regionRects.reserve(layer.regions.size());
    for (const auto &r : layer.regions) {
        regionRects.append(m_view->hostText().regionImageRect(r));
    }
    // Rubber in item image coords
    const QRectF sceneRect = m_view->mapToScene(m_rubberView).boundingRect();
    const QRectF local = item->mapFromScene(sceneRect).boundingRect();
    const QRectF imageRubber = local.translated(-item->offset());

    const QVector<int> hits =
        TextLayerGeometry::indicesIntersecting(regionRects, imageRubber);
    if (hits.isEmpty()) {
        return;
    }

    Annotation::Object obj;
    obj.id = m_session.nextId();
    obj.kind = Annotation::Kind::HighlightQuad;
    obj.blend = Annotation::Blend::Multiply;
    obj.color = m_color;
    QStringList snippets;
    for (int idx : hits) {
        if (idx < 0 || idx >= layer.regions.size()) {
            continue;
        }
        const auto &reg = layer.regions.at(idx);
        if (!reg.bbox.isEmpty()) {
            obj.quads.append(reg.bbox.normalized());
        }
        if (!reg.text.isEmpty()) {
            snippets.append(reg.text);
        }
    }
    if (obj.quads.isEmpty()) {
        return;
    }
    obj.textSnippet = snippets.join(QLatin1Char(' '));
    if (obj.textSnippet.size() > 200) {
        obj.textSnippet = obj.textSnippet.left(197) + QStringLiteral("…");
    }

    QRectF bounds = m_draftBounds;
    bool yUp = m_draftYUp;
    if (layer.pageBounds.isValid()) {
        bounds = layer.pageBounds;
        yUp = layer.pageYUp;
    }
    commitObject(m_draftSid, obj, bounds, yUp, QObject::tr("Text highlight"));
}

bool AnnotationController::tryMousePress(QMouseEvent *event)
{
    if (!isToolActive() || !m_view || !event || event->button() != Qt::LeftButton) {
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
    m_drawing = true;
    m_draftSid = targetSid(item);
    m_draftBounds = bounds;
    m_draftYUp = yUp;
    m_draftSourceSize = sourceSize;
    m_draftPoints.clear();
    m_rubberView = {};

    if (m_tool == Annotation::Tool::FreehandHighlighter) {
        m_draftPoints.append(viewToPage(item, event->pos(), bounds, yUp, sourceSize));
    } else if (m_tool == Annotation::Tool::TextHighlighter) {
        m_rubberOriginView = event->pos();
        m_rubberView = QRect(m_rubberOriginView, QSize(1, 1));
    }
    event->accept();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return true;
}

bool AnnotationController::tryMouseMove(QMouseEvent *event)
{
    if (!m_drawing || !isToolActive() || !event) {
        return false;
    }
    ImageItem *item = targetItem();
    if (!item) {
        return false;
    }
    if (m_tool == Annotation::Tool::FreehandHighlighter) {
        const QPointF pagePt =
            viewToPage(item, event->pos(), m_draftBounds, m_draftYUp, m_draftSourceSize);
        if (!m_draftPoints.isEmpty()) {
            const QPointF &last = m_draftPoints.last();
            const qreal dx = pagePt.x() - last.x();
            const qreal dy = pagePt.y() - last.y();
            if (dx * dx + dy * dy < 0.25) {
                return true;
            }
        }
        m_draftPoints.append(pagePt);
    } else if (m_tool == Annotation::Tool::TextHighlighter) {
        m_rubberView = QRect(m_rubberOriginView, event->pos()).normalized();
    }
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
    if (m_tool == Annotation::Tool::FreehandHighlighter) {
        finishFreehand();
    } else if (m_tool == Annotation::Tool::TextHighlighter) {
        finishTextHighlight();
    }
    m_draftPoints.clear();
    m_rubberView = {};
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
