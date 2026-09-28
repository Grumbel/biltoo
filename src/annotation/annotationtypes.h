// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONTYPES_H
#define ANNOTATIONTYPES_H

#include "imageview_types.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QPair>
#include <cstdint>
#include <QtMath>

/**
 * Session annotation overlay (docs/ANNOTATION_OVERLAY.md).
 *
 * Geometry is document page space (plain images: source pixels, Y-down).
 * Objects are pure data; SessionImageId is the entity key (same role as
 * ItemWorld sparse tables for appearance).
 *
 * On-disk: project JSON key "annotations" is either:
 *   - legacy: [ page, … ]  (array of pages; formatVersion implied 1)
 *   - current: { "formatVersion": 2, "pages": [ page, … ] }
 * Kind/blend accept string names (preferred) or historic integer enums.
 */
namespace Annotation {

/** Bump when on-disk page/object shape changes incompatibly. */
inline constexpr int kFormatVersion = 2;

enum class Kind : std::uint8_t {
    HighlighterStroke = 1,
    HighlightQuad = 2,
    InkStroke = 3,
    ShapeRect = 4,
    ShapeEllipse = 5,
    ShapeLine = 6,
    StickyNote = 7,
};

enum class Blend : std::uint8_t {
    Multiply = 1,
    SourceOver = 2,
};

enum class Tool : std::uint8_t {
    None = 0,
    FreehandHighlighter = 1,
    TextHighlighter = 2,
    Pen = 3,
    Eraser = 4,
    Select = 5,
    Rect = 6,
    Ellipse = 7,
    Line = 8,
    Sticky = 9,
};

/**
 * One markup object. Tagged by @a kind; geometry lives in @a points and/or
 * @a quads (data-driven: painters switch on kind, no per-kind subclasses).
 *
 * - Strokes (HighlighterStroke, InkStroke, ShapeLine): @a points
 * - Quads / shapes (HighlightQuad, ShapeRect, ShapeEllipse): @a quads
 * - StickyNote: single point in @a points (anchor); body in @a text
 * - textSnippet: optional OCR/native text captured at text-highlight create
 */
struct Object {
    quint64 id = 0;
    Kind kind = Kind::HighlighterStroke;
    Blend blend = Blend::Multiply;
    QColor color = QColor(246, 211, 45);
    qreal width = 18.0;
    QVector<QPointF> points;
    QVector<QRectF> quads;
    QString text;        /**< Sticky note body (and future free-text). */
    QString textSnippet; /**< Optional source text for HighlightQuad. */
};

struct Page {
    SessionImageId sid = kInvalidSessionImageId;
    QRectF pageBounds;
    bool pageYUp = false;
    QVector<Object> objects;
};

inline QString kindToString(Kind k)
{
    switch (k) {
    case Kind::HighlighterStroke:
        return QStringLiteral("highlighterStroke");
    case Kind::HighlightQuad:
        return QStringLiteral("highlightQuad");
    case Kind::InkStroke:
        return QStringLiteral("inkStroke");
    case Kind::ShapeRect:
        return QStringLiteral("shapeRect");
    case Kind::ShapeEllipse:
        return QStringLiteral("shapeEllipse");
    case Kind::ShapeLine:
        return QStringLiteral("shapeLine");
    case Kind::StickyNote:
        return QStringLiteral("stickyNote");
    }
    return QStringLiteral("highlighterStroke");
}

inline Kind kindFromString(const QString &s, Kind fallback = Kind::HighlighterStroke)
{
    if (s == QLatin1String("highlighterStroke")) {
        return Kind::HighlighterStroke;
    }
    if (s == QLatin1String("highlightQuad")) {
        return Kind::HighlightQuad;
    }
    if (s == QLatin1String("inkStroke")) {
        return Kind::InkStroke;
    }
    if (s == QLatin1String("shapeRect")) {
        return Kind::ShapeRect;
    }
    if (s == QLatin1String("shapeEllipse")) {
        return Kind::ShapeEllipse;
    }
    if (s == QLatin1String("shapeLine")) {
        return Kind::ShapeLine;
    }
    if (s == QLatin1String("stickyNote")) {
        return Kind::StickyNote;
    }
    return fallback;
}

inline Kind kindFromJson(const QJsonValue &v)
{
    if (v.isString()) {
        return kindFromString(v.toString());
    }
    const int n = v.toInt(1);
    if (n >= 1 && n <= 7) {
        return static_cast<Kind>(n);
    }
    return Kind::HighlighterStroke;
}

inline QString blendToString(Blend b)
{
    switch (b) {
    case Blend::Multiply:
        return QStringLiteral("multiply");
    case Blend::SourceOver:
        return QStringLiteral("sourceOver");
    }
    return QStringLiteral("multiply");
}

inline Blend blendFromJson(const QJsonValue &v)
{
    if (v.isString()) {
        const QString s = v.toString();
        if (s == QLatin1String("sourceOver")) {
            return Blend::SourceOver;
        }
        return Blend::Multiply;
    }
    const int n = v.toInt(1);
    if (n == static_cast<int>(Blend::SourceOver)) {
        return Blend::SourceOver;
    }
    return Blend::Multiply;
}

inline QJsonObject objectToJson(const Object &o)
{
    QJsonObject j;
    // Stable decimal string avoids double precision loss for large ids.
    j.insert(QStringLiteral("id"), QString::number(o.id));
    j.insert(QStringLiteral("kind"), kindToString(o.kind));
    j.insert(QStringLiteral("blend"), blendToString(o.blend));
    j.insert(QStringLiteral("color"), o.color.name(QColor::HexArgb));
    j.insert(QStringLiteral("width"), o.width);
    if (!o.text.isEmpty()) {
        j.insert(QStringLiteral("text"), o.text);
    }
    if (!o.textSnippet.isEmpty()) {
        j.insert(QStringLiteral("textSnippet"), o.textSnippet);
    }
    QJsonArray pts;
    for (const QPointF &p : o.points) {
        pts.append(QJsonArray{p.x(), p.y()});
    }
    if (!pts.isEmpty()) {
        j.insert(QStringLiteral("points"), pts);
    }
    QJsonArray qs;
    for (const QRectF &r : o.quads) {
        qs.append(QJsonArray{r.x(), r.y(), r.width(), r.height()});
    }
    if (!qs.isEmpty()) {
        j.insert(QStringLiteral("quads"), qs);
    }
    return j;
}

inline Object objectFromJson(const QJsonObject &j)
{
    Object o;
    const QJsonValue idVal = j.value(QStringLiteral("id"));
    if (idVal.isString()) {
        o.id = idVal.toString().toULongLong();
    } else {
        o.id = static_cast<quint64>(idVal.toDouble());
    }
    o.kind = kindFromJson(j.value(QStringLiteral("kind")));
    o.blend = blendFromJson(j.value(QStringLiteral("blend")));
    o.color = QColor(j.value(QStringLiteral("color")).toString(QStringLiteral("#fff6d32d")));
    o.width = j.value(QStringLiteral("width")).toDouble(18.0);
    o.text = j.value(QStringLiteral("text")).toString();
    o.textSnippet = j.value(QStringLiteral("textSnippet")).toString();
    // Legacy sticky / free-text used "text" for body; highlight used "text" for snippet.
    // If kind is sticky and textSnippet empty, body is already in text.
    // If kind is highlightQuad and textSnippet empty but text set, promote to snippet.
    if (o.kind == Kind::HighlightQuad && o.textSnippet.isEmpty() && !o.text.isEmpty()) {
        o.textSnippet = o.text;
        o.text.clear();
    }
    for (const QJsonValue &v : j.value(QStringLiteral("points")).toArray()) {
        const QJsonArray xy = v.toArray();
        if (xy.size() >= 2) {
            o.points.append(QPointF(xy.at(0).toDouble(), xy.at(1).toDouble()));
        }
    }
    for (const QJsonValue &v : j.value(QStringLiteral("quads")).toArray()) {
        const QJsonArray r = v.toArray();
        if (r.size() >= 4) {
            o.quads.append(QRectF(r.at(0).toDouble(), r.at(1).toDouble(),
                                  r.at(2).toDouble(), r.at(3).toDouble()));
        }
    }
    return o;
}

inline QJsonObject pageToJson(const Page &pg)
{
    QJsonObject j;
    j.insert(QStringLiteral("sid"), QString::number(static_cast<qulonglong>(pg.sid)));
    j.insert(QStringLiteral("pageYUp"), pg.pageYUp);
    QJsonObject b;
    b.insert(QStringLiteral("x"), pg.pageBounds.x());
    b.insert(QStringLiteral("y"), pg.pageBounds.y());
    b.insert(QStringLiteral("w"), pg.pageBounds.width());
    b.insert(QStringLiteral("h"), pg.pageBounds.height());
    j.insert(QStringLiteral("pageBounds"), b);
    QJsonArray objs;
    for (const Object &o : pg.objects) {
        objs.append(objectToJson(o));
    }
    j.insert(QStringLiteral("objects"), objs);
    return j;
}

inline Page pageFromJson(const QJsonObject &j)
{
    Page pg;
    const QJsonValue sidVal = j.value(QStringLiteral("sid"));
    if (sidVal.isString()) {
        pg.sid = static_cast<SessionImageId>(sidVal.toString().toULongLong());
    } else {
        pg.sid = static_cast<SessionImageId>(sidVal.toDouble());
    }
    pg.pageYUp = j.value(QStringLiteral("pageYUp")).toBool(false);
    const QJsonObject b = j.value(QStringLiteral("pageBounds")).toObject();
    pg.pageBounds = QRectF(b.value(QStringLiteral("x")).toDouble(),
                           b.value(QStringLiteral("y")).toDouble(),
                           b.value(QStringLiteral("w")).toDouble(),
                           b.value(QStringLiteral("h")).toDouble());
    for (const QJsonValue &v : j.value(QStringLiteral("objects")).toArray()) {
        pg.objects.append(objectFromJson(v.toObject()));
    }
    return pg;
}

/** Ramer–Douglas–Peucker thin for freehand (page units). */
inline qreal clamp01(qreal v)
{
    if (!(v > 0.0)) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

inline QVector<QPointF> simplifyPolyline(const QVector<QPointF> &pts, qreal epsilon)
{
    if (pts.size() < 3 || !(epsilon > 0)) {
        return pts;
    }
    QVector<bool> keep(pts.size(), false);
    keep[0] = true;
    keep[pts.size() - 1] = true;
    QVector<QPair<int, int>> stack;
    stack.append({0, pts.size() - 1});
    while (!stack.isEmpty()) {
        const auto range = stack.takeLast();
        const int i0 = range.first;
        const int i1 = range.second;
        if (i1 - i0 < 2) {
            continue;
        }
        const QPointF a = pts.at(i0);
        const QPointF b = pts.at(i1);
        const QPointF ab = b - a;
        const qreal ab2 = ab.x() * ab.x() + ab.y() * ab.y();
        qreal maxDist = 0;
        int maxIdx = i0;
        for (int i = i0 + 1; i < i1; ++i) {
            const QPointF p = pts.at(i);
            qreal dist = 0;
            if (!(ab2 > 1e-12)) {
                const QPointF d = p - a;
                dist = qSqrt(d.x() * d.x() + d.y() * d.y());
            } else {
                const qreal t = clamp01(
                    ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / ab2);
                const QPointF proj(a.x() + t * ab.x(), a.y() + t * ab.y());
                const QPointF d = p - proj;
                dist = qSqrt(d.x() * d.x() + d.y() * d.y());
            }
            if (dist > maxDist) {
                maxDist = dist;
                maxIdx = i;
            }
        }
        if (maxDist > epsilon && maxIdx > i0 && maxIdx < i1) {
            keep[maxIdx] = true;
            stack.append({i0, maxIdx});
            stack.append({maxIdx, i1});
        }
    }
    QVector<QPointF> out;
    out.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i) {
        if (keep.at(i)) {
            out.append(pts.at(i));
        }
    }
    return out.isEmpty() ? pts : out;
}

} // namespace Annotation

#endif // ANNOTATIONTYPES_H
