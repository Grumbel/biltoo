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

/**
 * Coordinate contract (docs/OCR_COORDINATES.md, docs/CONTENT_COORDINATES.md):
 *
 *   page ──pageRectToImageRect(pageYUp)──► source (unoriented full raster)
 *   source ──ContentXform──► display (crop-local)
 *   display + item->offset() ──mapToScene──► scene  (paint path)
 *
 * Stored annotation geometry is always page space. Live crop/orient/grade are
 * applied only when mapping for input or paint — never baked into points/quads.
 *
 * Paint runs in scene space (same as TextLayerController::paintSceneOverlays),
 * while the QPainter still has the view transform. Do not mix mapFromScene
 * view-pixel results into that painter without resetTransform.
 */

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
    SessionImageId sid = targetSid(item);
    const WorkspaceItemState want =
        m_view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
    const ContentXform::Value x = ContentXform::Value::fromState(want);

    // Unoriented full raster size (CONTENT_COORDINATES.md § recovering source size).
    QSize sourceSize = ThumtooCache::cachedSize(path);
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        sourceSize = m_view->hostSizeBook().known(path);
    }
    if (!sourceSize.isValid() || sourceSize.width() < 1 || sourceSize.height() < 1) {
        sourceSize = item->imageSize();
        if (ContentXform::swapsAspect(x) && !x.hasCrop) {
            sourceSize.transpose();
        }
    }
    if (sourceSize.width() < 1 || sourceSize.height() < 1) {
        return false;
    }
    *sourceSizeOut = sourceSize;

    // Prefer layer for *this* item path (not a stale primary from another page).
    m_view->hostText().ensureMemberLayers();
    if (const ThumtooCache::PageTextLayer *live = m_view->hostText().layerForItem(item)) {
        if (live->pageBounds.isValid() && live->pageBounds.width() > 1
            && live->pageBounds.height() > 1) {
            *boundsOut = live->pageBounds;
            *yUpOut = live->pageYUp;
            return true;
        }
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

/** Page → display (crop-local, no item offset). Same pipeline as regionImageRect. */
QRectF AnnotationController::pageRectToDisplay(ImageItem *item, const QRectF &pageRect,
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
    if (disp.isEmpty()) {
        return {};
    }
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
    return disp;
}

QPointF AnnotationController::viewToPage(ImageItem *item, const QPoint &viewPos,
                                         const QRectF &pageBounds, bool pageYUp,
                                         const QSize &sourceSize) const
{
    if (!item || !m_view) {
        return {};
    }
    // View → scene → item local → display (subtract offset) — inverse of paint.
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
        QRectF(disp.x(), disp.y(), 1.0, 1.0), sourceSize, x);
    if (srcRect.isEmpty()) {
        return {};
    }
    const QRectF pageRect =
        ThumtooCache::imageRectToPageRect(srcRect, pageBounds, sourceSize, pageYUp);
    return pageRect.center();
}

QPointF AnnotationController::pageToScene(ImageItem *item, const QPointF &pagePt,
                                          const QRectF &pageBounds, bool pageYUp,
                                          const QSize &sourceSize) const
{
    const QRectF disp = pageRectToDisplay(
        item, QRectF(pagePt.x(), pagePt.y(), 0.01, 0.01), pageBounds, pageYUp, sourceSize);
    if (disp.isEmpty()) {
        return {};
    }
    const QPointF local = disp.center() + item->offset();
    return item->mapToScene(local);
}

QRectF AnnotationController::pageRectToScene(ImageItem *item, const QRectF &pageRect,
                                             const QRectF &pageBounds, bool pageYUp,
                                             const QSize &sourceSize) const
{
    const QRectF disp = pageRectToDisplay(item, pageRect, pageBounds, pageYUp, sourceSize);
    if (disp.isEmpty()) {
        return {};
    }
    const QRectF local = disp.translated(item->offset());
    return item->mapToScene(local).boundingRect();
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
        const QPointF scene = pageToScene(item, pp, pageBounds, pageYUp, sourceSize);
        if (first) {
            path.moveTo(scene);
            first = false;
        } else {
            path.lineTo(scene);
        }
    }
    if (obj.points.size() == 1) {
        const QPointF scene =
            pageToScene(item, obj.points.first(), pageBounds, pageYUp, sourceSize);
        // Width in page units → scene via source scale and view transform.
        qreal pageUnit = 1.0;
        if (pageBounds.width() > 1 && sourceSize.width() > 0) {
            pageUnit = qreal(sourceSize.width()) / pageBounds.width();
        }
        const qreal r = qMax(0.5, obj.width * 0.5 * pageUnit);
        painter.setPen(Qt::NoPen);
        painter.setBrush(obj.color);
        painter.drawEllipse(scene, r, r);
        return;
    }

    QPen pen(obj.color);
    qreal pageUnit = 1.0;
    if (pageBounds.width() > 1 && sourceSize.width() > 0) {
        pageUnit = qreal(sourceSize.width()) / pageBounds.width();
    }
    // Stroke width in scene (source) units; view transform scales it on screen.
    pen.setWidthF(qMax(0.5, obj.width * pageUnit));
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
        const QRectF scene = pageRectToScene(item, q, pageBounds, pageYUp, sourceSize);
        if (!scene.isEmpty()) {
            painter.drawRect(scene);
        }
    }
}


void AnnotationController::applyHighlightBlend(QPainter &painter, const QColor &color) const
{
    // Viewport is software raster: Multiply darkens destination so black
    // text stays readable under yellow/green/cyan highlighters.
    Q_UNUSED(color);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    painter.setOpacity(1.0);
}

void AnnotationController::paintObject(QPainter &painter, ImageItem *item,
                                       const Annotation::Object &obj,
                                       const QRectF &pageBounds, bool pageYUp,
                                       const QSize &sourceSize) const
{
    painter.save();
    if (obj.blend == Annotation::Blend::Multiply) {
        applyHighlightBlend(painter, obj.color);
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setOpacity(1.0);
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

    // Scene-space paint (painter still has view transform) — match text overlays.
    if (const Annotation::Page *pg = m_session.page(sid)) {
        const QRectF pb = pg->pageBounds.isValid() ? pg->pageBounds : bounds;
        const bool py = pg->pageBounds.isValid() ? pg->pageYUp : yUp;
        for (const Annotation::Object &obj : pg->objects) {
            paintObject(painter, item, obj, pb, py, sourceSize);
        }
    }

    if (m_drawing && m_draftSid == sid) {
        if ((m_tool == Annotation::Tool::FreehandHighlighter
             || m_tool == Annotation::Tool::Pen)
            && !m_draftPoints.isEmpty()) {
            Annotation::Object draft;
            if (m_tool == Annotation::Tool::Pen) {
                draft.kind = Annotation::Kind::InkStroke;
                draft.blend = Annotation::Blend::SourceOver;
            } else {
                draft.kind = Annotation::Kind::HighlighterStroke;
                draft.blend = Annotation::Blend::Multiply;
            }
            draft.color = m_color;
            draft.width = m_width;
            draft.points = m_draftPoints;
            paintObject(painter, item, draft,
                        m_draftBounds.isValid() ? m_draftBounds : bounds, m_draftYUp,
                        m_draftSourceSize.isValid() ? m_draftSourceSize : sourceSize);
        } else if (m_tool == Annotation::Tool::TextHighlighter && !m_rubberView.isEmpty()) {
            // Rubber is view-space; map to scene for the scene-space painter.
            const QRectF sceneRubber =
                m_view->mapToScene(m_rubberView).boundingRect();
            painter.save();
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            QColor c = m_color;
            c.setAlpha(80);
            painter.fillRect(sceneRubber, c);
            painter.setPen(QPen(m_color, 0 /* cosmetic */, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(sceneRubber);
            painter.restore();
        }
    }
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
        stack->push(cmd);
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
    if (m_tool == Annotation::Tool::Pen) {
        obj.kind = Annotation::Kind::InkStroke;
        obj.blend = Annotation::Blend::SourceOver;
    } else {
        obj.kind = Annotation::Kind::HighlighterStroke;
        obj.blend = Annotation::Blend::Multiply;
    }
    obj.color = m_color;
    obj.width = m_width;
    QVector<QPointF> clean;
    clean.reserve(m_draftPoints.size());
    for (const QPointF &p : m_draftPoints) {
        if (qIsFinite(p.x()) && qIsFinite(p.y())) {
            clean.append(p);
        }
    }
    if (clean.isEmpty()) {
        return;
    }
    const qreal eps = qMax(0.4, m_width * 0.04);
    obj.points = Annotation::simplifyPolyline(clean, eps);
    if (obj.points.size() < 1) {
        obj.points = clean;
    }
    commitObject(m_draftSid, obj, m_draftBounds, m_draftYUp,
                 m_tool == Annotation::Tool::Pen ? QObject::tr("Ink stroke")
                                                 : QObject::tr("Highlight stroke"));
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
    // Bind text layer to *this* underlay (not a stale primary / other page).
    m_view->hostText().ensureMemberLayers();
    const ThumtooCache::PageTextLayer *layerPtr = m_view->hostText().layerForItem(item);
    if (!layerPtr || layerPtr->regions.isEmpty()) {
        m_view->hostText().refresh();
        m_view->hostText().ensureMemberLayers();
        layerPtr = m_view->hostText().layerForItem(item);
    }
    // Cache fallback by path if controller bag still empty.
    ThumtooCache::PageTextLayer cachedLayer;
    const QString path = m_view->hostText().pathForItem(item);
    if ((!layerPtr || layerPtr->regions.isEmpty()) && !path.isEmpty()) {
        cachedLayer = ThumtooCache::cachedPageTextLayer(path);
        if (!cachedLayer.regions.isEmpty()) {
            layerPtr = &cachedLayer;
        }
    }
    if (!layerPtr || layerPtr->regions.isEmpty()) {
        return;
    }
    const ThumtooCache::PageTextLayer &layer = *layerPtr;

    QVector<QRectF> regionRects;
    regionRects.reserve(layer.regions.size());
    for (const auto &r : layer.regions) {
        regionRects.append(m_view->hostText().regionImageRectFor(item, path, layer, r));
    }
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

    if (m_tool == Annotation::Tool::FreehandHighlighter
        || m_tool == Annotation::Tool::Pen) {
        m_draftPoints.append(viewToPage(item, event->pos(), bounds, yUp, sourceSize));
    } else if (m_tool == Annotation::Tool::TextHighlighter) {
        m_rubberOriginView = event->pos();
        m_rubberView = QRect(m_rubberOriginView, QSize(1, 1));
    } else if (m_tool == Annotation::Tool::Eraser) {
        eraseAtPagePoint(viewToPage(item, event->pos(), bounds, yUp, sourceSize),
                         bounds, yUp);
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
    if (m_tool == Annotation::Tool::FreehandHighlighter
        || m_tool == Annotation::Tool::Pen) {
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
    } else if (m_tool == Annotation::Tool::Eraser) {
        eraseAtPagePoint(
            viewToPage(item, event->pos(), m_draftBounds, m_draftYUp, m_draftSourceSize),
            m_draftBounds, m_draftYUp);
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
    if (m_tool == Annotation::Tool::FreehandHighlighter
        || m_tool == Annotation::Tool::Pen) {
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

static qreal dist2PointSeg(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal ab2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (!(ab2 > 1e-12)) {
        const QPointF d = p - a;
        return d.x() * d.x() + d.y() * d.y();
    }
    qreal t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / ab2;
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    const QPointF proj(a.x() + t * ab.x(), a.y() + t * ab.y());
    const QPointF d = p - proj;
    return d.x() * d.x() + d.y() * d.y();
}

static bool objectHitsPagePoint(const Annotation::Object &o, const QPointF &pagePt,
                                qreal radius)
{
    if (o.kind == Annotation::Kind::HighlightQuad) {
        for (const QRectF &q : o.quads) {
            if (q.adjusted(-radius, -radius, radius, radius).contains(pagePt)) {
                return true;
            }
        }
        return false;
    }
    if (o.points.isEmpty()) {
        return false;
    }
    const qreal strokeR = qMax(radius, o.width * 0.5 + 2.0);
    const qreal s2 = strokeR * strokeR;
    if (o.points.size() == 1) {
        const QPointF d = pagePt - o.points.first();
        return d.x() * d.x() + d.y() * d.y() <= s2;
    }
    for (int i = 1; i < o.points.size(); ++i) {
        if (dist2PointSeg(pagePt, o.points.at(i - 1), o.points.at(i)) <= s2) {
            return true;
        }
    }
    return false;
}

void AnnotationController::eraseAtPagePoint(const QPointF &pagePt, const QRectF &pageBounds,
                                            bool pageYUp)
{
    if (m_draftSid == kInvalidSessionImageId || !m_view) {
        return;
    }
    Annotation::Page *pg = m_session.page(m_draftSid);
    if (!pg || pg->objects.isEmpty()) {
        return;
    }
    const qreal radius = qMax(8.0, m_width * 0.6);
    QVector<Annotation::Object> hit;
    for (const Annotation::Object &o : pg->objects) {
        if (objectHitsPagePoint(o, pagePt, radius)) {
            hit.append(o);
        }
    }
    if (hit.isEmpty()) {
        return;
    }
    const QRectF bounds = pageBounds.isValid() ? pageBounds : pg->pageBounds;
    const bool yUp = pageBounds.isValid() ? pageYUp : pg->pageYUp;
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        auto *cmd = new AnnotationRemoveCommand(
            m_view, m_draftSid, hit, bounds, yUp,
            hit.size() == 1 ? QObject::tr("Erase annotation")
                            : QObject::tr("Erase annotations"));
        stack->push(cmd);
    } else {
        for (const Annotation::Object &o : hit) {
            m_session.removeObject(m_draftSid, o.id);
        }
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}

void AnnotationController::clearCurrentPage()
{
    ImageItem *item = targetItem();
    const SessionImageId sid = targetSid(item);
    if (sid == kInvalidSessionImageId || !m_view) {
        return;
    }
    Annotation::Page *pg = m_session.page(sid);
    if (!pg || pg->objects.isEmpty()) {
        return;
    }
    const QVector<Annotation::Object> all = pg->objects;
    const QRectF bounds = pg->pageBounds;
    const bool yUp = pg->pageYUp;
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        auto *cmd = new AnnotationRemoveCommand(
            m_view, sid, all, bounds, yUp, QObject::tr("Clear page annotations"));
        stack->push(cmd);
    } else {
        m_session.clearPage(sid);
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}
