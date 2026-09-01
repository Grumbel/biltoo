// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QSet>
#include <QThreadPool>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QWheelEvent>
#include <QRubberBand>
#include <QtMath>
#include <cmath>
#include <algorithm>
#include <cmath>

int ImageView::edgeZoneWidth() const
{
    return qMax(48, static_cast<int>(width() * 0.12));
}

int ImageView::edgeZoneHeight() const
{
    return qMax(40, static_cast<int>(height() * 0.10));
}

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    if (!isImageMode()) {
        return EdgeZone::None;
    }
    // Top strip: back to Gallery or Workspace (when Image was opened from there).
    // Takes priority over left/right so the upper corners still return.
    if (m_galleryReturnAvailable && viewPos.y() < edgeZoneHeight()) {
        return EdgeZone::GalleryReturn;
    }
    if (!m_imageModeNavEnabled) {
        return EdgeZone::None;
    }
    const int zone = edgeZoneWidth();
    if (viewPos.x() < zone) {
        return EdgeZone::Previous;
    }
    if (viewPos.x() > width() - zone) {
        return EdgeZone::Next;
    }
    return EdgeZone::None;
}

void ImageView::updateHoverEdge(const QPoint &viewPos)
{
    const EdgeZone zone = edgeZoneAt(viewPos);
    if (zone == m_hoverEdge) {
        return;
    }
    m_hoverEdge = zone;
    if (m_hoverEdge == EdgeZone::Previous || m_hoverEdge == EdgeZone::Next
        || m_hoverEdge == EdgeZone::GalleryReturn) {
        setCursor(Qt::PointingHandCursor);
    } else if (!m_panning && !m_rotating) {
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    viewport()->update();
}

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
    }
    if (!m_cropMode && m_hoverEdge != EdgeZone::None && isImageMode()
        && (m_imageModeNavEnabled || m_hoverEdge == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }

    // Empty session: invite the user to open or drop images.
    if (m_items.isEmpty() && m_classicPath.isEmpty() && !m_cropMode) {
        painter.save();
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont titleFont = font();
        titleFont.setPointSize(qBound(12, titleFont.pointSize() + 4, 28));
        titleFont.setBold(true);
        QFont hintFont = font();
        hintFont.setPointSize(qBound(10, hintFont.pointSize() + 1, 20));
        const QString title = tr("Drop images here or open a file");
        const QString hint = tr("File → Open…  ·  Ctrl+O  ·  drag and drop");
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
        painter.restore();
    }
    // HUD layout:
    //   top-left  — transient actions (slideshow, fit, …), never Next/Prev
    //   top-right — session index [i/n]
    //   bottom    — filename (+ technical detail when the HUD is pinned)
    // Crop mode: always show a pinned “Crop mode” cue so the tool state is clear.
    if (m_cropMode || m_hudVisible || m_hudFlashVisible || m_hudIdentityPulse
        || !m_galleryHoverPath.isEmpty()) {
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
        if (m_hudVisible || m_hudIdentityPulse || !m_galleryHoverPath.isEmpty()) {
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

void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    const QPointF scenePos = mapToScene(event->position().toPoint());
    emit filesDropped(event->mimeData()->urls(), event->modifiers(), scenePos);
    event->acceptProposedAction();
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



QRectF ImageView::selectionSceneBounds(const QList<ImageItem *> &items) const
{
    QRectF bounds;
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        const QRectF r = item->contentSceneRect();
        if (!r.isValid() || r.isEmpty()) {
            continue;
        }
        bounds = bounds.isValid() ? bounds.united(r) : r;
    }
    return bounds;
}

int ImageView::groupHandleAt(const QPoint &viewPos, const QList<ImageItem *> &items) const
{
    const QRectF sceneBounds = selectionSceneBounds(items);
    if (!sceneBounds.isValid() || sceneBounds.isEmpty()) {
        return -1;
    }
    const QRect viewRect = mapFromScene(sceneBounds).boundingRect();
    constexpr qreal kScaleHit = 10.0;
    constexpr qreal kRotateOffset = 28.0;
    constexpr qreal kRotateHit = 12.0;
    const QPointF corners[8] = {
        viewRect.topLeft(),
        QPointF(viewRect.center().x(), viewRect.top()),
        viewRect.topRight(),
        QPointF(viewRect.right(), viewRect.center().y()),
        viewRect.bottomRight(),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        viewRect.bottomLeft(),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    // Prefer rotate knobs (outside) so they are not stolen by edge scale hits.
    const QPointF rot[4] = {
        QPointF(viewRect.center().x(), viewRect.top() - kRotateOffset),    // 8 T
        QPointF(viewRect.right() + kRotateOffset, viewRect.center().y()),  // 9 R
        QPointF(viewRect.center().x(), viewRect.bottom() + kRotateOffset), // 10 B
        QPointF(viewRect.left() - kRotateOffset, viewRect.center().y()),   // 11 L
    };
    for (int i = 0; i < 4; ++i) {
        if (QLineF(QPointF(viewPos), rot[i]).length() <= kRotateHit) {
            return 8 + i;
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (QLineF(QPointF(viewPos), corners[i]).length() <= kScaleHit) {
            return i;
        }
    }
    return -1;
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

bool ImageView::beginGroupScale(int handle, const QList<ImageItem *> &items)
{
    if (handle < 0 || items.size() < 2) {
        return false;
    }
    const QRectF bounds = selectionSceneBounds(items);
    if (!bounds.isValid() || bounds.isEmpty()) {
        return false;
    }
    m_groupHandle = handle;
    m_groupScaleDrag = !isGroupRotateHandle(handle);
    m_groupRotateDrag = isGroupRotateHandle(handle);
    m_groupBoundsStart = bounds;
    m_groupCenterStart = bounds.center();
    m_groupDragItems = items;
    m_groupDragStartStates.clear();
    for (ImageItem *item : items) {
        m_groupDragStartStates.append(captureState(item));
    }
    return true;
}

void ImageView::updateGroupScale(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupScaleDrag || m_groupDragItems.isEmpty()
        || m_groupDragStartStates.size() != m_groupDragItems.size()) {
        return;
    }
    // Drop any pointers no longer on our canvas (deleted mid-drag).
    for (int i = m_groupDragItems.size() - 1; i >= 0; --i) {
        ImageItem *item = m_groupDragItems.at(i);
        if (!item || !m_items.contains(item) || item->scene() != m_scene) {
            m_groupDragItems.removeAt(i);
            m_groupDragStartStates.removeAt(i);
        }
    }
    if (m_groupDragItems.isEmpty()) {
        endGroupScale();
        return;
    }
    const QRectF b = m_groupBoundsStart;
    // Fixed opposite corner / edge as anchor
    QPointF anchor = m_groupCenterStart;
    switch (m_groupHandle) {
    case 0: anchor = b.bottomRight(); break; // TL
    case 1: anchor = QPointF(b.center().x(), b.bottom()); break; // T
    case 2: anchor = b.bottomLeft(); break; // TR
    case 3: anchor = QPointF(b.left(), b.center().y()); break; // R
    case 4: anchor = b.topLeft(); break; // BR
    case 5: anchor = QPointF(b.center().x(), b.top()); break; // B
    case 6: anchor = b.topRight(); break; // BL
    case 7: anchor = QPointF(b.right(), b.center().y()); break; // L
    default: break;
    }

    qreal sx = 1.0;
    qreal sy = 1.0;
    const qreal eps = 1.0;
    switch (m_groupHandle) {
    case 0: // TL
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        break;
    case 1: // T
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        sx = (mods & Qt::ShiftModifier) ? sy : 1.0;
        break;
    case 2: // TR
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        break;
    case 3: // R
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = (mods & Qt::ShiftModifier) ? sx : 1.0;
        break;
    case 4: // BR
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        break;
    case 5: // B
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        sx = (mods & Qt::ShiftModifier) ? sy : 1.0;
        break;
    case 6: // BL
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        break;
    case 7: // L
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = (mods & Qt::ShiftModifier) ? sx : 1.0;
        break;
    default:
        break;
    }
    // Corners: uniform scale unless Shift (free axes).
    if (m_groupHandle == 0 || m_groupHandle == 2 || m_groupHandle == 4 || m_groupHandle == 6) {
        if (!(mods & Qt::ShiftModifier)) {
            const qreal s = (qAbs(sx) + qAbs(sy)) * 0.5;
            sx = (sx < 0 ? -1 : 1) * s;
            sy = (sy < 0 ? -1 : 1) * s;
        }
    }
    sx = qBound(0.05, qAbs(sx), 20.0) * (sx < 0 ? -1.0 : 1.0);
    sy = qBound(0.05, qAbs(sy), 20.0) * (sy < 0 ? -1.0 : 1.0);
    // Disallow negative scale (mirrors) for group — keep positive.
    sx = qAbs(sx);
    sy = qAbs(sy);

    if (!qIsFinite(sx) || !qIsFinite(sy) || !qIsFinite(anchor.x()) || !qIsFinite(anchor.y())) {
        return;
    }
    for (int i = 0; i < m_groupDragItems.size(); ++i) {
        ImageItem *item = m_groupDragItems.at(i);
        const WorkspaceItemState &st = m_groupDragStartStates.at(i);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        const QPointF rel = st.pos - anchor;
        const QPointF newPos = anchor + QPointF(rel.x() * sx, rel.y() * sy);
        if (!qIsFinite(newPos.x()) || !qIsFinite(newPos.y())) {
            continue;
        }
        item->setPos(newPos);
        const qreal baseX = st.scale > 0 ? st.scale : 1.0;
        const qreal baseY = st.scaleY > 0 ? st.scaleY : baseX;
        const qreal nx = baseX * sx;
        const qreal ny = baseY * sy;
        if (!qIsFinite(nx) || !qIsFinite(ny)) {
            continue;
        }
        item->setItemScale(nx, ny);
    }
    m_fitMode = false;
    emit statusChanged();
}

void ImageView::endGroupScale()
{
    // Undo is committed from mouseReleaseEvent (TransformCommand is local there).
    m_groupScaleDrag = false;
    m_groupRotateDrag = false;
    m_groupHandle = -1;
    m_groupHoverHandle = -1;
    m_groupDragItems.clear();
    m_groupDragStartStates.clear();
}

void ImageView::updateGroupRotate(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupRotateDrag || m_groupDragItems.isEmpty()
        || m_groupDragStartStates.size() != m_groupDragItems.size()) {
        return;
    }
    for (int i = m_groupDragItems.size() - 1; i >= 0; --i) {
        ImageItem *item = m_groupDragItems.at(i);
        if (!item || !m_items.contains(item) || item->scene() != m_scene) {
            m_groupDragItems.removeAt(i);
            m_groupDragStartStates.removeAt(i);
        }
    }
    if (m_groupDragItems.isEmpty()) {
        return;
    }

    const QPointF centre = m_groupCenterStart;
    // Angle from group centre to pointer; seed from first press stored in
    // m_groupPressScenePos when the drag starts (set in mouse path).
    const QPointF v0 = m_groupPressScenePos - centre;
    const QPointF v1 = scenePos - centre;
    if (QLineF(QPointF(0, 0), v0).length() < 1e-3) {
        return;
    }
    qreal delta = qRadiansToDegrees(qAtan2(v1.y(), v1.x()) - qAtan2(v0.y(), v0.x()));
    if (mods & Qt::ControlModifier) {
        delta = qRound(delta / 90.0) * 90.0;
    } else if (mods & Qt::ShiftModifier) {
        delta = qRound(delta / 45.0) * 45.0;
    }
    const qreal rad = qDegreesToRadians(delta);
    const qreal c = qCos(rad);
    const qreal s = qSin(rad);

    for (int i = 0; i < m_groupDragItems.size(); ++i) {
        ImageItem *item = m_groupDragItems.at(i);
        const WorkspaceItemState &st = m_groupDragStartStates.at(i);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        // Orbit position around group centre; add the same delta to placement angle.
        const QPointF rel = st.pos - centre;
        const QPointF newPos(centre.x() + rel.x() * c - rel.y() * s,
                             centre.y() + rel.x() * s + rel.y() * c);
        if (!qIsFinite(newPos.x()) || !qIsFinite(newPos.y())) {
            continue;
        }
        item->setPos(newPos);
        item->setItemRotation(st.rotation + delta);
    }
    m_fitMode = false;
    emit statusChanged();
}

void ImageView::updateGalleryHoverAt(const QPoint &viewPos)
{
    if (!isGalleryMode() || !m_scene) {
        if (!m_galleryHoverPath.isEmpty()) {
            m_galleryHoverPath.clear();
            viewport()->update();
        }
        return;
    }
    QString path;
    const QPointF scenePos = mapToScene(viewPos);
    for (QGraphicsItem *gi : m_scene->items(scenePos)) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            path = ii->path();
            break;
        }
    }
    if (path != m_galleryHoverPath) {
        m_galleryHoverPath = path;
        viewport()->update();
    }
}

void ImageView::updateMouseInfo(const QPoint &viewPos)

{
    ImageMouseInfo info;
    const QPointF scenePos = mapToScene(viewPos);

    // Prefer the topmost item under the cursor
    ImageItem *hit = nullptr;
    const QList<QGraphicsItem *> hits = m_scene->items(scenePos);
    for (QGraphicsItem *gi : hits) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = item;
            break;
        }
    }

    if (hit) {
        const QPoint pixel = hit->pixelAtScenePos(scenePos);
        if (pixel.x() >= 0) {
            info.valid = true;
            info.imagePos = pixel;
            info.pixelColor = hit->colorAtPixel(pixel);
            info.path = hit->path();
        }
    }

    if (info.valid != m_mouseInfo.valid
        || info.imagePos != m_mouseInfo.imagePos
        || info.pixelColor != m_mouseInfo.pixelColor
        || info.path != m_mouseInfo.path) {
        m_mouseInfo = info;
        emit mouseInfoChanged(m_mouseInfo);
    }
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    // Gallery: wheel only scrolls the packed scene. Zoom belongs to Image mode
    // (and Workspace). Never scale the view from the wheel here — that felt
    // random when overflow was small or Ctrl was held accidentally.
    if (isGalleryMode()) {
        QScrollBar *hBar = horizontalScrollBar();
        QScrollBar *vBar = verticalScrollBar();
        const bool canH = hBar && hBar->maximum() > hBar->minimum();
        const bool canV = vBar && vBar->maximum() > vBar->minimum();

        int dx = 0;
        int dy = 0;
        if (!event->pixelDelta().isNull()) {
            dx = event->pixelDelta().x();
            dy = event->pixelDelta().y();
        } else {
            // angleDelta is in eighths of a degree; 120 ≈ one notch.
            dx = event->angleDelta().x();
            dy = event->angleDelta().y();
        }

        // Shift+wheel → prefer horizontal (common UI convention).
        if (event->modifiers() & Qt::ShiftModifier) {
            if (dx == 0 && dy != 0) {
                dx = dy;
                dy = 0;
            }
        }

        // Horizontal strip layouts: vertical wheel pans sideways.
        const bool preferHorizontalScroll =
            m_layoutMode == LayoutMode::SideBySide
            || m_layoutMode == LayoutMode::MasonryRows;

        if (preferHorizontalScroll && dx == 0 && dy != 0) {
            dx = dy;
            dy = 0;
        } else if (dx == 0 && dy != 0 && !canV && canH) {
            dx = dy;
            dy = 0;
        } else if (dy == 0 && dx != 0 && !canH && canV) {
            dy = dx;
            dx = 0;
        }

        if (canH && dx != 0) {
            hBar->setValue(hBar->value() - dx);
        }
        if (canV && dy != 0) {
            vBar->setValue(vBar->value() - dy);
        }
        // Accept even at scroll ends so the event does not fall through to zoom.
        event->accept();
        return;
    }

    const qreal factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);

    // Image mode and free-form Workspace: zoom the view about the cursor.
    // Do not touch selected-item geometry here — prepareGeometryChange on
    // handle pads was expanding AABBs and fighting the user's pan/zoom.
    m_fitMode = false;
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    viewport()->update(); // refresh viewport-space chrome at the new scale
    emit statusChanged();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_applyingLayout || m_galleryRelayoutSuppressCount > 0) {
        return;
    }
    if (isGalleryMode() && !m_items.isEmpty()) {
        // Defer so scrollbar/geometry changes from setSceneRect settle first.
        // Suppressed during session delete so thumb-strip geometry changes do not
        // repack the gallery or jump scroll.
        scheduleApplyLayout();
        return;
    }
    if (m_fitMode && m_items.size() == 1) {
        fitItem(m_items.first(), currentFitAspectMode());
    }
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    // Crop mode: handles adjust the draft rect; drag on image starts rubber-band.
    if (m_cropMode && event->button() == Qt::LeftButton) {
        const CropHandle h = cropHandleAt(event->pos());
        if (h == CropHandle::Reset) {
            // Expand draft to the full image; Enter/Apply commits a cleared session crop.
            if (ImageItem *item = cropTargetItem()) {
                m_cropRect = item->contentRect();
                ensureCropRectValid();
                viewport()->update();
            }
            event->accept();
            return;
        }
        if (h == CropHandle::Apply) {
            applyCrop();
            event->accept();
            return;
        }
        if (h != CropHandle::None) {
            beginCropHandleDrag(h, event->pos());
            event->accept();
            return;
        }
        // Middle/Alt still pan; plain left on the image body → new rubber-band crop.
        if (!(event->modifiers()
              & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier))) {
            beginCropRubberBand(event->pos());
            if (m_cropRubberBanding) {
                event->accept();
                return;
            }
        }
    }

    // One-shot rubber-band zoom (Z): capture the region before other tools.
    if (m_zoomRegionArmed && event->button() == Qt::LeftButton) {
        m_zoomRegionDragging = true;
        m_zoomRegionOrigin = event->pos();
        if (!m_zoomRubberBand) {
            m_zoomRubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
        }
        m_zoomRubberBand->setGeometry(QRect(m_zoomRegionOrigin, QSize()));
        m_zoomRubberBand->show();
        event->accept();
        return;
    }

    // Workspace chrome hit-testing is view-owned (DOMAIN: free-object transforms).
    // Chrome is painted above all tiles in viewport space; hit-testing must
    // similarly ignore scene z-order of other images under the pointer.
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && m_tool == Tool::Select) {
        const QPointF scenePos = mapToScene(event->pos());
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    selected.append(ii);
                }
            }
        }
        if (selected.size() > 1) {
            // Multi-select: group frame only (no per-item handles).
            const int gh = groupHandleAt(event->pos(), selected);
            if (gh >= 0 && beginGroupScale(gh, selected)) {
                m_groupPressScenePos = mapToScene(event->pos());
                event->accept();
                return;
            }
        } else if (selected.size() == 1) {
            // Single selection: test that item's handles first, even when the
            // pointer is over another tile's pixmap (handles are drawn on top).
            ImageItem *item = selected.first();
            if (item->beginHandleInteraction(scenePos, event->modifiers())) {
                m_handleDragItem = item;
                m_dragItem = item;
                m_dragStartState = captureState(item);
                event->accept();
                return;
            }
        }
        // No handle hit — fall through to move/select / clear.
    }

    // Image mode: edge clicks — top returns to Gallery/Workspace; left/right navigate
    if (isImageMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        const EdgeZone zone = edgeZoneAt(event->pos());
        if (zone == EdgeZone::GalleryReturn) {
            emit galleryReturnRequested();
            event->accept();
            return;
        }
        if (zone == EdgeZone::Previous) {
            emit navigatePreviousRequested();
            event->accept();
            return;
        }
        if (zone == EdgeZone::Next) {
            emit navigateNextRequested();
            event->accept();
            return;
        }
    }

    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton
            && ((isImageMode() && m_imageModeLeftDragPan)
                || (isWorkspaceMode() && m_tool == Tool::Pan)
                || (isGalleryMode() && (event->modifiers() & Qt::AltModifier))
                || (event->modifiers() & Qt::AltModifier)))) {
        if (!(isWorkspaceMode() && (event->modifiers() & Qt::ShiftModifier)
              && event->button() == Qt::LeftButton)) {
            m_panning = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Workspace only: Shift + left button free-rotates (unless the press is on
    // a selected item's scale/chrome handle — those use Shift for opposite-edge scale).
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ShiftModifier)) {
        ImageItem *hit = nullptr;
        const QPointF scenePos = mapToScene(event->pos());
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = ii;
                break;
            }
        }
        if (!hit) {
            hit = targetItem();
        }
        if (hit && hit->isSelected() && hit->hasHandleAt(hit->mapFromScene(scenePos))) {
            // Fall through to QGraphicsView → ImageItem handle interaction.
        } else if (hit) {
            m_rotating = true;
            m_rotateItem = hit;
            m_rotateStartAngle = angleAt(scenePos, hit);
            m_rotateItemStart = hit->itemRotation();
            m_dragStartState = captureState(hit);
            m_scene->clearSelection();
            hit->setSelected(true);
            setCursor(Qt::CrossCursor);
            event->accept();
            return;
        }
    }

    // Gallery right-click: do not let QGraphicsView alter selection (that
    // cancels multi-select before the context menu opens). If the click is on
    // an unselected tile, select only that tile; if it is already selected,
    // keep the current multi-select for bulk rotate/flip/delete.
    if (isGalleryMode() && event->button() == Qt::RightButton) {
        const QPointF scenePos = mapToScene(event->pos());
        ImageItem *hit = nullptr;
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = ii;
                break;
            }
        }
        if (hit && !hit->isSelected()) {
            m_scene->clearSelection();
            hit->setSelected(true);
            m_gallerySelectionAnchor = hit;
            emit galleryItemFocused(hit->path());
            emit statusChanged();
        }
        event->accept();
        return;
    }

    // Gallery: classic multi-select (click / Ctrl / Shift); open is double-click.
    if (isGalleryMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & Qt::AltModifier)) {
        const QPointF scenePos = mapToScene(event->pos());
        ImageItem *hit = nullptr;
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = ii;
                break;
            }
        }
        // Ctrl (and Meta on platforms where that is the multi-select modifier)
        // toggles membership without clearing the rest of the selection.
        const bool ctrl = event->modifiers()
                          & (Qt::ControlModifier | Qt::MetaModifier);
        const bool shift = event->modifiers() & Qt::ShiftModifier;

        if (hit && shift && m_gallerySelectionAnchor) {
            // Session-order range from anchor to hit (inclusive).
            int i0 = m_items.indexOf(m_gallerySelectionAnchor);
            int i1 = m_items.indexOf(hit);
            if (i0 < 0) {
                i0 = i1;
            }
            if (i1 < 0) {
                i1 = i0;
            }
            if (i0 > i1) {
                std::swap(i0, i1);
            }
            m_scene->clearSelection();
            for (int i = i0; i <= i1 && i < m_items.size(); ++i) {
                m_items.at(i)->setSelected(true);
            }
            emit galleryItemFocused(hit->path());
            event->accept();
            emit statusChanged();
            return;
        }

        if (hit && ctrl) {
            hit->setSelected(!hit->isSelected());
            if (hit->isSelected()) {
                m_gallerySelectionAnchor = hit;
            }
            emit galleryItemFocused(hit->path());
            event->accept();
            emit statusChanged();
            return;
        }

        if (hit) {
            m_scene->clearSelection();
            hit->setSelected(true);
            m_gallerySelectionAnchor = hit;
            emit galleryItemFocused(hit->path());
            event->accept();
            emit statusChanged();
            return;
        }

        // Empty space: clear selection (keep Ctrl-additive empty no-ops).
        if (!ctrl) {
            m_scene->clearSelection();
            emit statusChanged();
        }
        // Allow rubber-band start via base class when drag mode is RubberBandDrag.
        QGraphicsView::mousePressEvent(event);
        return;
    }

    // Workspace Select tool: let QGraphicsView handle selection / move
    if (isWorkspaceMode() && event->button() == Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        // Capture drag start for undo when an item is selected under the cursor
        if (ImageItem *hit = targetItem()) {
            m_dragItem = hit;
            m_dragStartState = captureState(hit);
        }
        emit statusChanged();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_cropMode && m_cropActiveHandle != CropHandle::None) {
        updateCropHandleDrag(event->pos());
        event->accept();
        return;
    }
    if (m_cropMode && m_cropRubberBanding) {
        updateCropRubberBand(event->pos());
        event->accept();
        return;
    }
    // Middle-button (or Alt) pan must work in crop mode — handle before crop hover.
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (m_cropMode) {
        const CropHandle h = cropHandleAt(event->pos());
        if (h != m_cropHoverHandle) {
            m_cropHoverHandle = h;
            viewport()->update();
        }
        switch (h) {
        case CropHandle::Left:
        case CropHandle::Right:
            viewport()->setCursor(Qt::SizeHorCursor);
            break;
        case CropHandle::Top:
        case CropHandle::Bottom:
            viewport()->setCursor(Qt::SizeVerCursor);
            break;
        case CropHandle::TopLeft:
        case CropHandle::BottomRight:
            viewport()->setCursor(Qt::SizeFDiagCursor);
            break;
        case CropHandle::TopRight:
        case CropHandle::BottomLeft:
            viewport()->setCursor(Qt::SizeBDiagCursor);
            break;
        case CropHandle::Reset:
        case CropHandle::Apply:
            viewport()->setCursor(Qt::PointingHandCursor);
            break;
        case CropHandle::None:
            viewport()->setCursor(Qt::CrossCursor);
            break;
        }
        updateMouseInfo(event->pos());
        event->accept();
        return;
    }

    if (m_zoomRegionDragging && m_zoomRubberBand) {
        m_zoomRubberBand->setGeometry(QRect(m_zoomRegionOrigin, event->pos()).normalized());
        event->accept();
        return;
    }

    updateMouseInfo(event->pos());

    if (m_groupScaleDrag) {
        updateGroupScale(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return;
    }
    if (m_groupRotateDrag) {
        updateGroupRotate(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return;
    }

    if (m_handleDragItem && m_handleDragItem->hasActiveHandle()) {
        m_handleDragItem->updateHandleInteraction(mapToScene(event->pos()),
                                                    event->modifiers());
        viewport()->update(); // live chrome while scaling/rotating
        event->accept();
        return;
    }

    if (m_rotating && m_rotateItem) {
        const QPointF scenePos = mapToScene(event->pos());
        const qreal angle = angleAt(scenePos, m_rotateItem);
        const qreal delta = angle - m_rotateStartAngle;
        qreal rot = m_rotateItemStart + delta;
        if (event->modifiers() & Qt::ControlModifier) {
            rot = qRound(rot / 90.0) * 90.0;
        } else if (event->modifiers() & Qt::ShiftModifier) {
            // Shift is held to start free-rotate; add Ctrl for 90°, alone keep smooth
            // unless also... User asked Ctrl/Shift snap. Shift starts rotate so
            // during shift-drag, snap to 45 when Shift still held without wanting smooth.
            // Use Ctrl=90 always; for shift-drag path Shift is always down — snap 45.
            rot = qRound(rot / 45.0) * 45.0;
        }
        m_rotateItem->setItemRotation(rot);
        m_fitMode = false;
        emit statusChanged();
        event->accept();
        return;
    }

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        // Grow the free-form sceneRect with the view so middle-drag is never
        // clamped against a stale zero-range scrollbar.
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (isImageMode()) {
        updateHoverEdge(event->pos());
    }

    m_lastHoverViewPos = event->pos();
    updateGalleryHoverAt(m_lastHoverViewPos);

    // Workspace: drive handle hover from the view so highlight matches the
    // view-owned hit path (rotated / covered items included).
    if (isWorkspaceMode() && m_tool == Tool::Select && !m_handleDragItem
        && !m_groupScaleDrag && !m_groupRotateDrag && !m_panning) {
        const QPointF scenePos = mapToScene(event->pos());
        QList<ImageItem *> candidates;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    candidates.append(ii);
                }
            }
        }

        // Multi-select: only group handles (individual chrome is hidden).
        if (candidates.size() > 1) {
            for (ImageItem *item : candidates) {
                if (item->hoverHandle() != ImageItem::Handle::None) {
                    item->setHoverHandle(ImageItem::Handle::None);
                }
            }
            const int gh = groupHandleAt(event->pos(), candidates);
            if (gh != m_groupHoverHandle) {
                m_groupHoverHandle = gh;
                viewport()->update();
            }
            if (gh >= 0) {
                // 0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L; 8–11 rotate
                switch (gh) {
                case 0: case 4: // TL, BR — NW–SE diagonal
                    viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case 2: case 6: // TR, BL — NE–SW diagonal
                    viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case 1: case 5:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case 3: case 7:
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case 8: case 9: case 10: case 11:
                    viewport()->setCursor(Qt::CrossCursor);
                    break;
                default:
                    viewport()->setCursor(Qt::ArrowCursor);
                    break;
                }
            } else if (!m_panning) {
                viewport()->unsetCursor();
            }
        } else {
            if (m_groupHoverHandle != -1) {
                m_groupHoverHandle = -1;
                viewport()->update();
            }
            ImageItem *hoverOwner = nullptr;
            ImageItem::Handle hoverH = ImageItem::Handle::None;
            std::sort(candidates.begin(), candidates.end(),
                      [](ImageItem *a, ImageItem *b) { return a->stackZ() > b->stackZ(); });
            for (ImageItem *item : candidates) {
                const ImageItem::Handle h = item->handleAt(item->mapFromScene(scenePos));
                if (h != ImageItem::Handle::None) {
                    hoverOwner = item;
                    hoverH = h;
                    break;
                }
            }
            bool hoverChanged = false;
            for (ImageItem *item : candidates) {
                const ImageItem::Handle next =
                    (item == hoverOwner) ? hoverH : ImageItem::Handle::None;
                if (item->hoverHandle() != next) {
                    hoverChanged = true;
                }
                item->setHoverHandle(next);
            }
            if (hoverChanged) {
                viewport()->update();
            }
            if (hoverOwner && hoverH != ImageItem::Handle::None) {
                using H = ImageItem::Handle;
                switch (hoverH) {
                case H::RotateTop: case H::RotateRight:
                case H::RotateBottom: case H::RotateLeft:
                    viewport()->setCursor(Qt::CrossCursor);
                    break;
                case H::ScaleTopLeft: case H::ScaleBottomRight:
                    // NW–SE diagonal
                    viewport()->setCursor(Qt::SizeFDiagCursor);
                    break;
                case H::ScaleTopRight: case H::ScaleBottomLeft:
                    // NE–SW diagonal
                    viewport()->setCursor(Qt::SizeBDiagCursor);
                    break;
                case H::ScaleTop: case H::ScaleBottom:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                case H::ScaleLeft: case H::ScaleRight:
                    viewport()->setCursor(Qt::SizeHorCursor);
                    break;
                case H::OpacitySlider:
                    viewport()->setCursor(Qt::SizeVerCursor);
                    break;
                default:
                    viewport()->setCursor(Qt::PointingHandCursor);
                    break;
                }
            } else if (!m_panning && !m_handleDragItem) {
                viewport()->unsetCursor();
            }
        }
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_cropMode && m_cropActiveHandle != CropHandle::None
        && event->button() == Qt::LeftButton) {
        endCropHandleDrag();
        event->accept();
        return;
    }
    if (m_cropMode && m_cropRubberBanding && event->button() == Qt::LeftButton) {
        endCropRubberBand();
        event->accept();
        return;
    }

    if (m_zoomRegionDragging) {
        const QRect viewRect = QRect(m_zoomRegionOrigin, event->pos()).normalized();
        m_zoomRegionDragging = false;
        if (m_zoomRubberBand) {
            m_zoomRubberBand->hide();
        }
        // Ignore tiny clicks — treat as cancel rather than extreme zoom.
        if (viewRect.width() >= 8 && viewRect.height() >= 8) {
            const QRectF sceneRect = mapToScene(viewRect).boundingRect();
            if (sceneRect.isValid() && !sceneRect.isEmpty()) {
                m_fitMode = false;
                m_fillMode = false;
                fitInView(sceneRect, Qt::KeepAspectRatio);
                emit statusChanged();
            }
        }
        cancelZoomRegion();
        event->accept();
        return;
    }
    if ((m_groupScaleDrag || m_groupRotateDrag) && event->button() == Qt::LeftButton) {
        if (m_undoStack && !m_groupDragItems.isEmpty()) {
            m_undoStack->beginMacro(m_groupRotateDrag ? tr("Rotate selection")
                                                      : tr("Scale selection"));
            for (int i = 0; i < m_groupDragItems.size(); ++i) {
                ImageItem *item = m_groupDragItems.at(i);
                if (!item || i >= m_groupDragStartStates.size()) {
                    continue;
                }
                const WorkspaceItemState after = captureState(item);
                const WorkspaceItemState &before = m_groupDragStartStates.at(i);
                if (after.pos == before.pos
                    && qFuzzyCompare(after.scale, before.scale)
                    && qFuzzyCompare(after.scaleY > 0 ? after.scaleY : 1.0,
                                     before.scaleY > 0 ? before.scaleY : 1.0)
                    && qFuzzyCompare(after.rotation, before.rotation)) {
                    continue;
                }
                // Inline undo entry matching other transform paths.
                class TransformCommand : public QUndoCommand {
                public:
                    TransformCommand(ImageView *view, ImageItem *item,
                                     const WorkspaceItemState &before,
                                     const WorkspaceItemState &after)
                        : m_view(view), m_item(item), m_before(before), m_after(after)
                    {
                        setText(QObject::tr("Transform"));
                    }
                    void undo() override { if (m_view && m_item) m_view->applyState(m_item, m_before); }
                    void redo() override { if (m_view && m_item) m_view->applyState(m_item, m_after); }
                private:
                    ImageView *m_view;
                    ImageItem *m_item;
                    WorkspaceItemState m_before, m_after;
                };
                m_undoStack->push(new TransformCommand(this, item, before, after));
            }
            m_undoStack->endMacro();
        }
        endGroupScale();
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        event->accept();
        return;
    }

    if (m_handleDragItem && event->button() == Qt::LeftButton) {
        m_handleDragItem->endHandleInteraction();
        const WorkspaceItemState after = captureState(m_handleDragItem);
        if (after.pos != m_dragStartState.pos
            || after.scale != m_dragStartState.scale
            || after.scaleY != m_dragStartState.scaleY
            || after.rotation != m_dragStartState.rotation
            || after.opacity != m_dragStartState.opacity) {
            class TransformCommand : public QUndoCommand {
            public:
                TransformCommand(ImageView *view, ImageItem *item,
                                 const WorkspaceItemState &before,
                                 const WorkspaceItemState &after)
                    : m_view(view), m_item(item), m_before(before), m_after(after)
                {
                    setText(QObject::tr("Transform"));
                }
                void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
            private:
                ImageView *m_view;
                ImageItem *m_item;
                WorkspaceItemState m_before, m_after;
            };
            if (m_undoStack) {
                m_undoStack->push(new TransformCommand(this, m_handleDragItem,
                                                       m_dragStartState, after));
            }
            emit statusChanged();
        }
        m_handleDragItem = nullptr;
        m_dragItem = nullptr;
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        event->accept();
        return;
    }

    if (m_rotating && event->button() == Qt::LeftButton) {
        if (m_rotateItem) {
            const WorkspaceItemState after = captureState(m_rotateItem);
            if (after.rotation != m_dragStartState.rotation
                || after.pos != m_dragStartState.pos) {
                // Lightweight: clear is avoided; push a simple undo via reset path
                // Store as single-step by re-applying start on undo through stack of states
                class TransformCommand : public QUndoCommand {
                public:
                    TransformCommand(ImageView *view, ImageItem *item,
                                     const WorkspaceItemState &before,
                                     const WorkspaceItemState &after)
                        : m_view(view), m_item(item), m_before(before), m_after(after)
                    {
                        setText(QObject::tr("Transform"));
                    }
                    void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                    void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
                private:
                    ImageView *m_view;
                    ImageItem *m_item;
                    WorkspaceItemState m_before, m_after;
                };
                m_undoStack->push(new TransformCommand(this, m_rotateItem,
                                                       m_dragStartState, after));
            }
        }
        m_rotating = false;
        m_rotateItem = nullptr;
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_dragItem && event->button() == Qt::LeftButton) {
        const WorkspaceItemState after = captureState(m_dragItem);
        if (after.pos != m_dragStartState.pos
            || after.scale != m_dragStartState.scale
            || after.scaleY != m_dragStartState.scaleY
            || after.rotation != m_dragStartState.rotation) {
            class TransformCommand : public QUndoCommand {
            public:
                TransformCommand(ImageView *view, ImageItem *item,
                                 const WorkspaceItemState &before,
                                 const WorkspaceItemState &after)
                    : m_view(view), m_item(item), m_before(before), m_after(after)
                {
                    setText(QObject::tr("Move"));
                }
                void undo() override { if (m_item) m_view->applyState(m_item, m_before); }
                void redo() override { if (m_item) m_view->applyState(m_item, m_after); }
            private:
                ImageView *m_view;
                ImageItem *m_item;
                WorkspaceItemState m_before, m_after;
            };
            m_undoStack->push(new TransformCommand(this, m_dragItem,
                                                   m_dragStartState, after));
            emit statusChanged();
        }
        m_dragItem = nullptr;
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_cropMode) {
        if (event->key() == Qt::Key_Escape) {
            cancelCrop();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            applyCrop();
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape && (m_zoomRegionArmed || m_zoomRegionDragging)) {
        cancelZoomRegion();
        event->accept();
        return;
    }

    // Image mode: Left/Right (and friends) navigate the session. QGraphicsView
    // would otherwise scroll the viewport when the image is zoomed or the view
    // has focus (typical in fullscreen), swallowing the QAction shortcuts.
    if (isImageMode()
        && !(event->modifiers()
             & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_PageUp:
        case Qt::Key_Backspace:
            emit navigatePreviousRequested();
            event->accept();
            return;
        case Qt::Key_Right:
        case Qt::Key_PageDown:
            emit navigateNextRequested();
            event->accept();
            return;
        default:
            break;
        }
    }

    // Gallery: arrow keys move among tiles by scene position; Enter opens.
    if (isGalleryMode()
        && !(event->modifiers()
             & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        && !m_items.isEmpty()) {
        auto selectedGalleryItem = [this]() -> ImageItem * {
            for (QGraphicsItem *gi : m_scene->selectedItems()) {
                if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                    return ii;
                }
            }
            return m_items.isEmpty() ? nullptr : m_items.first();
        };

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (ImageItem *item = selectedGalleryItem()) {
                emit galleryItemOpenRequested(item->path());
                event->accept();
                return;
            }
        }

        if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
            ImageItem *item = (event->key() == Qt::Key_Home)
                                  ? m_items.first()
                                  : m_items.last();
            focusSessionPath(item->path());
            emit galleryItemFocused(item->path());
            event->accept();
            return;
        }

        // Spatial neighbour: prefer candidates in the arrow direction, score by
        // primary-axis distance with a cross-axis penalty (grid-friendly).
        const int key = event->key();
        if (key == Qt::Key_Left || key == Qt::Key_Right
            || key == Qt::Key_Up || key == Qt::Key_Down) {
            ImageItem *from = selectedGalleryItem();
            if (!from) {
                from = m_items.first();
            }
            const QPointF origin = from->sceneBoundingRect().center();
            ImageItem *best = nullptr;
            qreal bestScore = 1e300;
            constexpr qreal kEps = 1.0;
            constexpr qreal kCrossWeight = 2.5;
            for (ImageItem *cand : m_items) {
                if (cand == from) {
                    continue;
                }
                const QPointF d = cand->sceneBoundingRect().center() - origin;
                qreal primary = 0;
                qreal cross = 0;
                bool inDir = false;
                switch (key) {
                case Qt::Key_Left:
                    inDir = d.x() < -kEps;
                    primary = -d.x();
                    cross = qAbs(d.y());
                    break;
                case Qt::Key_Right:
                    inDir = d.x() > kEps;
                    primary = d.x();
                    cross = qAbs(d.y());
                    break;
                case Qt::Key_Up:
                    inDir = d.y() < -kEps;
                    primary = -d.y();
                    cross = qAbs(d.x());
                    break;
                case Qt::Key_Down:
                    inDir = d.y() > kEps;
                    primary = d.y();
                    cross = qAbs(d.x());
                    break;
                default:
                    break;
                }
                if (!inDir) {
                    continue;
                }
                const qreal score = primary + kCrossWeight * cross;
                if (score < bestScore) {
                    bestScore = score;
                    best = cand;
                }
            }
            if (best) {
                focusSessionPath(best->path());
                emit galleryItemFocused(best->path());
                event->accept();
                return;
            }
        }
    }

    if (event->key() == Qt::Key_Delete
        || (event->key() == Qt::Key_Backspace && isMultiItemMode())) {
        const QList<QGraphicsItem *> selected = m_scene->selectedItems();
        QStringList paths;
        for (QGraphicsItem *gi : selected) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (m_items.contains(item)) {
                    paths.append(item->path());
                }
            }
        }
        if (paths.isEmpty()) {
            // fall through
        } else if (isGalleryMode()) {
            // Gallery tiles are the session — remove from session so a layout
            // switch does not resurrect them via setWorkspacePaths(m_files).
            emit sessionRemovePathsRequested(paths);
            event->accept();
            return;
        } else if (isWorkspaceMode()) {
            // Workspace: hide from canvas only; session membership stays.
            // Destroy by item pointer (same path may exist twice after Duplicate).
            QList<ImageItem *> toRemove;
            for (QGraphicsItem *gi : selected) {
                if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                    if (m_items.contains(item)) {
                        toRemove.append(item);
                    }
                }
            }
            setUpdatesEnabled(false);
            if (m_scene) {
                m_scene->blockSignals(true);
            }
            for (ImageItem *item : toRemove) {
                destroyCanvasItem(item);
            }
            if (m_scene) {
                m_scene->blockSignals(false);
            }
            setUpdatesEnabled(true);
            viewport()->update();
            emit statusChanged();
            emit workspacePathsChanged();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}
