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
#include <cstdint>

/**
 * Session annotation overlay (docs/ANNOTATION_OVERLAY.md).
 * Geometry is document page space (plain images: source pixels, Y-down).
 */
namespace Annotation {

enum class Kind : std::uint8_t {
    HighlighterStroke = 1,
    // Phase B+: InkStroke, HighlightQuad, ShapeRect, …
};

enum class Blend : std::uint8_t {
    Multiply = 1,
    SourceOver = 2,
};

struct Object {
    quint64 id = 0;
    Kind kind = Kind::HighlighterStroke;
    Blend blend = Blend::Multiply;
    QColor color = QColor(246, 211, 45); // classic highlighter yellow
    qreal width = 18.0;                  // page/source units
    QVector<QPointF> points;             // page space
};

struct Page {
    SessionImageId sid = kInvalidSessionImageId;
    QRectF pageBounds; // axis-aligned page box used when objects were created
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
    QJsonArray pts;
    for (const QPointF &p : o.points) {
        QJsonArray xy;
        xy.append(p.x());
        xy.append(p.y());
        pts.append(xy);
    }
    j.insert(QStringLiteral("points"), pts);
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
    for (const QJsonValue &v : j.value(QStringLiteral("points")).toArray()) {
        const QJsonArray xy = v.toArray();
        if (xy.size() >= 2) {
            o.points.append(QPointF(xy.at(0).toDouble(), xy.at(1).toDouble()));
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

} // namespace Annotation

#endif // ANNOTATIONTYPES_H
