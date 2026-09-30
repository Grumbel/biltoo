// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "annotation/annotationcontroller.h"

#include "annotation/annotationcommand.h"
#include "annotation/annotationpainter.h"
#include "content/contentxform.h"
#include "host/thumtoocache.h"
#include "imageitem.h"
#include "imageview.h"
#include "session/sessionappearance.h"
#include "text/textlayergeometry.h"
#include "text/textlayercontroller.h"
#include "text/textselection.h"

#include <QHash>
#include <QSet>

#include <QMouseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QPainter>
#include <QPainterPath>
#include <QUndoStack>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include "image/toolcursors.h"
#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsItem>

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


namespace {

QRectF unionOfQuads(const QVector<QRectF> &quads)
{
    QRectF u;
    for (const QRectF &q : quads) {
        if (!q.isValid()) {
            continue;
        }
        u = u.isValid() ? u.united(q) : q;
    }
    return u;
}

QRectF mapRectThroughUnions(const QRectF &r, const QRectF &fromU, const QRectF &toU)
{
    if (!fromU.isValid() || fromU.width() < 1e-6 || fromU.height() < 1e-6 || !toU.isValid()) {
        return r;
    }
    const qreal nx = (r.x() - fromU.x()) / fromU.width();
    const qreal ny = (r.y() - fromU.y()) / fromU.height();
    const qreal nw = r.width() / fromU.width();
    const qreal nh = r.height() / fromU.height();
    return QRectF(toU.x() + nx * toU.width(), toU.y() + ny * toU.height(),
                  qMax(1.0, nw * toU.width()), qMax(1.0, nh * toU.height()));
}

/**
 * Rectangle / ellipse rubber in viewport pixels (Photoshop / Inkscape-style):
 * - Shift: equal sides (square / circle)
 * - Alt: drag from centre (origin is the centre)
 * - Shift+Alt: both
 */
QRect constrainedShapeRubber(const QPoint &origin, const QPoint &current,
                             Qt::KeyboardModifiers mods)
{
    QPoint end = current;
    if (mods.testFlag(Qt::ShiftModifier)) {
        const int dx = end.x() - origin.x();
        const int dy = end.y() - origin.y();
        const int side = qMax(qAbs(dx), qAbs(dy));
        end = QPoint(origin.x() + (dx < 0 ? -side : side),
                     origin.y() + (dy < 0 ? -side : side));
    }
    if (mods.testFlag(Qt::AltModifier)) {
        const int dx = end.x() - origin.x();
        const int dy = end.y() - origin.y();
        return QRect(QPoint(origin.x() - dx, origin.y() - dy), end).normalized();
    }
    return QRect(origin, end).normalized();
}

/**
 * Line endpoint in viewport pixels:
 * - Shift: snap angle to nearest 45° (0, 45, 90, …)
 */
QPoint constrainedLineEnd(const QPoint &origin, const QPoint &current,
                          Qt::KeyboardModifiers mods)
{
    if (!mods.testFlag(Qt::ShiftModifier)) {
        return current;
    }
    const qreal dx = qreal(current.x() - origin.x());
    const qreal dy = qreal(current.y() - origin.y());
    const qreal len = qSqrt(dx * dx + dy * dy);
    if (len < 1e-3) {
        return current;
    }
    const qreal ang = qAtan2(dy, dx);
    constexpr qreal kStep = M_PI / 4.0; // 45°
    const qreal snapped = qRound(ang / kStep) * kStep;
    return QPoint(origin.x() + int(qRound(qCos(snapped) * len)),
                  origin.y() + int(qRound(qSin(snapped) * len)));
}

} // namespace

AnnotationController::AnnotationController(ImageView *view)
    : m_view(view)
{
    QSettings s;
    s.beginGroup(QStringLiteral("annotation"));
    const QString colorName = s.value(QStringLiteral("color")).toString();
    if (!colorName.isEmpty()) {
        const QColor c(colorName);
        if (c.isValid()) {
            m_color = c;
        }
    }
    const qreal w = s.value(QStringLiteral("width"), m_width).toDouble();
    if (w >= 1.0) {
        m_width = w;
    }
    m_session.setVisible(s.value(QStringLiteral("layerVisible"), true).toBool());
    s.endGroup();
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
    m_shapeEndView = {};
    if (m_resizing) {
        cancelResizeSelection();
    }
    if (m_moving) {
        cancelMoveSelection();
    }
    if (m_tool != Annotation::Tool::Select) {
        m_selectedIds.clear();
    }
    if (m_view) {
        if (m_tool != Annotation::Tool::None) {
            // Drop any lingering edge-nav hover so chevrons do not stick under tools.
            m_view->hostImage().clearHoverEdge();
        }
        // Always go through restoreToolCursor so view + viewport stay in sync.
        m_view->restoreToolCursor();
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}

void AnnotationController::setToolActive(bool on)
{
    setTool(on ? Annotation::Tool::FreehandHighlighter : Annotation::Tool::None);
}

void AnnotationController::setColor(const QColor &c)
{
    if (!c.isValid()) {
        return;
    }
    m_color = c;
    QSettings s;
    s.beginGroup(QStringLiteral("annotation"));
    s.setValue(QStringLiteral("color"), m_color.name(QColor::HexArgb));
    s.endGroup();
}

void AnnotationController::setWidth(qreal w)
{
    m_width = qMax(1.0, w);
    QSettings s;
    s.beginGroup(QStringLiteral("annotation"));
    s.setValue(QStringLiteral("width"), m_width);
    s.endGroup();
}

void AnnotationController::setLayerVisible(bool on)
{
    if (m_session.isVisible() == on) {
        return;
    }
    m_session.setVisible(on);
    QSettings s;
    s.beginGroup(QStringLiteral("annotation"));
    s.setValue(QStringLiteral("layerVisible"), on);
    s.endGroup();
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

ImageItem *AnnotationController::targetItem() const
{
    // Session cursor / primary underlay. Hit-testing uses itemAtViewPos;
    // in-progress tools use itemForDraftSid.
    return m_view ? m_view->primaryItem() : nullptr;
}

ImageItem *AnnotationController::itemAtViewPos(const QPoint &viewPos) const
{
    if (!m_view) {
        return nullptr;
    }
    // Single-page Image: session cursor underlay. Multi-underlay Image
    // (Double View / spread) shares Gallery/Workspace scene hit-test so the
    // second page is not forced to primaryItem().
    if (m_view->isImageMode() && m_view->liveItems().size() <= 1) {
        return targetItem();
    }
    // Top-most ImageItem under the cursor (Gallery / Workspace / spread).
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return nullptr;
    }
    const QPointF scenePt = m_view->mapToScene(viewPos);
    const QList<QGraphicsItem *> hits = scene->items(scenePt, Qt::IntersectsItemShape,
                                                     Qt::DescendingOrder, m_view->transform());
    for (QGraphicsItem *gi : hits) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (m_view->liveItems().contains(ii)) {
                return ii;
            }
        }
    }
    return nullptr;
}

ImageItem *AnnotationController::itemForDraftSid() const
{
    if (!m_view || m_draftSid == kInvalidSessionImageId) {
        return targetItem();
    }
    for (ImageItem *it : m_view->liveItems()) {
        if (it && it->sessionId() == m_draftSid) {
            return it;
        }
    }
    return targetItem();
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

QPointF AnnotationController::viewToPage(ImageItem *item, const QPoint &viewPos,
                                         const QRectF &pageBounds, bool pageYUp,
                                         const QSize &sourceSize) const
{
    if (!item || !m_view || sourceSize.width() < 1 || sourceSize.height() < 1) {
        return {};
    }
    // One pipeline (inverse of paint):
    //   view → scene → item local → logical display → source → page
    // Content orient (flip + quarter turns + crop) is only in ContentXform —
    // no separate rotate/flip branches. Item placement (spread scale, Workspace
    // pose) is handled by mapFromScene.
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
    const QPointF srcPt = ContentXform::mapDisplayPointToSource(disp, sourceSize, x);
    if (!qIsFinite(srcPt.x()) || !qIsFinite(srcPt.y())) {
        return {};
    }
    const QRectF pageRect = ThumtooCache::imageRectToPageRect(
        QRectF(srcPt.x(), srcPt.y(), 1e-3, 1e-3), pageBounds, sourceSize, pageYUp);
    return pageRect.center();
}









QString AnnotationController::pathForSid(SessionImageId sid) const
{
    if (!m_view || sid == kInvalidSessionImageId) {
        return {};
    }
    for (ImageItem *it : m_view->liveItems()) {
        if (it && it->sessionId() == sid && !it->path().isEmpty()) {
            return it->path();
        }
    }
    if (ImageItem *primary = m_view->primaryItem()) {
        if (primary->sessionId() == sid) {
            return primary->path();
        }
    }
    return {};
}

void AnnotationController::persistPageForSid(SessionImageId sid)
{
    const QString path = pathForSid(sid);
    if (path.isEmpty()) {
        return;
    }
    const Annotation::Page *pg = m_session.page(sid);
    if (!pg || pg->objects.isEmpty()) {
        ThumtooCache::clearLocatorAnnotationJson(path);
        return;
    }
    Annotation::Page copy = *pg;
    copy.sid = kInvalidSessionImageId;
    const QByteArray bytes =
        QJsonDocument(Annotation::pageToJson(copy)).toJson(QJsonDocument::Compact);
    ThumtooCache::saveLocatorAnnotationJson(path, bytes);
}

void AnnotationController::ensureHydrated(ImageItem *item)
{
    if (!item || !m_view) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    if (m_hydratedSids.contains(sid)) {
        return;
    }
    m_hydratedSids.insert(sid);
    if (const Annotation::Page *existing = m_session.page(sid)) {
        if (!existing->objects.isEmpty()) {
            return;
        }
    }
    const QString path = item->path();
    if (path.isEmpty()) {
        return;
    }
    QByteArray json;
    if (!ThumtooCache::loadLocatorAnnotationJson(path, &json) || json.isEmpty()) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return;
    }
    Annotation::Page pg = Annotation::pageFromJson(doc.object());
    pg.sid = sid;
    if (pg.objects.isEmpty()) {
        return;
    }
    m_session.putPage(pg);
}

void AnnotationController::paintItemAnnotations(QPainter &painter, ImageItem *item,
                                                   bool selectionChrome)
{
    if (!item || !m_view) {
        return;
    }
    ensureHydrated(item);
    // Gallery/Workspace tiles must use the item's own sid — never the Image-mode
    // session cursor (targetSid would fall back to current and mis-paint).
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
                                   ? item->sessionId()
                                   : targetSid(item);
    if (sid == kInvalidSessionImageId) {
        return;
    }
    const Annotation::Page *pg = m_session.page(sid);
    if (!pg || pg->objects.isEmpty()) {
        return;
    }

    QRectF bounds;
    bool yUp = false;
    QSize sourceSize;
    if (!pageSpaceForItem(item, &bounds, &yUp, &sourceSize)) {
        return;
    }
    const QRectF pb = pg->pageBounds.isValid() ? pg->pageBounds : bounds;
    const bool py = pg->pageBounds.isValid() ? pg->pageYUp : yUp;
    AnnotationPainter::paintPageObjects(painter, m_view, item, *pg, bounds, yUp,
                                        sourceSize);
    if (selectionChrome) {
        AnnotationPainter::paintSelectionChrome(painter, m_view, item, *pg, pb, py,
                                                sourceSize, m_selectedIds);
    }
}

void AnnotationController::paintDraftChrome(QPainter &painter, ImageItem *item)
{
    if (!m_drawing || !item || !m_view || m_draftSid == kInvalidSessionImageId) {
        return;
    }
    if (targetSid(item) != m_draftSid) {
        return;
    }
    if ((m_tool == Annotation::Tool::FreehandHighlighter
         || m_tool == Annotation::Tool::Pen)
        && !m_draftPoints.isEmpty() && m_draftBounds.isValid()
        && m_draftSourceSize.isValid()) {
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
        AnnotationPainter::paintObject(painter, m_view, item, draft, m_draftBounds,
                                       m_draftYUp, m_draftSourceSize);
        return;
    }
    if ((m_tool == Annotation::Tool::TextHighlighter
         || m_tool == Annotation::Tool::Rect
         || m_tool == Annotation::Tool::Ellipse
         || m_tool == Annotation::Tool::Line)
        && (m_tool == Annotation::Tool::Line || !m_rubberView.isEmpty())) {
        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        if (m_tool == Annotation::Tool::Line) {
            QPen pen(m_color);
            pen.setWidthF(0);
            pen.setCosmetic(true);
            painter.setPen(pen);
            painter.drawLine(m_view->mapToScene(m_rubberOriginView),
                             m_view->mapToScene(m_shapeEndView));
        } else {
            const QRectF sceneRubber =
                m_view->mapToScene(m_rubberView).boundingRect();
            if (m_tool == Annotation::Tool::TextHighlighter) {
                QColor c = m_color;
                c.setAlpha(80);
                painter.fillRect(sceneRubber, c);
                painter.setPen(QPen(m_color, 0, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(sceneRubber);
            } else {
                QPen pen(m_color);
                pen.setWidthF(0);
                pen.setCosmetic(true);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                if (m_tool == Annotation::Tool::Ellipse) {
                    painter.drawEllipse(sceneRubber);
                } else {
                    painter.drawRect(sceneRubber);
                }
            }
        }
        painter.restore();
    }
}

void AnnotationController::paintOverlay(QPainter &painter)
{
    if (!m_view || !m_session.isVisible()) {
        return;
    }

    // One path for Image (single + Double View / spread), Gallery, Workspace:
    // committed marks on every live underlay. Selection chrome is a no-op when
    // selectedIds is empty (see AnnotationPainter::paintSelectionChrome).
    // Draft stroke/rubber is drawn once on the draft item.
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->sessionId() == kInvalidSessionImageId) {
            continue;
        }
        paintItemAnnotations(painter, item, true);
    }
    if (m_drawing) {
        if (ImageItem *di = itemForDraftSid()) {
            paintDraftChrome(painter, di);
        }
    }
}

void AnnotationController::recordPageSourceKey(SessionImageId sid, const QRectF &pageBounds,
                                               bool pageYUp)
{
    if (!m_view || sid == kInvalidSessionImageId) {
        return;
    }
    ImageItem *item = nullptr;
    for (ImageItem *it : m_view->liveItems()) {
        if (it && it->sessionId() == sid) {
            item = it;
            break;
        }
    }
    if (!item) {
        item = targetItem();
    }
    if (!item) {
        return;
    }
    const QString path = item->path();
    if (path.isEmpty()) {
        return;
    }
    m_session.setPageSourceKey(sid, path, pageBounds, pageYUp);
}

void AnnotationController::commitObject(SessionImageId sid, const Annotation::Object &obj,
                                        const QRectF &pageBounds, bool pageYUp,
                                        const QString &undoText)
{
    if (!m_view || sid == kInvalidSessionImageId) {
        return;
    }
    recordPageSourceKey(sid, pageBounds, pageYUp);
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        auto *cmd = new AnnotationAddCommand(m_view, sid, obj, pageBounds, pageYUp, undoText);
        stack->push(cmd);
    } else {
        m_session.addObject(sid, obj, pageBounds, pageYUp);
        persistPageForSid(sid);
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
    ImageItem *item = itemForDraftSid();
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

int AnnotationController::markTextSelection()
{
    if (!m_view) {
        return 0;
    }
    TextLayerController &text = m_view->hostText();
    text.ensureMemberLayers();
    const TextLayerSession &ts = text.session();

    // Prefer multi-page bag; else primary selectedRegions on current session.
    QVector<TextSelRef> refs;
    if (!ts.multiSelection.isEmpty()) {
        refs = ts.multiSelection.refs();
    } else if (!ts.selectedRegions.isEmpty()) {
        SessionImageId sid = kInvalidSessionImageId;
        if (ImageItem *primary = m_view->primaryItem()) {
            sid = targetSid(primary);
        }
        if (sid == kInvalidSessionImageId) {
            sid = m_view->hostSessionId().currentIdValue();
        }
        if (sid == kInvalidSessionImageId) {
            return 0;
        }
        const ThumtooCache::PageTextLayer &layer = ts.layer;
        for (int idx : ts.selectedRegions) {
            TextSelRef r;
            r.sessionId = sid;
            r.regionIndex = idx;
            if (idx >= 0 && idx < layer.regions.size()) {
                r.text = layer.regions.at(idx).text;
            }
            refs.append(r);
        }
    }
    if (refs.isEmpty()) {
        return 0;
    }

    // Group region indices by session id (preserve bag order within page).
    QVector<SessionImageId> order;
    QHash<SessionImageId, QVector<int>> bySid;
    QHash<SessionImageId, QStringList> snippets;
    for (const TextSelRef &r : refs) {
        if (r.sessionId == kInvalidSessionImageId || r.regionIndex < 0) {
            continue;
        }
        if (!bySid.contains(r.sessionId)) {
            order.append(r.sessionId);
        }
        bySid[r.sessionId].append(r.regionIndex);
        if (!r.text.isEmpty()) {
            snippets[r.sessionId].append(r.text);
        }
    }

    int pagesMarked = 0;
    for (SessionImageId sid : order) {
        ImageItem *item = nullptr;
        for (ImageItem *it : m_view->liveItems()) {
            if (it && it->sessionId() == sid) {
                item = it;
                break;
            }
        }
        if (!item) {
            item = m_view->primaryItem();
            if (!item || targetSid(item) != sid) {
                continue;
            }
        }

        const ThumtooCache::PageTextLayer *layerPtr = text.layerForItem(item);
        ThumtooCache::PageTextLayer cachedLayer;
        const QString path = text.pathForItem(item);
        if ((!layerPtr || layerPtr->regions.isEmpty()) && !path.isEmpty()) {
            cachedLayer = ThumtooCache::cachedPageTextLayer(path);
            if (!cachedLayer.regions.isEmpty()) {
                layerPtr = &cachedLayer;
            }
        }
        // Primary text session layer may be the only copy for current page.
        if ((!layerPtr || layerPtr->regions.isEmpty()) && !ts.layer.regions.isEmpty()
            && (ts.layerPath.isEmpty() || ts.layerPath == path
                || path.isEmpty())) {
            layerPtr = &ts.layer;
        }
        if (!layerPtr || layerPtr->regions.isEmpty()) {
            continue;
        }
        const ThumtooCache::PageTextLayer &layer = *layerPtr;

        Annotation::Object obj;
        obj.id = m_session.nextId();
        obj.kind = Annotation::Kind::HighlightQuad;
        obj.blend = Annotation::Blend::Multiply;
        obj.color = m_color;
        QStringList snip = snippets.value(sid);
        QSet<int> seen;
        for (int idx : bySid.value(sid)) {
            if (seen.contains(idx) || idx < 0 || idx >= layer.regions.size()) {
                continue;
            }
            seen.insert(idx);
            const auto &reg = layer.regions.at(idx);
            if (!reg.bbox.isEmpty()) {
                obj.quads.append(reg.bbox.normalized());
            }
            if (snip.isEmpty() && !reg.text.isEmpty()) {
                snip.append(reg.text);
            }
        }
        if (obj.quads.isEmpty()) {
            continue;
        }
        obj.textSnippet = snip.join(QLatin1Char(' '));
        if (obj.textSnippet.size() > 200) {
            obj.textSnippet = obj.textSnippet.left(197) + QStringLiteral("…");
        }

        QRectF bounds;
        bool yUp = false;
        QSize sourceSize;
        if (!pageSpaceForItem(item, &bounds, &yUp, &sourceSize)) {
            continue;
        }
        if (layer.pageBounds.isValid()) {
            bounds = layer.pageBounds;
            yUp = layer.pageYUp;
        }
        commitObject(sid, obj, bounds, yUp, QObject::tr("Mark selection"));
        ++pagesMarked;
    }

    if (pagesMarked > 0 && m_view->viewport()) {
        m_view->viewport()->update();
    }
    return pagesMarked;
}

bool AnnotationController::tryMousePress(QMouseEvent *event)
{
    if (!isToolActive() || !m_view || !event || event->button() != Qt::LeftButton) {
        return false;
    }
    ImageItem *item = itemAtViewPos(event->pos());
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
    m_shapeEndView = {};

    if (m_tool == Annotation::Tool::FreehandHighlighter
        || m_tool == Annotation::Tool::Pen) {
        m_draftPoints.append(viewToPage(item, event->pos(), bounds, yUp, sourceSize));
    } else if (m_tool == Annotation::Tool::TextHighlighter
               || m_tool == Annotation::Tool::Rect
               || m_tool == Annotation::Tool::Ellipse
               || m_tool == Annotation::Tool::Line) {
        m_rubberOriginView = event->pos();
        m_shapeEndView = event->pos();
        m_rubberView = QRect(m_rubberOriginView, QSize(1, 1));
    } else if (m_tool == Annotation::Tool::Eraser) {
        eraseAtPagePoint(viewToPage(item, event->pos(), bounds, yUp, sourceSize),
                         bounds, yUp);
    } else if (m_tool == Annotation::Tool::Select) {
        const QPointF pagePt =
            viewToPage(item, event->pos(), bounds, yUp, sourceSize);
        const Qt::KeyboardModifiers mods = event->modifiers();
        selectAtPagePoint(pagePt, mods);
        const bool additive = mods.testFlag(Qt::ShiftModifier)
                              || mods.testFlag(Qt::ControlModifier);
        if (!additive) {
            const qreal handleR = AnnotationPainter::handleHitRadiusPage(
                m_view, item, bounds, yUp, sourceSize);
            // Corner / endpoint resize: single selection only.
            if (m_selectedIds.size() == 1) {
                Annotation::Object selObj;
                if (m_session.findObject(m_draftSid, m_selectedIds.first(), &selObj)) {
                    if (!selObj.quads.isEmpty()) {
                        const QRectF handleBox = selObj.quads.size() == 1
                                                    ? selObj.quads.first()
                                                    : unionOfQuads(selObj.quads);
                        const ResizeCorner corner =
                            hitTestQuadHandle(handleBox, pagePt, handleR);
                        if (corner != ResizeCorner::None) {
                            beginResizeSelection(corner, selObj.id, selObj);
                            event->accept();
                            if (m_view->viewport()) {
                                m_view->viewport()->update();
                            }
                            return true;
                        }
                    }
                    // ShapeLine (and short strokes): drag endpoints.
                    if (selObj.kind == Annotation::Kind::ShapeLine
                        && selObj.points.size() >= 2) {
                        const qreal r2 = handleR * handleR;
                        for (int i = 0; i < selObj.points.size(); ++i) {
                            const QPointF d = pagePt - selObj.points.at(i);
                            if (d.x() * d.x() + d.y() * d.y() <= r2) {
                                beginResizeEndpoint(i, selObj.id, selObj);
                                event->accept();
                                if (m_view->viewport()) {
                                    m_view->viewport()->update();
                                }
                                return true;
                            }
                        }
                    }
                }
            }
            const quint64 hit =
                hitTestTopObject(m_draftSid, pagePt, qMax(6.0, m_width * 0.35));
            if (hit != 0 && m_selectedIds.contains(hit)) {
                beginMoveSelection(pagePt);
            }
        }
    } else if (m_tool == Annotation::Tool::Sticky) {
        placeStickyAt(viewToPage(item, event->pos(), bounds, yUp, sourceSize), bounds, yUp);
        m_drawing = false;
    }
    event->accept();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    return true;
}

bool AnnotationController::tryMouseDoubleClick(QMouseEvent *event)
{
    if (!isToolActive() || !m_view || !event || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_tool != Annotation::Tool::Select) {
        return false;
    }
    ImageItem *item = itemAtViewPos(event->pos());
    if (!item) {
        return false;
    }
    QRectF bounds;
    bool yUp = false;
    QSize sourceSize;
    if (!pageSpaceForItem(item, &bounds, &yUp, &sourceSize)) {
        return false;
    }
    const SessionImageId sid = targetSid(item);
    const QPointF pagePt = viewToPage(item, event->pos(), bounds, yUp, sourceSize);
    const quint64 hitId = hitTestTopObject(sid, pagePt, qMax(6.0, m_width * 0.35));
    Annotation::Object hitObj;
    if (hitId == 0 || !m_session.findObject(sid, hitId, &hitObj)
        || hitObj.kind != Annotation::Kind::StickyNote) {
        return false;
    }
    const QString prior = !hitObj.text.isEmpty() ? hitObj.text : hitObj.textSnippet;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        m_view, QObject::tr("Edit sticky note"), QObject::tr("Note text:"), prior, &ok);
    if (!ok) {
        event->accept();
        return true;
    }
    Annotation::Object updated = hitObj;
    updated.text = text;
    updated.textSnippet.clear();
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        stack->push(new AnnotationReplaceCommand(
            m_view, sid, hitObj, updated, QObject::tr("Edit sticky note")));
    } else {
        m_session.updateObject(sid, updated);
    }
    m_selectedIds = {hitId};
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    event->accept();
    return true;
}

bool AnnotationController::tryMouseMove(QMouseEvent *event)
{
    if (!m_drawing || !isToolActive() || !event) {
        return false;
    }
    ImageItem *item = itemForDraftSid();
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
        m_shapeEndView = event->pos();
        m_rubberView = QRect(m_rubberOriginView, event->pos()).normalized();
    } else if (m_tool == Annotation::Tool::Rect
               || m_tool == Annotation::Tool::Ellipse) {
        const Qt::KeyboardModifiers mods = event->modifiers();
        m_shapeEndView = event->pos();
        m_rubberView = constrainedShapeRubber(m_rubberOriginView, event->pos(), mods);
    } else if (m_tool == Annotation::Tool::Line) {
        const Qt::KeyboardModifiers mods = event->modifiers();
        m_shapeEndView = constrainedLineEnd(m_rubberOriginView, event->pos(), mods);
        m_rubberView = QRect(m_rubberOriginView, m_shapeEndView).normalized();
    } else if (m_tool == Annotation::Tool::Eraser) {
        eraseAtPagePoint(
            viewToPage(item, event->pos(), m_draftBounds, m_draftYUp, m_draftSourceSize),
            m_draftBounds, m_draftYUp);
    } else if (m_tool == Annotation::Tool::Select && m_resizing) {
        const QPointF pagePt =
            viewToPage(item, event->pos(), m_draftBounds, m_draftYUp, m_draftSourceSize);
        applyResizeTo(pagePt);
    } else if (m_tool == Annotation::Tool::Select && m_moving) {
        const QPointF pagePt =
            viewToPage(item, event->pos(), m_draftBounds, m_draftYUp, m_draftSourceSize);
        applyMoveDelta(pagePt - m_moveOriginPage);
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
    // item resolved via m_draftSid in finish* helpers / itemForDraftSid
    m_drawing = false;
    if (m_tool == Annotation::Tool::FreehandHighlighter
        || m_tool == Annotation::Tool::Pen) {
        finishFreehand();
    } else if (m_tool == Annotation::Tool::TextHighlighter) {
        finishTextHighlight();
    } else if (m_tool == Annotation::Tool::Rect
               || m_tool == Annotation::Tool::Ellipse) {
        if (event) {
            m_rubberView = constrainedShapeRubber(m_rubberOriginView, event->pos(),
                                                  event->modifiers());
            m_shapeEndView = event->pos();
        }
        finishShape();
    } else if (m_tool == Annotation::Tool::Line) {
        if (event) {
            m_shapeEndView = constrainedLineEnd(m_rubberOriginView, event->pos(),
                                                event->modifiers());
        }
        finishLine();
    } else if (m_tool == Annotation::Tool::Select && m_resizing) {
        finishResizeSelection();
    } else if (m_tool == Annotation::Tool::Select && m_moving) {
        finishMoveSelection();
    }
    m_draftPoints.clear();
    m_rubberView = {};
    m_shapeEndView = {};
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
    if (o.kind == Annotation::Kind::HighlightQuad
        || o.kind == Annotation::Kind::ShapeRect
        || o.kind == Annotation::Kind::ShapeEllipse
        || o.kind == Annotation::Kind::StickyNote) {
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
    SessionImageId sid = m_draftSid;
    if (sid == kInvalidSessionImageId) {
        sid = targetSid(targetItem());
    }
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
        persistPageForSid(sid);
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }
}






void AnnotationController::placeStickyAt(const QPointF &pagePt, const QRectF &pageBounds,
                                         bool pageYUp)
{
    if (!m_view || m_draftSid == kInvalidSessionImageId) {
        return;
    }
    qreal w = 120.0;
    qreal h = 90.0;
    if (pageBounds.isValid() && pageBounds.width() > 10) {
        w = pageBounds.width() * 0.18;
        h = w * 0.75;
    }
    const QRectF box(pagePt.x() - w * 0.05, pagePt.y() - h * 0.05, w, h);

    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        m_view, QObject::tr("Sticky note"), QObject::tr("Note text:"),
        QString(), &ok);
    if (!ok) {
        return;
    }

    Annotation::Object obj;
    obj.id = m_session.nextId();
    obj.kind = Annotation::Kind::StickyNote;
    obj.blend = Annotation::Blend::SourceOver;
    obj.color = m_color.isValid() ? m_color : QColor(255, 230, 100);
    if (obj.color.lightness() < 80) {
        obj.color = QColor(255, 230, 100);
    }
    obj.width = 1.0;
    obj.quads.append(box);
    obj.text = text;
    commitObject(m_draftSid, obj, pageBounds, pageYUp, QObject::tr("Sticky note"));
}

void AnnotationController::finishLine()
{
    if (!m_view || m_draftSid == kInvalidSessionImageId) {
        return;
    }
    ImageItem *item = itemForDraftSid();
    if (!item) {
        return;
    }
    const QPointF p0 =
        viewToPage(item, m_rubberOriginView, m_draftBounds, m_draftYUp, m_draftSourceSize);
    const QPointF p1 =
        viewToPage(item, m_shapeEndView, m_draftBounds, m_draftYUp, m_draftSourceSize);
    const qreal dx = p1.x() - p0.x();
    const qreal dy = p1.y() - p0.y();
    if (dx * dx + dy * dy < 1.0) {
        return;
    }
    Annotation::Object obj;
    obj.id = m_session.nextId();
    obj.kind = Annotation::Kind::ShapeLine;
    obj.blend = Annotation::Blend::SourceOver;
    obj.color = m_color;
    obj.width = m_width;
    obj.points = {p0, p1};
    commitObject(m_draftSid, obj, m_draftBounds, m_draftYUp, QObject::tr("Line"));
}

void AnnotationController::finishShape()
{
    if (!m_view || m_draftSid == kInvalidSessionImageId || m_rubberView.isEmpty()) {
        return;
    }
    if (m_rubberView.width() < 3 && m_rubberView.height() < 3) {
        return;
    }
    ImageItem *item = itemForDraftSid();
    if (!item) {
        return;
    }
    // Map rubber corners to page space.
    const QPoint topLeft = m_rubberView.topLeft();
    const QPoint bottomRight = m_rubberView.bottomRight();
    const QPointF p0 = viewToPage(item, topLeft, m_draftBounds, m_draftYUp, m_draftSourceSize);
    const QPointF p1 = viewToPage(item, bottomRight, m_draftBounds, m_draftYUp, m_draftSourceSize);
    QRectF pageR = QRectF(p0, p1).normalized();
    if (pageR.width() < 1e-3 || pageR.height() < 1e-3) {
        return;
    }
    Annotation::Object obj;
    obj.id = m_session.nextId();
    obj.kind = (m_tool == Annotation::Tool::Ellipse) ? Annotation::Kind::ShapeEllipse
                                                     : Annotation::Kind::ShapeRect;
    obj.blend = Annotation::Blend::SourceOver;
    obj.color = m_color;
    obj.width = m_width;
    obj.quads.append(pageR);
    commitObject(m_draftSid, obj, m_draftBounds, m_draftYUp,
                 obj.kind == Annotation::Kind::ShapeEllipse ? QObject::tr("Ellipse")
                                                            : QObject::tr("Rectangle"));
}


QImage AnnotationController::renderFlattenedDisplay() const
{
    if (!m_view) {
        return {};
    }
    ImageItem *item = targetItem();
    if (!item) {
        return {};
    }
    QImage base = item->displayImage();
    if (base.isNull()) {
        return {};
    }
    QImage out = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (out.isNull()) {
        return {};
    }

    QRectF bounds;
    bool yUp = false;
    QSize sourceSize;
    // Const cast for pageSpace helpers that are non-const only due to ensureMemberLayers
    auto *self = const_cast<AnnotationController *>(this);
    if (!self->pageSpaceForItem(item, &bounds, &yUp, &sourceSize)) {
        return out;
    }
    SessionImageId sid = targetSid(item);
    const Annotation::Page *pg = m_session.page(sid);
    if (!pg || pg->objects.isEmpty()) {
        return out;
    }
    const QRectF pb = pg->pageBounds.isValid() ? pg->pageBounds : bounds;
    const bool py = pg->pageBounds.isValid() ? pg->pageYUp : yUp;

    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw in display-pixel space (image coords, no item offset / scene).
    for (const Annotation::Object &obj : pg->objects) {
        painter.save();
        if (obj.blend == Annotation::Blend::Multiply) {
            painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        } else {
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }

        if (obj.kind == Annotation::Kind::StickyNote) {
            for (const QRectF &q : obj.quads) {
                const QRectF disp = AnnotationPainter::pageRectToDisplay(m_view, item, q, pb, py, sourceSize);
                if (disp.isEmpty()) {
                    continue;
                }
                painter.setPen(QPen(obj.color.darker(120), 1));
                painter.setBrush(obj.color);
                painter.drawRoundedRect(disp, 4, 4);
                painter.setPen(QColor(40, 40, 40));
                QFont font = painter.font();
                font.setPointSizeF(qBound(8.0, disp.height() * 0.11, 18.0));
                painter.setFont(font);
                const QString text =
                    !obj.text.isEmpty() ? obj.text : (!obj.textSnippet.isEmpty() ? obj.textSnippet : QObject::tr("(note)"));
                painter.drawText(disp.adjusted(6, 6, -6, -6),
                                 Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, text);
            }
        } else if (obj.kind == Annotation::Kind::HighlightQuad
            || obj.kind == Annotation::Kind::ShapeRect
            || obj.kind == Annotation::Kind::ShapeEllipse) {
            painter.setPen(Qt::NoPen);
            if (obj.kind == Annotation::Kind::HighlightQuad) {
                painter.setBrush(obj.color);
            } else {
                QPen pen(obj.color);
                qreal pageUnit = 1.0;
                if (pb.width() > 1 && sourceSize.width() > 0) {
                    pageUnit = qreal(sourceSize.width()) / pb.width();
                }
                pen.setWidthF(qMax(0.5, obj.width * pageUnit));
                painter.setPen(pen);
                QColor fill = obj.color;
                fill.setAlpha(40);
                painter.setBrush(fill);
            }
            for (const QRectF &q : obj.quads) {
                const QRectF disp = AnnotationPainter::pageRectToDisplay(m_view, item, q, pb, py, sourceSize);
                if (disp.isEmpty()) {
                    continue;
                }
                if (obj.kind == Annotation::Kind::ShapeEllipse) {
                    painter.drawEllipse(disp);
                } else {
                    painter.drawRect(disp);
                }
            }
        } else if (!obj.points.isEmpty()) {
            // Strokes + lines
            QPainterPath path;
            bool first = true;
            for (const QPointF &pp : obj.points) {
                const QRectF disp = AnnotationPainter::pageRectToDisplay(
                    m_view, item, QRectF(pp.x(), pp.y(), 0.01, 0.01), pb, py, sourceSize);
                const QPointF pt = disp.center();
                if (first) {
                    path.moveTo(pt);
                    first = false;
                } else {
                    path.lineTo(pt);
                }
            }
            QPen pen(obj.color);
            qreal pageUnit = 1.0;
            if (pb.width() > 1 && sourceSize.width() > 0) {
                pageUnit = qreal(sourceSize.width()) / pb.width();
            }
            pen.setWidthF(qMax(0.5, obj.width * pageUnit));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            if (obj.points.size() == 1) {
                const qreal r = qMax(0.5, obj.width * 0.5 * pageUnit);
                painter.setPen(Qt::NoPen);
                painter.setBrush(obj.color);
                const QRectF disp = AnnotationPainter::pageRectToDisplay(
                    m_view, item, QRectF(obj.points.first().x(), obj.points.first().y(), 0.01, 0.01),
                    pb, py, sourceSize);
                painter.drawEllipse(disp.center(), r, r);
            }
        }
        painter.restore();
    }
    painter.end();
    return out;
}

void AnnotationController::clearSelection()
{
    if (m_selectedIds.isEmpty()) {
        return;
    }
    m_selectedIds.clear();
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

quint64 AnnotationController::hitTestTopObject(SessionImageId sid, const QPointF &pagePt,
                                               qreal radius) const
{
    const Annotation::Page *pg = m_session.page(sid);
    if (!pg) {
        return 0;
    }
    // Topmost = last in list (drawn last).
    for (int i = pg->objects.size() - 1; i >= 0; --i) {
        if (objectHitsPagePoint(pg->objects.at(i), pagePt, radius)) {
            return pg->objects.at(i).id;
        }
    }
    return 0;
}


Annotation::Object AnnotationController::translatedObject(const Annotation::Object &o,
                                                          const QPointF &delta)
{
    Annotation::Object out = o;
    for (QPointF &pt : out.points) {
        pt += delta;
    }
    for (QRectF &q : out.quads) {
        q.translate(delta);
    }
    return out;
}


AnnotationController::ResizeCorner
AnnotationController::hitTestQuadHandle(const QRectF &quad, const QPointF &pagePt,
                                        qreal radius)
{
    if (!quad.isValid() || !(radius > 0)) {
        return ResizeCorner::None;
    }
    const QPointF corners[4] = {
        quad.topLeft(),
        quad.topRight(),
        quad.bottomRight(),
        quad.bottomLeft(),
    };
    const qreal r2 = radius * radius;
    ResizeCorner best = ResizeCorner::None;
    qreal bestD = r2;
    for (int i = 0; i < 4; ++i) {
        const qreal dx = pagePt.x() - corners[i].x();
        const qreal dy = pagePt.y() - corners[i].y();
        const qreal d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            best = static_cast<ResizeCorner>(i);
        }
    }
    return best;
}

QRectF AnnotationController::resizedQuad(const QRectF &base, ResizeCorner corner,
                                         const QPointF &pagePt)
{
    if (corner == ResizeCorner::None || !base.isValid()) {
        return base;
    }
    QPointF fixed;
    switch (corner) {
    case ResizeCorner::TL:
        fixed = base.bottomRight();
        break;
    case ResizeCorner::TR:
        fixed = base.bottomLeft();
        break;
    case ResizeCorner::BR:
        fixed = base.topLeft();
        break;
    case ResizeCorner::BL:
        fixed = base.topRight();
        break;
    default:
        return base;
    }
    QRectF out = QRectF(fixed, pagePt).normalized();
    if (out.width() < 2.0) {
        out.setWidth(2.0);
        if (corner == ResizeCorner::TL || corner == ResizeCorner::BL) {
            out.moveRight(fixed.x());
        } else {
            out.moveLeft(fixed.x());
        }
    }
    if (out.height() < 2.0) {
        out.setHeight(2.0);
        if (corner == ResizeCorner::TL || corner == ResizeCorner::TR) {
            out.moveBottom(fixed.y());
        } else {
            out.moveTop(fixed.y());
        }
    }
    return out;
}


void AnnotationController::beginResizeSelection(ResizeCorner corner, quint64 id,
                                                const Annotation::Object &baseline)
{
    m_resizing = true;
    m_resizeCorner = corner;
    m_resizeEndpoint = -1;
    m_resizeId = id;
    m_resizeBaseline = baseline;
    m_moving = false;
    m_moveDidDrag = false;
    m_moveBaseline.clear();
}

void AnnotationController::beginResizeEndpoint(int endpointIndex, quint64 id,
                                               const Annotation::Object &baseline)
{
    m_resizing = true;
    m_resizeCorner = ResizeCorner::None;
    m_resizeEndpoint = endpointIndex;
    m_resizeId = id;
    m_resizeBaseline = baseline;
    m_moving = false;
    m_moveDidDrag = false;
    m_moveBaseline.clear();
}

void AnnotationController::applyResizeTo(const QPointF &pagePt)
{
    if (!m_resizing) {
        return;
    }
    Annotation::Object updated = m_resizeBaseline;
    if (m_resizeEndpoint >= 0 && m_resizeEndpoint < updated.points.size()) {
        updated.points[m_resizeEndpoint] = pagePt;
        m_session.updateObject(m_draftSid, updated);
        return;
    }
    if (m_resizeCorner == ResizeCorner::None || m_resizeBaseline.quads.isEmpty()) {
        return;
    }
    if (m_resizeBaseline.quads.size() == 1) {
        updated.quads[0] =
            resizedQuad(m_resizeBaseline.quads.first(), m_resizeCorner, pagePt);
    } else {
        const QRectF fromU = unionOfQuads(m_resizeBaseline.quads);
        const QRectF toU = resizedQuad(fromU, m_resizeCorner, pagePt);
        for (int i = 0; i < updated.quads.size(); ++i) {
            updated.quads[i] =
                mapRectThroughUnions(m_resizeBaseline.quads.at(i), fromU, toU);
        }
    }
    m_session.updateObject(m_draftSid, updated);
}

void AnnotationController::finishResizeSelection()
{
    if (!m_resizing) {
        return;
    }
    Annotation::Object after;
    if (m_session.findObject(m_draftSid, m_resizeId, &after) && m_view) {
        bool changed = false;
        if (m_resizeEndpoint >= 0) {
            if (m_resizeEndpoint < after.points.size()
                && m_resizeEndpoint < m_resizeBaseline.points.size()) {
                changed = after.points.at(m_resizeEndpoint)
                          != m_resizeBaseline.points.at(m_resizeEndpoint);
            }
        } else {
            const QRectF a = after.quads.isEmpty() ? QRectF() : after.quads.first();
            const QRectF b = m_resizeBaseline.quads.isEmpty()
                                ? QRectF()
                                : m_resizeBaseline.quads.first();
            changed = a != b;
        }
        if (changed) {
            if (QUndoStack *stack = m_view->hostUndoStack()) {
                stack->push(new AnnotationReplaceCommand(
                    m_view, m_draftSid, m_resizeBaseline, after,
                    m_resizeEndpoint >= 0 ? QObject::tr("Move line endpoint")
                                          : QObject::tr("Resize annotation")));
            }
        } else {
            m_session.updateObject(m_draftSid, m_resizeBaseline);
        }
    }
    m_resizing = false;
    m_resizeCorner = ResizeCorner::None;
    m_resizeEndpoint = -1;
    m_resizeId = 0;
    m_resizeBaseline = {};
}

void AnnotationController::cancelResizeSelection()
{
    if (!m_resizing) {
        return;
    }
    m_session.updateObject(m_draftSid, m_resizeBaseline);
    m_resizing = false;
    m_resizeCorner = ResizeCorner::None;
    m_resizeEndpoint = -1;
    m_resizeId = 0;
    m_resizeBaseline = {};
}

void AnnotationController::beginMoveSelection(const QPointF &pageOrigin)
{
    m_moving = true;
    m_moveDidDrag = false;
    m_moveOriginPage = pageOrigin;
    m_moveBaseline.clear();
    if (m_draftSid == kInvalidSessionImageId) {
        return;
    }
    const Annotation::Page *pg = m_session.page(m_draftSid);
    if (!pg) {
        return;
    }
    for (const Annotation::Object &o : pg->objects) {
        if (m_selectedIds.contains(o.id)) {
            m_moveBaseline.append(o);
        }
    }
}

void AnnotationController::applyMoveDelta(const QPointF &delta)
{
    if (!m_moving || m_moveBaseline.isEmpty()) {
        return;
    }
    if (delta.x() * delta.x() + delta.y() * delta.y() > 1e-6) {
        m_moveDidDrag = true;
    }
    for (const Annotation::Object &base : m_moveBaseline) {
        m_session.updateObject(m_draftSid, translatedObject(base, delta));
    }
}

void AnnotationController::finishMoveSelection()
{
    if (!m_moving) {
        return;
    }
    if (m_moveDidDrag && !m_moveBaseline.isEmpty() && m_view
        && m_draftSid != kInvalidSessionImageId) {
        QVector<Annotation::Object> after;
        after.reserve(m_moveBaseline.size());
        for (const Annotation::Object &base : m_moveBaseline) {
            Annotation::Object cur;
            if (m_session.findObject(m_draftSid, base.id, &cur)) {
                after.append(cur);
            } else {
                after.append(translatedObject(base, QPointF()));
            }
        }
        if (QUndoStack *stack = m_view->hostUndoStack()) {
            stack->push(new AnnotationMoveCommand(
                m_view, m_draftSid, m_moveBaseline, after,
                m_moveBaseline.size() == 1 ? QObject::tr("Move annotation")
                                           : QObject::tr("Move annotations")));
        }
    } else if (m_moveDidDrag == false && !m_moveBaseline.isEmpty()) {
        // Snap back to baseline (no-op if never dragged).
        for (const Annotation::Object &base : m_moveBaseline) {
            m_session.updateObject(m_draftSid, base);
        }
    }
    m_moving = false;
    m_moveDidDrag = false;
    m_moveBaseline.clear();
    m_moveOriginPage = {};
}

void AnnotationController::cancelMoveSelection()
{
    if (!m_moving) {
        return;
    }
    for (const Annotation::Object &base : m_moveBaseline) {
        m_session.updateObject(m_draftSid, base);
    }
    m_moving = false;
    m_moveDidDrag = false;
    m_moveBaseline.clear();
    m_moveOriginPage = {};
}

void AnnotationController::selectAtPagePoint(const QPointF &pagePt,
                                             Qt::KeyboardModifiers mods)
{
    if (m_draftSid == kInvalidSessionImageId) {
        return;
    }
    const quint64 id = hitTestTopObject(m_draftSid, pagePt, qMax(6.0, m_width * 0.35));
    const bool additive = mods.testFlag(Qt::ShiftModifier);
    const bool toggle = mods.testFlag(Qt::ControlModifier);
    if (!additive && !toggle) {
        // Replace selection (empty click clears).
        m_selectedIds.clear();
        if (id != 0) {
            m_selectedIds.append(id);
        }
    } else if (id != 0) {
        if (toggle) {
            if (m_selectedIds.contains(id)) {
                m_selectedIds.removeAll(id);
            } else {
                m_selectedIds.append(id);
            }
        } else { // additive (Shift)
            if (!m_selectedIds.contains(id)) {
                m_selectedIds.append(id);
            }
        }
    }
    // Shift/Ctrl + miss: keep existing selection.
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AnnotationController::selectAllCurrentPage()
{
    SessionImageId sid = m_draftSid;
    if (sid == kInvalidSessionImageId) {
        sid = targetSid(targetItem());
    }
    if (sid == kInvalidSessionImageId) {
        return;
    }
    m_draftSid = sid;
    m_selectedIds.clear();
    if (const Annotation::Page *pg = m_session.page(sid)) {
        for (const Annotation::Object &o : pg->objects) {
            m_selectedIds.append(o.id);
        }
    }
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AnnotationController::deleteSelected()
{
    if (m_selectedIds.isEmpty() || !m_view) {
        return;
    }
    SessionImageId sid = m_draftSid;
    if (sid == kInvalidSessionImageId) {
        sid = targetSid(targetItem());
    }
    if (sid == kInvalidSessionImageId) {
        return;
    }
    Annotation::Page *pg = m_session.page(sid);
    if (!pg) {
        return;
    }
    QVector<Annotation::Object> hit;
    for (const Annotation::Object &o : pg->objects) {
        if (m_selectedIds.contains(o.id)) {
            hit.append(o);
        }
    }
    if (hit.isEmpty()) {
        m_selectedIds.clear();
        return;
    }
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        auto *cmd = new AnnotationRemoveCommand(
            m_view, sid, hit, pg->pageBounds, pg->pageYUp,
            hit.size() == 1 ? QObject::tr("Delete annotation")
                            : QObject::tr("Delete annotations"));
        stack->push(cmd);
    } else {
        for (const Annotation::Object &o : hit) {
            m_session.removeObject(sid, o.id);
        }
    }
    m_selectedIds.clear();
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


bool AnnotationController::tryKeyPress(QKeyEvent *event)
{
    if (!event || !isToolActive()) {
        return false;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_tool == Annotation::Tool::Select && !m_selectedIds.isEmpty()) {
            deleteSelected();
            event->accept();
            return true;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_resizing) {
            cancelResizeSelection();
            event->accept();
            return true;
        }
        if (m_moving) {
            cancelMoveSelection();
            event->accept();
            return true;
        }
        if (m_drawing) {
            m_drawing = false;
            m_draftPoints.clear();
            m_rubberView = {};
            m_shapeEndView = {};
            if (m_view && m_view->viewport()) {
                m_view->viewport()->update();
            }
            event->accept();
            return true;
        }
        if (!m_selectedIds.isEmpty()) {
            clearSelection();
            event->accept();
            return true;
        }
    }
    if (m_tool == Annotation::Tool::Select
        && event->key() == Qt::Key_A
        && event->modifiers().testFlag(Qt::ControlModifier)) {
        selectAllCurrentPage();
        event->accept();
        return true;
    }
    return false;
}
