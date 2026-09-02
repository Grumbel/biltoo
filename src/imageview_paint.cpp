// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionGraphicsItem>

void ImageView::drawEdgeAffordances(QPainter &painter)
{
    if (m_hoverEdge == EdgeZone::None || !isImageMode()) {
        return;
    }
    if (m_hoverEdge != EdgeZone::GalleryReturn && !m_imageModeNavEnabled) {
        return;
    }

    const QRect vr = viewport()->rect();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int r = 22;
    constexpr int kEdgeMargin = 10;

    auto drawChevronButton = [&](int cx, int cy, auto buildChevron) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 140));
        painter.drawEllipse(QPoint(cx, cy), r, r);
        painter.setBrush(QColor(255, 255, 255, 230));
        painter.drawEllipse(QPoint(cx, cy), r - 3, r - 3);
        QPainterPath chevron;
        buildChevron(chevron, cx, cy);
        QPen pen(QColor(40, 40, 40), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.strokePath(chevron, pen);
    };

    if (m_hoverEdge == EdgeZone::GalleryReturn) {
        const int zone = edgeZoneHeight();
        QLinearGradient grad(0, 0, 0, zone);
        grad.setColorAt(0.0, QColor(0, 0, 0, 90));
        grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.fillRect(QRect(0, 0, vr.width(), zone), grad);
        const int cx = vr.center().x();
        const int cy = kEdgeMargin + r;
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px - 10, py + 5);
            chevron.lineTo(px, py - 6);
            chevron.lineTo(px + 10, py + 5);
        });
        return;
    }

    const int zone = edgeZoneWidth();
    const int cy = vr.center().y();
    QLinearGradient grad;
    if (m_hoverEdge == EdgeZone::Previous) {
        grad = QLinearGradient(0, 0, zone, 0);
        grad.setColorAt(0.0, QColor(0, 0, 0, 90));
        grad.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.fillRect(QRect(0, 0, zone, vr.height()), grad);
    } else {
        grad = QLinearGradient(vr.width() - zone, 0, vr.width(), 0);
        grad.setColorAt(0.0, QColor(0, 0, 0, 0));
        grad.setColorAt(1.0, QColor(0, 0, 0, 90));
        painter.fillRect(QRect(vr.width() - zone, 0, zone, vr.height()), grad);
    }

    const int cx = (m_hoverEdge == EdgeZone::Previous)
                       ? (kEdgeMargin + r)
                       : (vr.width() - kEdgeMargin - r);
    if (m_hoverEdge == EdgeZone::Previous) {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px + 5, py - 10);
            chevron.lineTo(px - 6, py);
            chevron.lineTo(px + 5, py + 10);
        });
    } else {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px - 5, py - 10);
            chevron.lineTo(px + 6, py);
            chevron.lineTo(px - 5, py + 10);
        });
    }
}

void ImageView::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);
    QPainter painter(viewport());
    // Workspace chrome in *viewport* device pixels (not scene drawForeground).
    // Painting here keeps handles a constant on-screen size under any view or
    // item scale — the same coordinate space as edge affordances and the HUD.
    if (m_cropMode) {
        paintCropOverlay(painter);
    } else if (isWorkspaceMode() && m_scene) {
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                // Only paint chrome for items we still own (guards against a
                // stale selection entry after destroyCanvasItem).
                if (ii->isInteractive() && m_items.contains(ii) && ii->scene() == m_scene) {
                    selected.append(ii);
                }
            }
        }
        std::sort(selected.begin(), selected.end(),
                  [](ImageItem *a, ImageItem *b) { return a->stackZ() < b->stackZ(); });
        if (selected.size() == 1) {
            selected.first()->paintInteractionChrome(&painter);
        } else if (selected.size() > 1) {
            // Multi-select: per-item outline only; group scale handles on the union.
            for (ImageItem *item : selected) {
                item->paintSelectionFrame(&painter);
            }
            paintGroupSelectionChrome(&painter, selected);
        }
        if (m_pageGuideVisible && m_pageGuideSelected) {
            paintPageGuideHandles(&painter);
        }
    }
    if (!m_cropMode && m_hoverEdge != EdgeZone::None && isImageMode()
        && (m_imageModeNavEnabled || m_hoverEdge == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }

    // Empty session: invite the user to open or drop images.
    if (m_items.isEmpty() && !hasClassicPath() && !m_cropMode) {
        painter.save();
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont titleFont = font();
        titleFont.setPointSize(qBound(12, titleFont.pointSize() + 4, 28));
        titleFont.setBold(true);
        QFont hintFont = font();
        hintFont.setPointSize(qBound(10, hintFont.pointSize() + 1, 20));
        const QString title = isWorkspaceMode()
            ? tr("Drop images here")
            : tr("Drop images here or open a file");
        const QString hint = isWorkspaceMode()
            ? tr("Drag files onto the canvas  ·  double-click a thumbnail to place")
            : tr("File → Open…  ·  Ctrl+O  ·  drag and drop");
        const QFontMetrics titleFm(titleFont);
        const QFontMetrics hintFm(hintFont);
        const int gap = 8;
        const int totalH = titleFm.height() + gap + hintFm.height();
        const int cy = viewport()->height() / 2 - totalH / 2;
        painter.setFont(titleFont);
        painter.setPen(QColor(220, 220, 220, 230));
        painter.drawText(QRect(0, cy, viewport()->width(), titleFm.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter, title);
        painter.setFont(hintFont);
        painter.setPen(QColor(180, 180, 180, 200));
        painter.drawText(QRect(0, cy + titleFm.height() + gap, viewport()->width(),
                               hintFm.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter, hint);

        // Edge-zone captions (Image mode uses these corners once a session is open).
        if (isImageMode() || (!isWorkspaceMode() && !isGalleryMode())) {
            QFont edgeFont = font();
            edgeFont.setPointSize(qBound(9, edgeFont.pointSize(), 16));
            painter.setFont(edgeFont);
            painter.setPen(QColor(160, 160, 160, 180));
            const QFontMetrics efm(edgeFont);
            const int em = 16;
            const int vw = viewport()->width();
            const int vh = viewport()->height();
            const QString prevLabel = tr("← Previous");
            const QString nextLabel = tr("Next →");
            const QString backLabel = tr("↑ Back to Gallery / Workspace");
            painter.drawText(QRect(em, vh / 2 - efm.height() / 2, efm.horizontalAdvance(prevLabel) + 8,
                                   efm.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, prevLabel);
            const int nextW = efm.horizontalAdvance(nextLabel) + 8;
            painter.drawText(QRect(vw - em - nextW, vh / 2 - efm.height() / 2, nextW, efm.height()),
                             Qt::AlignRight | Qt::AlignVCenter, nextLabel);
            const int backW = efm.horizontalAdvance(backLabel) + 8;
            painter.drawText(QRect((vw - backW) / 2, em, backW, efm.height()),
                             Qt::AlignHCenter | Qt::AlignTop, backLabel);
        }
        painter.restore();
    }
    // HUD layout:
    //   top-left  — transient actions (slideshow, fit, …), never Next/Prev
    //   top-right — session index [i/n]
    //   bottom    — filename (+ technical detail when the HUD is pinned)
    // Crop mode: always show a pinned “Crop mode” cue so the tool state is clear.
    if (m_cropMode || m_hudVisible || m_hudFlashVisible || m_hudIdentityPulse
        || !m_gallery.hoverPath().isEmpty()) {
        // Prefer the user preference (Preferences → HUD), not the widget font.
        QFont f = font();
        const int pt = qBound(8, m_hudFontPointSize, 48);
        f.setPointSize(pt);
        QFont boldF = f;
        boldF.setBold(true);
        const QFontMetrics fm(f);
        const QFontMetrics fmBold(boldF);
        const int margin = 10;
        const int pad = 8;
        const int lineGap = 2;
        const int viewW = viewport()->width();
        const int viewH = viewport()->height();

        auto wrapLine = [&](const QString &text, const QFontMetrics &metrics, int maxTextW) {
            QStringList out;
            if (text.isEmpty()) {
                return out;
            }
            if (metrics.horizontalAdvance(text) <= maxTextW) {
                out << text;
                return out;
            }
            const QString sep = QStringLiteral(" | ");
            const QStringList parts = text.split(sep, Qt::KeepEmptyParts);
            if (parts.size() <= 1) {
                out << metrics.elidedText(text, Qt::ElideMiddle, maxTextW);
                return out;
            }
            QString current;
            for (const QString &part : parts) {
                const QString candidate = current.isEmpty() ? part : current + sep + part;
                if (metrics.horizontalAdvance(candidate) <= maxTextW) {
                    current = candidate;
                    continue;
                }
                if (!current.isEmpty()) {
                    out << current;
                }
                if (metrics.horizontalAdvance(part) <= maxTextW) {
                    current = part;
                } else {
                    out << metrics.elidedText(part, Qt::ElideMiddle, maxTextW);
                    current.clear();
                }
            }
            if (!current.isEmpty()) {
                out << current;
            }
            return out;
        };

        struct HudLine {
            QString text;
            bool bold = false;
        };

        auto drawPanel = [&](const QList<HudLine> &lines, int anchorX, int anchorY,
                             bool fromRight, bool fromBottom) {
            if (lines.isEmpty()) {
                return;
            }
            const int maxBgW = qMax(40, viewW - 2 * margin);
            const int maxTextW = qMax(20, maxBgW - 2 * pad);
            QList<HudLine> drawn;
            int textW = 0;
            int textH = 0;
            for (const HudLine &hl : lines) {
                const QFontMetrics &m = hl.bold ? fmBold : fm;
                for (const QString &w : wrapLine(hl.text, m, maxTextW)) {
                    drawn.append({w, hl.bold});
                    // boundingRect undercounts some fonts; size with the same
                    // flags used for drawing and add a small safety margin.
                    const QRect br = m.boundingRect(QRect(0, 0, maxTextW, 1000),
                                                    Qt::AlignLeft | Qt::AlignVCenter
                                                        | Qt::TextSingleLine,
                                                    w);
                    textW = qMax(textW, br.width() + 2);
                    textH += qMax(m.height(), br.height());
                }
            }
            if (drawn.isEmpty()) {
                return;
            }
            if (drawn.size() > 1) {
                textH += lineGap * (drawn.size() - 1);
            }
            textW = qMin(textW, maxTextW);
            const int bgW = qMin(maxBgW, textW + 2 * pad);
            const int bgH = textH + 2 * pad;
            int x = fromRight ? (viewW - margin - bgW) : anchorX;
            int y = fromBottom ? (viewH - margin - bgH) : anchorY;
            // Keep fully on-screen
            x = qBound(margin, x, viewW - margin - bgW);
            y = qBound(margin, y, viewH - margin - bgH);
            const QRect bg(x, y, bgW, bgH);
            painter.setPen(Qt::NoPen);
            QColor panel = m_hudPanelColor;
            if (!panel.isValid() || panel.alpha() == 0) {
                panel = QColor(0, 0, 0, 160);
            }
            painter.setBrush(panel);
            painter.drawRoundedRect(bg, 6, 6);
            painter.setPen(m_hudTextColor.isValid() ? m_hudTextColor : QColor(240, 240, 240));
            int ty = bg.top() + pad;
            const int textAreaW = bg.width() - 2 * pad;
            for (const HudLine &hl : drawn) {
                const QFont &lf = hl.bold ? boldF : f;
                const QFontMetrics &m = hl.bold ? fmBold : fm;
                painter.setFont(lf);
                painter.drawText(QRect(bg.left() + pad, ty, textAreaW, m.height()),
                                 Qt::AlignLeft | Qt::AlignVCenter, hl.text);
                ty += m.height() + lineGap;
            }
        };

        // Top-left: crop mode cue (persistent while active) or transient flash.
        if (m_cropMode) {
            drawPanel({{tr("Crop mode"), true},
                       {tr("Handles · Reset · Apply · Esc"), false}},
                      margin, margin, false, false);
        } else if (m_hudFlashVisible && !m_hudAction.isEmpty()) {
            QString actionLine = m_hudAction;
            if (!m_hudDetail.isEmpty()) {
                actionLine += QLatin1Char(' ') + m_hudDetail;
            }
            drawPanel({{actionLine, true}}, margin, margin, false, false);
        }

        // Top-right: session index — pinned HUD or brief identity pulse after
        // user navigation. Not during pure action flashes (slideshow start, …)
        // and not on automatic slideshow advance (pulseIdentity=false).
        const QString badge = sessionBadgeText();
        if (!badge.isEmpty() && (m_hudVisible || m_hudIdentityPulse)) {
            drawPanel({{badge, true}}, 0, margin, true, false);
        }

        // Bottom: filename — pinned HUD, identity pulse after user nav, or gallery hover
        if (m_hudVisible || m_hudIdentityPulse || !m_gallery.hoverPath().isEmpty()) {
            QList<HudLine> bottom;
            const QString name = hudFileName();
            if (!name.isEmpty()) {
                bottom.append({name, true});
            }
            if (m_hudVisible) {
                const QString tech = statusText();
                if (!tech.isEmpty() && tech != name) {
                    bottom.append({tech, false});
                }
            }
            drawPanel(bottom, margin, 0, false, true);
        }
    }

    // Slideshow dwell: single device pixel along the bottom edge while the full
    // HUD is pinned. No track, no panel — only the filled fraction of the width.
    if (m_hudVisible && m_slideshowProgressActive && m_slideshowProgressIntervalMs > 0) {
        const qint64 elapsed = m_slideshowProgressElapsed.isValid()
            ? m_slideshowProgressElapsed.elapsed()
            : 0;
        qreal fraction = qreal(elapsed) / qreal(m_slideshowProgressIntervalMs);
        if (fraction < 0.0) {
            fraction = 0.0;
        } else if (fraction > 1.0) {
            fraction = 1.0;
        }
        const int viewW = viewport()->width();
        const int viewH = viewport()->height();
        if (viewW > 0 && viewH > 0 && fraction > 0.0) {
            const int barW = qMax(1, int(qRound(fraction * viewW)));
            QColor c = m_hudTextColor.isValid() ? m_hudTextColor : QColor(255, 255, 255);
            // Soft so it does not fight the image; still readable on dark or light.
            c.setAlpha(200);
            painter.setPen(Qt::NoPen);
            painter.setBrush(c);
            painter.drawRect(0, viewH - 1, barW, 1);
        }
    }
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    const bool useChecker =
        m_bgPattern == BackgroundPattern::Checkerboard
        && (!m_bgCheckerWorkspaceOnly || isWorkspaceMode());

    if (!useChecker) {
        painter->fillRect(rect, m_bgColor);
    } else {
        // Checkerboard in scene coordinates so it pans with the view. Cell size is
        // LOD-snapped so on-screen square size stays in a comfortable range: when
        // a 16-scene-unit cell would shrink below ~16 device px, double the scene
        // cell (and again) until squares are large enough — never draw a dense
        // field of sub-pixel checkers.
        const qreal viewScale = qMax(1e-6, transform().m11());
        constexpr qreal kBaseCell = 16.0;
        constexpr qreal kMinScreenPx = 16.0;
        qreal cell = kBaseCell;
        while (cell * viewScale < kMinScreenPx && cell < 4096.0) {
            cell *= 2.0;
        }

        const QColor a = m_bgColor;
        const QColor b = m_bgColorAlt.isValid() ? m_bgColorAlt : m_bgColor.lighter(120);

        const qreal x0 = std::floor(rect.left() / cell) * cell;
        const qreal y0 = std::floor(rect.top() / cell) * cell;
        const qreal x1 = std::ceil(rect.right() / cell) * cell;
        const qreal y1 = std::ceil(rect.bottom() / cell) * cell;

        for (qreal y = y0; y < y1; y += cell) {
            for (qreal x = x0; x < x1; x += cell) {
                const int ix = static_cast<int>(std::floor(x / cell));
                const int iy = static_cast<int>(std::floor(y / cell));
                const bool dark = ((ix + iy) & 1) != 0;
                painter->fillRect(QRectF(x, y, cell, cell), dark ? a : b);
            }
        }
    }

    // Page guide paper (under images): plain white sheet in scene units.
    if (m_pageGuideVisible && isWorkspaceMode()) {
        const QRectF page = pageGuideSceneRect();
        if (page.intersects(rect)) {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::white);
            painter->drawRect(page);
            painter->restore();
        }
    }
}

void ImageView::drawForeground(QPainter *painter, const QRectF &rect)
{
    // Page guide outline above images so the frame stays visible when tiles
    // cover the white sheet. Transform chrome stays in paintEvent (viewport space).
    if (m_pageGuideVisible && isWorkspaceMode()) {
        const QRectF page = pageGuideSceneRect();
        if (page.intersects(rect)) {
            painter->save();
            QPen pen(QColor(40, 100, 200, 220));
            pen.setStyle(Qt::DashLine);
            pen.setWidthF(0);
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(page);
            QRectF margin = page.adjusted(page.width() * 0.05, page.height() * 0.05,
                                          -page.width() * 0.05, -page.height() * 0.05);
            QPen marginPen(QColor(40, 100, 200, 120));
            marginPen.setStyle(Qt::DotLine);
            marginPen.setCosmetic(true);
            painter->setPen(marginPen);
            painter->drawRect(margin);
            painter->restore();
        }
    }
}

void ImageView::paintGroupSelectionChrome(QPainter *painter, const QList<ImageItem *> &items) const
{
    if (!painter) {
        return;
    }
    const QRectF sceneBounds = selectionSceneBounds(items);
    if (!sceneBounds.isValid() || sceneBounds.isEmpty()) {
        return;
    }
    const QRect viewRect = mapFromScene(sceneBounds).boundingRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Group chrome: violet family so it is distinct from single-select blue
    // and crop amber.
    const QColor frameCol(150, 90, 220, 220);
    const QColor handleFill(180, 120, 255, 240);
    const QColor handleFillHot(210, 160, 255, 255);
    const QColor handleEdge(80, 40, 140);
    const QColor rotFill(200, 140, 255);
    const QColor rotFillHot(255, 230, 120);

    QPen framePen(frameCol, 0);
    framePen.setCosmetic(true);
    framePen.setWidthF(1.75);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(viewRect);

    const QPointF pts[8] = {
        viewRect.topLeft(),
        QPointF(viewRect.center().x(), viewRect.top()),
        viewRect.topRight(),
        QPointF(viewRect.right(), viewRect.center().y()),
        viewRect.bottomRight(),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        viewRect.bottomLeft(),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    // Corners 0,2,4,6: rounded line-arc-line; edges 1,3,5,7: short bars.
    auto unit = [](QPointF v) {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB, int id) {
        const bool hot = (m_groupHoverHandle == id || m_groupHandle == id);
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal arm = hs * 1.35;
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter->setPen(hp);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
        if (hot) {
            QPen glow(handleFill, 0);
            glow.setCosmetic(true);
            glow.setWidthF(thick * 0.55);
            glow.setCapStyle(Qt::RoundCap);
            glow.setJoinStyle(Qt::RoundJoin);
            painter->setPen(glow);
            painter->drawPath(path);
        }
    };
    auto drawEdgeBar = [&](const QPointF &mid, const QPointF &along, int id) {
        const bool hot = (m_groupHoverHandle == id || m_groupHandle == id);
        const QPointF a = unit(along);
        const QPointF perp(-a.y(), a.x());
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal len = hs * 1.8;
        const qreal thick = hs * 0.35;
        QPen hp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.6 : 1.15);
        painter->setPen(hp);
        painter->setBrush(hot ? handleFillHot : handleFill);
        QPolygonF bar;
        bar << mid + a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) - perp * (thick / 2)
            << mid + a * (len / 2) - perp * (thick / 2);
        painter->drawPolygon(bar);
        painter->setBrush(Qt::NoBrush);
    };
    // pts: 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
    drawCorner(pts[0], QPointF(1, 0), QPointF(0, 1), 0);
    drawEdgeBar(pts[1], QPointF(1, 0), 1);
    drawCorner(pts[2], QPointF(-1, 0), QPointF(0, 1), 2);
    drawEdgeBar(pts[3], QPointF(0, 1), 3);
    drawCorner(pts[4], QPointF(-1, 0), QPointF(0, -1), 4);
    drawEdgeBar(pts[5], QPointF(1, 0), 5);
    drawCorner(pts[6], QPointF(1, 0), QPointF(0, -1), 6);
    drawEdgeBar(pts[7], QPointF(0, 1), 7);

    // Rotate knobs outside mid-edges (same offset language as single-item).
    constexpr qreal kRotateOffset = 28.0;
    const QPointF rot[4] = {
        QPointF(viewRect.center().x(), viewRect.top() - kRotateOffset),
        QPointF(viewRect.right() + kRotateOffset, viewRect.center().y()),
        QPointF(viewRect.center().x(), viewRect.bottom() + kRotateOffset),
        QPointF(viewRect.left() - kRotateOffset, viewRect.center().y()),
    };
    const QPointF edgeMid[4] = {
        QPointF(viewRect.center().x(), viewRect.top()),
        QPointF(viewRect.right(), viewRect.center().y()),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    QPen stem(frameCol, 0);
    stem.setCosmetic(true);
    stem.setWidthF(1.25);
    for (int i = 0; i < 4; ++i) {
        const int handleId = 8 + i;
        const bool hot = (m_groupHoverHandle == handleId || m_groupHandle == handleId);
        painter->setPen(stem);
        painter->drawLine(edgeMid[i], rot[i]);
        const qreal rad = hot ? 7.0 : 5.0;
        painter->setBrush(hot ? rotFillHot : rotFill);
        QPen rp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        rp.setCosmetic(true);
        rp.setWidthF(hot ? 1.6 : 1.15);
        painter->setPen(rp);
        painter->drawEllipse(rot[i], rad, rad);
    }

    painter->restore();
}


void ImageView::paintPageGuideHandles(QPainter *painter) const
{
    if (!painter || !m_pageGuideVisible || !m_pageGuideSelected) {
        return;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.isEmpty()) {
        return;
    }
    const QRect viewRect = mapFromScene(page).boundingRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Same language as group scale grips, in the page-guide blue family.
    const QColor handleEdge(20, 60, 120);
    const QColor handleHot(255, 255, 255);

    const QPointF pts[8] = {
        viewRect.topLeft(),
        QPointF(viewRect.center().x(), viewRect.top()),
        viewRect.topRight(),
        QPointF(viewRect.right(), viewRect.center().y()),
        viewRect.bottomRight(),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        viewRect.bottomLeft(),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    auto unit = [](QPointF v) {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB, int id) {
        const bool hot = (m_pageGuideHoverHandle == id || m_pageGuideDragHandle == id);
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal arm = hs * 1.35;
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? handleHot : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter->setPen(hp);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    };
    auto drawEdge = [&](const QPointF &c, const QPointF &along, int id) {
        const bool hot = (m_pageGuideHoverHandle == id || m_pageGuideDragHandle == id);
        const QPointF d = unit(along);
        const qreal hs = hot ? 11.0 : 9.0;
        const qreal half = hs * 1.1;
        QPen hp(hot ? handleHot : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(hs * (hot ? 0.42 : 0.32));
        hp.setCapStyle(Qt::RoundCap);
        painter->setPen(hp);
        painter->drawLine(c - d * half, c + d * half);
    };
    // Corners: 0 TL, 2 TR, 4 BR, 6 BL
    drawCorner(pts[0], pts[1] - pts[0], pts[7] - pts[0], 0);
    drawCorner(pts[2], pts[1] - pts[2], pts[3] - pts[2], 2);
    drawCorner(pts[4], pts[5] - pts[4], pts[3] - pts[4], 4);
    drawCorner(pts[6], pts[5] - pts[6], pts[7] - pts[6], 6);
    // Edges: 1 T, 3 R, 5 B, 7 L
    drawEdge(pts[1], pts[2] - pts[0], 1);
    drawEdge(pts[3], pts[4] - pts[2], 3);
    drawEdge(pts[5], pts[4] - pts[6], 5);
    drawEdge(pts[7], pts[6] - pts[0], 7);
    painter->restore();
}
