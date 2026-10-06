// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_plan_debug_overlay.hpp"

#include "display/displayquality.h"
#include "display/imagecache.h"
#include "util/debugflags.h"
#include <QFont>
#include <QFontMetrics>
#include <QtMath>

bool tilePlanDebugOverlayEnabled()
{
    return debugFlag(DebugFlags::Overlay) || debugFlag(DebugFlags::TileDebug);
}

void paintTilePlanDebugOverlay(QPainter *painter, tilelod::TileSession *session,
                               const tilelod::DrawPlan &plan,
                               const QRectF &contentBounds,
                               const QPointF &contentOffset,
                               const QSize &nativeSize,
                               const ContentXform::Value &xform,
                               bool freeRotPainter,
                               const QString &itemPath)
{
    if (!painter || !session) {
        return;
    }
    painter->save();
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Yellow = exact, orange = parent, magenta = EMB, cyan = LQIP, blue = hole.
    // Host-side coverage debug only. Durable TILE text on pixels belongs in
    // thumtoo (bitmap stamps rotate/flip with the patch automatically).
    constexpr int kWashAlpha = 90;
    const QColor fillExact(255, 220, 40, kWashAlpha);
    const QColor fillParent(255, 140, 20, kWashAlpha);
    const QColor fillEmb(220, 40, 200, kWashAlpha);   // magenta — EXIF/PDF thumb
    const QColor fillLqip(40, 200, 255, kWashAlpha);  // cyan — ThumbHash
    const QColor fillHole(80, 80, 200, kWashAlpha);   // blue — empty underlay
    const QColor edgeExact(255, 230, 60);
    const QColor edgeParent(255, 160, 40);
    const QColor edgeEmb(255, 80, 220);
    const QColor edgeLqip(60, 220, 255);
    const QColor edgeHole(120, 120, 255);

    const int target = session->target_scale();
    // Classify underlay once for the item (EMB vs LQIP vs none).
    QString underTag;
    {
        const QImage u = ImageCache::getUnderlay(itemPath);
        if (!u.isNull()) {
            const int le = ImageCache::longEdge(u);
            underTag = (le <= DisplayQuality::kLqipMaxEdge)
                ? QStringLiteral("LQIP")
                : QStringLiteral("EMB");
        } else if (session->has_lqip()) {
            underTag = QStringLiteral("LQIP");
        }
    }

    for (const tilelod::DrawCommand &cmd : plan.commands) {
        // Same mapping as tile paint: source content rect → oriented display.
        const QRectF srcBox(cmd.dst_content.x, cmd.dst_content.y,
                            cmd.dst_content.w, cmd.dst_content.h);
        if (srcBox.isEmpty()) {
            continue;
        }
        QRectF disp = ContentXform::mapSourceRectToOriented(srcBox, nativeSize, xform);
        if (disp.isEmpty()) {
            continue;
        }
        if (xform.hasCrop && !xform.cropRect.isEmpty()) {
            const QRect contentCrop =
                ContentXform::orientedCropRect(nativeSize, xform);
            if (contentCrop.width() < 1 || contentCrop.height() < 1) {
                continue;
            }
            const QRectF local = disp.translated(-contentCrop.x(), -contentCrop.y());
            const QRectF cropLocal(0.0, 0.0, contentCrop.width(), contentCrop.height());
            disp = local.intersected(cropLocal);
            if (disp.isEmpty()) {
                continue;
            }
        }
        // Axis-aligned paint adds item offset(); free-rot uses painter xform only.
        QRectF dst = disp;
        if (!freeRotPainter) {
            dst = QRectF(disp.x() + contentOffset.x(), disp.y() + contentOffset.y(),
                         disp.width(), disp.height());
        }
        if (dst.isEmpty()) {
            continue;
        }

        QColor fill;
        QColor edge;
        QString cellTag;
        if (cmd.kind == tilelod::DrawKind::ExactTile) {
            fill = fillExact;
            edge = edgeExact;
            cellTag = QStringLiteral("EXACT");
        } else if (cmd.kind == tilelod::DrawKind::CoarserTile) {
            fill = fillParent;
            edge = edgeParent;
            cellTag = QStringLiteral("PARENT");
        } else if (cmd.kind == tilelod::DrawKind::Underlay) {
            if (underTag == QLatin1String("EMB")) {
                fill = fillEmb;
                edge = edgeEmb;
                cellTag = QStringLiteral("EMB");
            } else if (underTag == QLatin1String("LQIP")) {
                fill = fillLqip;
                edge = edgeLqip;
                cellTag = QStringLiteral("LQIP");
            } else {
                fill = fillHole;
                edge = edgeHole;
                cellTag = QStringLiteral("HOLE");
            }
        } else {
            continue;
        }
        painter->fillRect(dst, fill);
        painter->setPen(QPen(edge, 0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(dst);

        // Cell tag in device space at a fixed pixel size. Dividing logical
        // setPixelSize by sx produced FreeType "render glyph failed err=62"
        // when zoomed out (huge logical px) or edge cases at extreme scale.
        const QRectF dstDev = painter->transform().mapRect(dst);
        if (qMin(dstDev.width(), dstDev.height()) >= 18.0 && !cellTag.isEmpty()) {
            painter->save();
            painter->resetTransform();
            QFont cf(QStringLiteral("Sans Serif"));
            cf.setBold(true);
            cf.setStyleHint(QFont::SansSerif);
            cf.setPixelSize(11);
            painter->setFont(cf);
            painter->setPen(QColor(0, 0, 0, 200));
            painter->drawText(dstDev.translated(1, 1), Qt::AlignCenter, cellTag);
            painter->setPen(edge);
            painter->drawText(dstDev, Qt::AlignCenter, cellTag);
            painter->restore();
        }
    }

    // Large centred summary over the whole content box — not a fixed ~14px
    // corner plate (unreadable when zoomed out / on large natives).
    // freeRotPainter: dests are crop-local; label the crop window. Else: item
    // contentRect (offset already in contentBounds).
    QRectF labelBox = contentBounds;
    if (freeRotPainter && xform.hasCrop && !xform.cropRect.isEmpty()) {
        const QRect oc = ContentXform::orientedCropRect(nativeSize, xform);
        labelBox = oc.isEmpty() ? QRectF(xform.cropRect.normalized())
                                : QRectF(oc);
    }
    if (labelBox.width() < 8.0 || labelBox.height() < 8.0) {
        painter->restore();
        return;
    }

    const tilelod::TileSession::Coverage cov = session->coverage();
    const tilelod::TileSession::Phase phase = session->phase();
    // Three lines so "s=N" is not glued to TILE (easier to read at large size).
    QStringList summary;
    summary << QStringLiteral("TILE");
    summary << QStringLiteral("s=%1").arg(target);
    switch (phase) {
    case tilelod::TileSession::Phase::Complete:
        summary << QStringLiteral("COMPLETE");
        break;
    case tilelod::TileSession::Phase::Loading:
        summary << QStringLiteral("LOADING %1/%2").arg(cov.ready).arg(cov.visible);
        summary << QStringLiteral("queued %1 retry %2").arg(cov.queued).arg(cov.retrying);
        break;
    case tilelod::TileSession::Phase::Degraded:
    case tilelod::TileSession::Phase::Error: {
        summary << QStringLiteral("%1 %2/%3 ready")
                       .arg(phase == tilelod::TileSession::Phase::Error
                                ? QStringLiteral("ERROR")
                                : QStringLiteral("DEGRADED"))
                       .arg(cov.ready)
                       .arg(cov.visible);
        summary << QStringLiteral("failed %1 unavailable %2")
                       .arg(cov.failed)
                       .arg(cov.unavailable);
        QString why = QString::fromStdString(session->first_error());
        if (why.size() > 72) {
            why = why.left(69) + QStringLiteral("...");
        }
        if (!why.isEmpty()) {
            summary << why;
        }
        break;
    }
    case tilelod::TileSession::Phase::Idle:
        summary << QStringLiteral("IDLE");
        break;
    }
    // EMB/LQIP in the summary only when the plan still has underlay holes —
    // not when every visible cell is EXACT (underlay may still sit under tiles
    // in the paint path, but it is not what you are looking at).
    int underCmds = 0;
    for (const tilelod::DrawCommand &c : plan.commands) {
        if (c.kind == tilelod::DrawKind::Underlay) {
            ++underCmds;
        }
    }
    if (underCmds > 0 && !underTag.isEmpty()) {
        summary << underTag;
    }

    const int nlines = summary.size();
    // Summary also in device space — fixed 14px, never extreme FreeType sizes.
    const QRectF labelDev = painter->transform().mapRect(labelBox);
    {
        painter->save();
        painter->resetTransform();
        QFont pf(QStringLiteral("Sans Serif"));
        pf.setWeight(QFont::Black);
        pf.setStyleHint(QFont::SansSerif);
        pf.setPixelSize(14);
        painter->setFont(pf);
        const QFontMetrics fm(pf);
        const int lineH = fm.height();
        const qreal totalH = lineH * nlines;
        const qreal y0 = labelDev.center().y() - totalH / 2.0;
        painter->setPen(QColor(0, 0, 0, 110));
        painter->setBrush(Qt::NoBrush);
        for (int i = 0; i < nlines; ++i) {
            const QString &line = summary.at(i);
            const QRectF lineRect(labelDev.left(), y0 + i * lineH, labelDev.width(),
                                  lineH);
            painter->drawText(lineRect, Qt::AlignHCenter | Qt::AlignVCenter, line);
        }
        painter->restore();
    }
    painter->restore();
}

