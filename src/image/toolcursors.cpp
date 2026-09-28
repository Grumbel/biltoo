// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "image/toolcursors.h"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSvgRenderer>

namespace {

/** Logical cursor size (Qt recommends 32; supported on all platforms). */
constexpr int kCursorLogical = 32;

struct CursorDef {
    const char *resource; // under :/icons/cursors/
    int hotX;             // logical pixels
    int hotY;
    Qt::CursorShape fallback;
};

QCursor loadCursor(const CursorDef &def)
{
    static QHash<QString, QCursor> cache;

    qreal dpr = 1.0;
    if (QGuiApplication::primaryScreen()) {
        dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    }
    if (dpr < 1.0) {
        dpr = 1.0;
    }
    // Snap to integer pixel sizes; cache key includes DPR bucket.
    const int dprBucket = qMax(1, qRound(dpr * 100));
    const QString key = QStringLiteral("%1@%2").arg(QLatin1String(def.resource)).arg(dprBucket);
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return it.value();
    }

    const QString path = QStringLiteral(":/icons/cursors/%1").arg(QLatin1String(def.resource));
    if (!QFile::exists(path)) {
        const QCursor fb(def.fallback);
        cache.insert(key, fb);
        return fb;
    }

    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        const QCursor fb(def.fallback);
        cache.insert(key, fb);
        return fb;
    }

    const int px = qMax(16, qRound(kCursorLogical * dpr));
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&p, QRectF(0, 0, kCursorLogical, kCursorLogical));
    }

    // Hotspot in device pixels (Qt expects pixmap coordinates).
    const int hotX = qBound(def.hotX * dpr);
    const int hotY = qBound(def.hotY * dpr);
    const QCursor cur(pm, hotX, hotY);
    cache.insert(key, cur);
    return cur;
}

} // namespace

namespace ToolCursors {

QCursor forTool(Tool tool)
{
    switch (tool) {
    case Tool::Pan:
        return panOpen();
    case Tool::Zoom:
        return loadCursor(CursorDef{"zoom.svg", 16, 16, Qt::CrossCursor});
    case Tool::Select:
    default:
        return loadCursor(CursorDef{"select.svg", 4, 4, Qt::ArrowCursor});
    }
}

QCursor panOpen()
{
    return loadCursor(CursorDef{"pan-open.svg", 16, 18, Qt::OpenHandCursor});
}

QCursor panClosed()
{
    return loadCursor(CursorDef{"pan-closed.svg", 16, 18, Qt::ClosedHandCursor});
}

QCursor forAnnotation(Annotation::Tool tool)
{
    switch (tool) {
    case Annotation::Tool::None:
        return QCursor(Qt::ArrowCursor);
    case Annotation::Tool::Select:
        return loadCursor(CursorDef{"select.svg", 4, 4, Qt::ArrowCursor});
    case Annotation::Tool::FreehandHighlighter:
        return loadCursor(CursorDef{"annot-highlighter.svg", 16, 16, Qt::CrossCursor});
    case Annotation::Tool::TextHighlighter:
        return loadCursor(CursorDef{"annot-text-hl.svg", 16, 16, Qt::CrossCursor});
    case Annotation::Tool::Pen:
        return loadCursor(CursorDef{"annot-pen.svg", 16, 16, Qt::CrossCursor});
    case Annotation::Tool::Eraser:
        return loadCursor(CursorDef{"annot-eraser.svg", 16, 16, Qt::CrossCursor});
    case Annotation::Tool::Rect:
        return loadCursor(CursorDef{"annot-rect.svg", 16, 16, Qt::CrossCursor});
    case Annotation::Tool::Ellipse:
        return loadCursor(CursorDef{"annot-ellipse.svg", 16, 16, Qt::CrossCursor});
    case Annotation::Tool::Line:
        return loadCursor(CursorDef{"annot-line.svg", 16, 16, Qt::CrossCursor});
    case Annotation::Tool::Sticky:
        return loadCursor(CursorDef{"annot-sticky.svg", 16, 16, Qt::CrossCursor});
    }
    return loadCursor(CursorDef{"crosshair.svg", 16, 16, Qt::CrossCursor});
}

QCursor cropOutside()
{
    return loadCursor(CursorDef{"crop.svg", 16, 16, Qt::CrossCursor});
}

QCursor attentionDraw()
{
    return loadCursor(CursorDef{"attention.svg", 16, 16, Qt::CrossCursor});
}

QCursor textSelect()
{
    return loadCursor(CursorDef{"text-select.svg", 16, 16, Qt::CrossCursor});
}

QCursor crosshair()
{
    return loadCursor(CursorDef{"crosshair.svg", 16, 16, Qt::CrossCursor});
}

} // namespace ToolCursors
