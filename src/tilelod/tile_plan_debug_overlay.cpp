// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_plan_debug_overlay.hpp"

#include "display/displayquality.h"
#include "display/imagecache.h"
#include "util/debugflags.h"
#include "view/viewtransform.h"

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
    const QTransform dt = painter->deviceTransform();
    const qreal sx = ViewTransform::scaleFrom(dt);

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

        // Cell tag: constant on-screen size (not proportional to cell).
        // setPixelSize is in painter logical units; deviceTransform scales them,
        // so divide by sx or labels explode when zoomed in (fine / negative s)
        // and vanish when zoomed out (coarse / large s).
        const qreal cellDev = qMin(dst.width(), dst.height()) * sx;
        if (cellDev >= 18.0 && !cellTag.isEmpty()) {
            constexpr qreal kTagDevicePx = 11.0;
            const int cpx = qMax(1, qRound(kTagDevicePx / qMax(sx, qreal(0.001))));
            QFont cf = painter->font();
            cf.setBold(true);
            cf.setStyleHint(QFont::SansSerif);
            cf.setPixelSize(cpx);
            painter->setFont(cf);
            painter->setPen(QColor(0, 0, 0, 200));
            painter->drawText(dst.adjusted(1, 1, 1, 1), Qt::AlignCenter, cellTag);
            painter->setPen(edge);
            painter->drawText(dst, Qt::AlignCenter, cellTag);
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
    // Three lines so "s=N" is not glued to TILE (easier to read at large size).
    QStringList summary;
    summary << QStringLiteral("TILE");
    summary << QStringLiteral("s=%1").arg(target);
    if (cov.fully_covered()) {
        summary << QStringLiteral("COMPLETE");
    } else if (cov.in_flight > 0) {
        summary << QStringLiteral("LOADING %1/%2")
                       .arg(cov.exact_succeeded)
                       .arg(cov.visible);
    } else if (cov.settled() && cov.failed > 0) {
        // Failed = tile request returned miss (encode deny, cancel, source
        // error). Negative s is live denser-than-layout (PDF/DjVu/EPUB).
        summary << QStringLiteral("FAILED %1/%2")
                       .arg(cov.failed)
                       .arg(cov.visible);
        if (target < 0) {
            summary << QStringLiteral("live denser");
        }
        if (cov.exact_succeeded == 0 && cov.failed == cov.visible) {
            summary << QStringLiteral("no cells");
        }
    } else {
        summary << QStringLiteral("WAITING %1/%2")
                       .arg(cov.exact_succeeded)
                       .arg(cov.visible);
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
    // Constant on-screen summary size (same compensation as cell tags).
    constexpr qreal kSummaryDevicePx = 14.0;
    int px = qMax(1, qRound(kSummaryDevicePx / qMax(sx, qreal(0.001))));

    QFont pf = painter->font();
    pf.setWeight(QFont::Black);
    pf.setStyleHint(QFont::SansSerif);
    pf.setFamily(QStringLiteral("Sans Serif"));
    pf.setPixelSize(px);
    painter->setFont(pf);

    // Shrink only if a line would exceed the content box in logical units.
    {
        qreal maxLineW = 0.0;
        const QFontMetrics fm(pf);
        for (const QString &line : summary) {
            maxLineW = qMax(maxLineW, qreal(fm.horizontalAdvance(line)));
        }
        if (maxLineW > labelBox.width() * 0.98 && maxLineW > 1.0) {
            px = qMax(1, qRound(px * (labelBox.width() * 0.98) / maxLineW));
            pf.setPixelSize(px);
            painter->setFont(pf);
        }
    }

    const QFontMetrics fm(pf);
    const int lineH = fm.height();
    const qreal totalH = lineH * nlines;
    const qreal y0 = labelBox.center().y() - totalH / 2.0;
    // Semi-transparent black, no outline — yellow stays thumtoo stamps.
    painter->setPen(QColor(0, 0, 0, 110));
    painter->setBrush(Qt::NoBrush);
    for (int i = 0; i < nlines; ++i) {
        const QString &line = summary.at(i);
        const QRectF lineRect(
            labelBox.left(),
            y0 + i * lineH,
            labelBox.width(),
            lineH);
        painter->drawText(lineRect, Qt::AlignHCenter | Qt::AlignVCenter, line);
    }
    painter->restore();
}

