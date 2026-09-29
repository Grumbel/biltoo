// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "annotation/annotationpainter.h"

#include "content/contentxform.h"
#include "host/thumtoocache.h"
#include "imageitem.h"
#include "imageview.h"
#include "view/viewtransform.h"

#include <QFont>
#include <QLineF>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QtMath>

namespace {

SessionImageId targetSid(ImageItem *item)
{
    return item ? item->sessionId() : kInvalidSessionImageId;
}

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

/** Scene units per device pixel (view transform × DPR). */
qreal scenePerDevicePx(ImageView *view)
{
    if (!view) {
        return 1.0;
    }
    const qreal vs = ViewTransform::sanitizeViewScale(
        ViewTransform::scaleFrom(view->transform()));
    qreal dpr = 1.0;
    if (view->viewport()) {
        dpr = view->viewport()->devicePixelRatioF();
    }
    if (!(dpr > 0.0)) {
        dpr = 1.0;
    }
    return 1.0 / (vs * dpr);
}

/** Selection / resize handle half-size in scene units (~device px). */
constexpr qreal kHandleHalfDevicePx = 6.0;
constexpr qreal kHandleHitDevicePx = 12.0;

qreal handleHalfScene(ImageView *view)
{
    return kHandleHalfDevicePx * scenePerDevicePx(view);
}

} // namespace

QRectF AnnotationPainter::pageRectToDisplay(ImageView *view, ImageItem *item,
                                            const QRectF &pageRect, const QRectF &pageBounds,
                                            bool pageYUp, const QSize &sourceSize)
{
    if (!item || !view || pageRect.isEmpty()) {
        return {};
    }
    SessionImageId sid = targetSid(item);
    const WorkspaceItemState st =
        view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
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

QPointF AnnotationPainter::pageToScene(ImageView *view, ImageItem *item, const QPointF &pagePt,
                                       const QRectF &pageBounds, bool pageYUp,
                                       const QSize &sourceSize)
{
    if (!item || !view || sourceSize.width() < 1 || sourceSize.height() < 1) {
        return {};
    }
    SessionImageId sid = targetSid(item);
    const WorkspaceItemState st =
        view->hostDisplayPipeline().wantAppearanceForItem(item, sid);
    const ContentXform::Value x = ContentXform::Value::fromState(st);
    const QRectF inSource =
        ThumtooCache::pageRectToImageRect(QRectF(pagePt.x(), pagePt.y(), 1e-3, 1e-3),
                                          pageBounds, sourceSize, pageYUp);
    if (inSource.isEmpty()) {
        return {};
    }
    QPointF disp =
        ContentXform::mapSourcePointToDisplay(inSource.center(), sourceSize, x);
    const QSize logical = ContentXform::layoutSize(sourceSize, x);
    const QSize itemSz = item->imageSize();
    if (logical.width() > 0 && logical.height() > 0 && itemSz.width() > 0
        && itemSz.height() > 0
        && (logical.width() != itemSz.width() || logical.height() != itemSz.height())) {
        disp = QPointF(disp.x() * qreal(itemSz.width()) / qreal(logical.width()),
                       disp.y() * qreal(itemSz.height()) / qreal(logical.height()));
    }
    return item->mapToScene(disp + item->offset());
}

/** Scene-space length of one page unit (for non-cosmetic stroke widths). */
static qreal pageUnitInScene(ImageView *view, ImageItem *item, const QRectF &pageBounds,
                             bool pageYUp, const QSize &sourceSize)
{
    if (!view || !item || pageBounds.width() < 1e-6) {
        return 1.0;
    }
    const QPointF c = pageBounds.center();
    const QPointF a =
        AnnotationPainter::pageToScene(view, item, c, pageBounds, pageYUp, sourceSize);
    const QPointF b = AnnotationPainter::pageToScene(
        view, item, QPointF(c.x() + 1.0, c.y()), pageBounds, pageYUp, sourceSize);
    const qreal d = QLineF(a, b).length();
    if (d > 1e-6) {
        return d;
    }
    if (sourceSize.width() > 0) {
        return qreal(sourceSize.width()) / pageBounds.width();
    }
    return 1.0;
}

QRectF AnnotationPainter::pageRectToScene(ImageView *view, ImageItem *item,
                                          const QRectF &pageRect, const QRectF &pageBounds,
                                          bool pageYUp, const QSize &sourceSize)
{
    const QRectF disp = pageRectToDisplay(view, item, pageRect, pageBounds, pageYUp, sourceSize);
    if (disp.isEmpty()) {
        return {};
    }
    const QRectF local = disp.translated(item->offset());
    return item->mapToScene(local).boundingRect();
}

void AnnotationPainter::applyHighlightBlend(QPainter &painter, const QColor &color)
{
    Q_UNUSED(color);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    painter.setOpacity(1.0);
}

void AnnotationPainter::paintStroke(QPainter &painter, ImageView *view, ImageItem *item,
                                    const Annotation::Object &obj, const QRectF &pageBounds,
                                    bool pageYUp, const QSize &sourceSize)
{
    if (!item || obj.points.size() < 1) {
        return;
    }
    QPainterPath path;
    bool first = true;
    for (const QPointF &pp : obj.points) {
        const QPointF scene = pageToScene(view, item, pp, pageBounds, pageYUp, sourceSize);
        if (first) {
            path.moveTo(scene);
            first = false;
        } else {
            path.lineTo(scene);
        }
    }
    if (obj.points.size() == 1) {
        const QPointF scene =
            pageToScene(view, item, obj.points.first(), pageBounds, pageYUp, sourceSize);
        const qreal pageUnit =
            pageUnitInScene(view, item, pageBounds, pageYUp, sourceSize);
        const qreal r = qMax(0.25, obj.width * 0.5 * pageUnit);
        painter.setPen(Qt::NoPen);
        painter.setBrush(obj.color);
        painter.drawEllipse(scene, r, r);
        return;
    }

    QPen pen(obj.color);
    const qreal pageUnit = pageUnitInScene(view, item, pageBounds, pageYUp, sourceSize);
    // Non-cosmetic: width is in scene units and scales with the view transform.
    pen.setCosmetic(false);
    pen.setWidthF(qMax(0.25, obj.width * pageUnit));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void AnnotationPainter::paintQuads(QPainter &painter, ImageView *view, ImageItem *item,
                                   const Annotation::Object &obj, const QRectF &pageBounds,
                                   bool pageYUp, const QSize &sourceSize)
{
    if (!item || obj.quads.isEmpty()) {
        return;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(obj.color);
    for (const QRectF &q : obj.quads) {
        const QRectF scene = pageRectToScene(view, item, q, pageBounds, pageYUp, sourceSize);
        if (!scene.isEmpty()) {
            painter.drawRect(scene);
        }
    }
}

void AnnotationPainter::paintShape(QPainter &painter, ImageView *view, ImageItem *item,
                                   const Annotation::Object &obj, const QRectF &pageBounds,
                                   bool pageYUp, const QSize &sourceSize)
{
    if (!item || obj.quads.isEmpty()) {
        return;
    }
    const QRectF pageR = obj.quads.first();
    const QRectF scene = pageRectToScene(view, item, pageR, pageBounds, pageYUp, sourceSize);
    if (scene.isEmpty()) {
        return;
    }
    const qreal pageUnit = pageUnitInScene(view, item, pageBounds, pageYUp, sourceSize);
    QPen pen(obj.color);
    pen.setCosmetic(false);
    pen.setWidthF(qMax(0.25, obj.width * pageUnit));
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    QColor fill = obj.color;
    fill.setAlpha(40);
    painter.setBrush(fill);
    if (obj.kind == Annotation::Kind::ShapeEllipse) {
        painter.drawEllipse(scene);
    } else {
        painter.drawRect(scene);
    }
}

void AnnotationPainter::paintSticky(QPainter &painter, ImageView *view, ImageItem *item,
                                    const Annotation::Object &obj, const QRectF &pageBounds,
                                    bool pageYUp, const QSize &sourceSize)
{
    if (!item || obj.quads.isEmpty()) {
        return;
    }
    const QRectF scene = pageRectToScene(view, item, obj.quads.first(), pageBounds, pageYUp,
                                         sourceSize);
    if (scene.isEmpty()) {
        return;
    }
    // Chrome sizes in device pixels so Gallery zoom / HiDPI match Image mode.
    const qreal sp = scenePerDevicePx(view);
    const qreal rad = 4.0 * sp;
    const qreal fold = qBound(6.0 * sp, scene.width() * 0.18, 28.0 * sp);
    // pad reserved for future label chrome

    QPen border(obj.color.darker(120), 0);
    border.setCosmetic(true);
    border.setWidthF(1.0);
    painter.setPen(border);
    painter.setBrush(obj.color);
    painter.drawRoundedRect(scene, rad, rad);
    QPolygonF dogear;
    dogear << QPointF(scene.right() - fold, scene.top())
           << QPointF(scene.right(), scene.top())
           << QPointF(scene.right(), scene.top() + fold);
    painter.setBrush(obj.color.darker(110));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(dogear);

    const QString text = !obj.text.isEmpty()
                             ? obj.text
                             : (!obj.textSnippet.isEmpty() ? obj.textSnippet
                                                          : QStringLiteral("(note)"));
    // Text: draw in device-pixel space so point size is not double-scaled by the
    // view matrix and stays readable at Gallery cell size and deep zoom.
    const qreal vs = ViewTransform::sanitizeViewScale(
        ViewTransform::scaleFrom(view ? view->transform() : QTransform()));
    qreal dpr = 1.0;
    if (view && view->viewport()) {
        dpr = view->viewport()->devicePixelRatioF();
    }
    if (!(dpr > 0.0)) {
        dpr = 1.0;
    }
    const qreal screenH = scene.height() * vs * dpr;
    const qreal screenW = scene.width() * vs * dpr;
    const int pixelSize = qBound(9, qRound(screenH * 0.13), 28);

    painter.save();
    painter.translate(scene.center());
    painter.scale(1.0 / (vs * dpr), 1.0 / (vs * dpr));
    const QRectF pixelBox(-screenW * 0.5, -screenH * 0.5, screenW, screenH);
    const qreal padPx = 6.0 * dpr;
    const qreal foldPx = fold * vs * dpr;
    QFont font = painter.font();
    font.setPixelSize(pixelSize);
    painter.setFont(font);
    painter.setPen(QColor(40, 40, 40));
    painter.drawText(pixelBox.adjusted(padPx, padPx, -padPx - foldPx * 0.25, -padPx),
                     Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, text);
    painter.restore();
}

void AnnotationPainter::paintObject(QPainter &painter, ImageView *view, ImageItem *item,
                                    const Annotation::Object &obj, const QRectF &pageBounds,
                                    bool pageYUp, const QSize &sourceSize)
{
    painter.save();
    if (obj.blend == Annotation::Blend::Multiply) {
        applyHighlightBlend(painter, obj.color);
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setOpacity(1.0);
    }
    if (obj.kind == Annotation::Kind::HighlightQuad) {
        paintQuads(painter, view, item, obj, pageBounds, pageYUp, sourceSize);
    } else if (obj.kind == Annotation::Kind::ShapeRect
               || obj.kind == Annotation::Kind::ShapeEllipse) {
        paintShape(painter, view, item, obj, pageBounds, pageYUp, sourceSize);
    } else if (obj.kind == Annotation::Kind::StickyNote) {
        paintSticky(painter, view, item, obj, pageBounds, pageYUp, sourceSize);
    } else {
        paintStroke(painter, view, item, obj, pageBounds, pageYUp, sourceSize);
    }
    painter.restore();
}

void AnnotationPainter::paintPageObjects(QPainter &painter, ImageView *view, ImageItem *item,
                                         const Annotation::Page &page,
                                         const QRectF &fallbackBounds, bool fallbackYUp,
                                         const QSize &sourceSize)
{
    const QRectF pb = page.pageBounds.isValid() ? page.pageBounds : fallbackBounds;
    const bool py = page.pageBounds.isValid() ? page.pageYUp : fallbackYUp;
    for (const Annotation::Object &obj : page.objects) {
        paintObject(painter, view, item, obj, pb, py, sourceSize);
    }
}

void AnnotationPainter::paintSelectionChrome(QPainter &painter, ImageView *view, ImageItem *item,
                                             const Annotation::Page &page,
                                             const QRectF &pageBounds, bool pageYUp,
                                             const QSize &sourceSize,
                                             const QVector<quint64> &selectedIds)
{
    if (!item || selectedIds.isEmpty()) {
        return;
    }
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QPen pen(QColor(53, 132, 228));
    pen.setCosmetic(true);
    pen.setWidthF(1.5);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (const Annotation::Object &o : page.objects) {
        if (!selectedIds.contains(o.id)) {
            continue;
        }
        if (o.kind == Annotation::Kind::HighlightQuad
            || o.kind == Annotation::Kind::ShapeRect
            || o.kind == Annotation::Kind::ShapeEllipse
            || o.kind == Annotation::Kind::StickyNote) {
            for (const QRectF &q : o.quads) {
                const QRectF scene =
                    pageRectToScene(view, item, q, pageBounds, pageYUp, sourceSize);
                if (!scene.isEmpty()) {
                    if (o.kind == Annotation::Kind::ShapeEllipse) {
                        painter.drawEllipse(scene);
                    } else {
                        painter.drawRect(scene);
                    }
                }
            }
            if (selectedIds.size() == 1 && !o.quads.isEmpty()) {
                const QRectF pageBox =
                    o.quads.size() == 1 ? o.quads.first() : unionOfQuads(o.quads);
                const QRectF scene =
                    pageRectToScene(view, item, pageBox, pageBounds, pageYUp, sourceSize);
                if (!scene.isEmpty()) {
                    const qreal hs = handleHalfScene(view);
                    painter.save();
                    painter.setBrush(QColor(255, 255, 255));
                    QPen hp(QColor(53, 132, 228), 0);
                    hp.setCosmetic(true);
                    hp.setWidthF(1.0);
                    painter.setPen(hp);
                    const QPointF corners[4] = {
                        scene.topLeft(), scene.topRight(), scene.bottomRight(),
                        scene.bottomLeft(),
                    };
                    for (const QPointF &c : corners) {
                        painter.drawRect(QRectF(c.x() - hs, c.y() - hs, hs * 2, hs * 2));
                    }
                    painter.restore();
                }
            }
        } else if (!o.points.isEmpty()) {
            QPainterPath path;
            bool first = true;
            QVector<QPointF> scenePts;
            scenePts.reserve(o.points.size());
            for (const QPointF &pp : o.points) {
                const QPointF scene =
                    pageToScene(view, item, pp, pageBounds, pageYUp, sourceSize);
                scenePts.append(scene);
                if (first) {
                    path.moveTo(scene);
                    first = false;
                } else {
                    path.lineTo(scene);
                }
            }
            painter.drawPath(path);
            if (selectedIds.size() == 1 && o.kind == Annotation::Kind::ShapeLine
                && scenePts.size() >= 2) {
                const qreal hs = handleHalfScene(view);
                painter.save();
                painter.setBrush(QColor(255, 255, 255));
                QPen hp(QColor(53, 132, 228), 0);
                hp.setCosmetic(true);
                hp.setWidthF(1.0);
                painter.setPen(hp);
                for (const QPointF &c : scenePts) {
                    painter.drawRect(QRectF(c.x() - hs, c.y() - hs, hs * 2, hs * 2));
                }
                painter.restore();
            } else {
                const qreal pad = 3.0 * scenePerDevicePx(view);
                const QRectF br = path.boundingRect().adjusted(-pad, -pad, pad, pad);
                painter.drawRect(br);
            }
        }
    }
    painter.restore();
}

qreal AnnotationPainter::handleHitRadiusPage(ImageView *view, ImageItem *item,
                                             const QRectF &pageBounds, bool pageYUp,
                                             const QSize &sourceSize)
{
    const qreal pu = pageUnitInScene(view, item, pageBounds, pageYUp, sourceSize);
    if (!(pu > 1e-9)) {
        return 12.0;
    }
    qreal vs = 1.0;
    qreal dpr = 1.0;
    if (view) {
        vs = ViewTransform::sanitizeViewScale(ViewTransform::scaleFrom(view->transform()));
        if (view->viewport()) {
            dpr = view->viewport()->devicePixelRatioF();
        }
    }
    if (!(dpr > 0.0)) {
        dpr = 1.0;
    }
    constexpr qreal kHitDevicePx = 12.0;
    const qreal sceneR = kHitDevicePx / (vs * dpr);
    return sceneR / pu;
}
