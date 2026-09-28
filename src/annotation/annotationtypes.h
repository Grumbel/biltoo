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
#include <QVector>
#include <QPair>
#include <cstdint>
#include <QtMath>

/**
 * Session annotation overlay (docs/ANNOTATION_OVERLAY.md).
 * Geometry is document page space (plain images: source pixels, Y-down).
 */
namespace Annotation {

enum class Kind : std::uint8_t {
    HighlighterStroke = 1,
    HighlightQuad = 2,
};

enum class Blend : std::uint8_t {
    Multiply = 1,
    SourceOver = 2,
};

enum class Tool : std::uint8_t {
    None = 0,
    FreehandHighlighter = 1,
    TextHighlighter = 2,
};

struct Object {
    quint64 id = 0;
    Kind kind = Kind::HighlighterStroke;
    Blend blend = Blend::Multiply;
    QColor color = QColor(246, 211, 45);
    qreal width = 18.0;
    QVector<QPointF> points; // freehand
    QVector<QRectF> quads;   // text-snapped page-space boxes
    QString textSnippet;     // optional, from regions at create
};

struct Page {
    SessionImageId sid = kInvalidSessionImageId;
    QRectF pageBounds;
    bool pageYUp = false;
    QVector<Object> objects;
};

inline QJsonObject objectToJson(const Object &o)
{
    QJsonObject j;
    j.insert(QStringLiteral("id"), static_cast<double>(o.id));
    j.insert(QStringLiteral("kind"), static_cast<int>(o.kind));
    j.insert(QStringLiteral("blend"), static_cast<int>(o.blend));
    j.insert(QStringLiteral("color"), o.color.name(QColor::HexArgb));
    j.insert(QStringLiteral("width"), o.width);
    if (!o.textSnippet.isEmpty()) {
        j.insert(QStringLiteral("text"), o.textSnippet);
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
    o.id = static_cast<quint64>(j.value(QStringLiteral("id")).toDouble());
    o.kind = static_cast<Kind>(j.value(QStringLiteral("kind")).toInt(1));
    o.blend = static_cast<Blend>(j.value(QStringLiteral("blend")).toInt(1));
    o.color = QColor(j.value(QStringLiteral("color")).toString(QStringLiteral("#fff6d32d")));
    o.width = j.value(QStringLiteral("width")).toDouble(18.0);
    o.textSnippet = j.value(QStringLiteral("text")).toString();
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
    j.insert(QStringLiteral("sid"), static_cast<double>(pg.sid));
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
    pg.sid = static_cast<SessionImageId>(j.value(QStringLiteral("sid")).toDouble());
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
    // Avoid Qt qBound(min/max assert) and NaN: clamp without Q_ASSERT.
    if (!(v > 0.0)) {
        return 0.0; // also maps NaN → 0
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
