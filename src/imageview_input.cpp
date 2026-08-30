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
    // Top strip: back to Gallery (when Image was opened from a gallery tile).
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
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int cx, int cy) {
            chevron.moveTo(cx - 10, cy + 5);
            chevron.lineTo(cx, cy - 6);
            chevron.lineTo(cx + 10, cy + 5);
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
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int cx, int cy) {
            chevron.moveTo(cx + 5, cy - 10);
            chevron.lineTo(cx - 6, cy);
            chevron.lineTo(cx + 5, cy + 10);
        });
    } else {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int cx, int cy) {
            chevron.moveTo(cx - 5, cy - 10);
            chevron.lineTo(cx + 6, cy);
            chevron.lineTo(cx - 5, cy + 10);
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
    if (isWorkspaceMode() && m_scene) {
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                // Only paint chrome for items we still own (guards against a
                // stale selection entry after destroyCanvasItem).
                if (ii->isInteractive() && m_items.contains(ii)) {
                    selected.append(ii);
                }
            }
        }
        std::sort(selected.begin(), selected.end(),
                  [](ImageItem *a, ImageItem *b) { return a->stackZ() < b->stackZ(); });
        for (ImageItem *item : selected) {
            item->paintInteractionChrome(&painter);
        }
    }
    if (m_hoverEdge != EdgeZone::None && isImageMode()
        && (m_imageModeNavEnabled || m_hoverEdge == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }
    // HUD layout:
    //   top-left  — transient actions (slideshow, fit, …), never Next/Prev
    //   top-right — session index [i/n]
    //   bottom    — filename (+ technical detail when the HUD is pinned)
    if (m_hudVisible || m_hudFlashVisible || m_hudIdentityPulse
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

        // Top-left: transient actions only (slideshow start/stop, …)
        if (m_hudFlashVisible && !m_hudAction.isEmpty()) {
            QString actionLine = m_hudAction;
            if (!m_hudDetail.isEmpty()) {
                actionLine += QLatin1Char(' ') + m_hudDetail;
            }
            drawPanel({{actionLine, true}}, margin, margin, false, false);
        }

        // Top-right: session index (pinned HUD, flash, or identity pulse after nav)
        const QString badge = sessionBadgeText();
        if (!badge.isEmpty()
            && (m_hudVisible || m_hudFlashVisible || m_hudIdentityPulse)) {
            drawPanel({{badge, true}}, 0, margin, true, false);
        }

        // Bottom: filename — pinned, identity pulse, or gallery hover
        if (m_hudVisible || m_hudIdentityPulse || m_hudFlashVisible
            || !m_galleryHoverPath.isEmpty()) {
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
        return;
    }

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

void ImageView::drawForeground(QPainter *painter, const QRectF &rect)
{
    // Chrome is painted in paintEvent (viewport device space) so handle size
    // stays scale-invariant. Keep this override empty.
    Q_UNUSED(painter);
    Q_UNUSED(rect);
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
    // Gallery: Ctrl+wheel (or Meta) zooms the view about the cursor — packing
    // still owns item scale; default view transform on enter is 1:1. Plain
    // wheel scrolls the packed scene when it overflows; Shift prefers horizontal.
    if (isGalleryMode()) {
        const bool wantZoom = event->modifiers()
                              & (Qt::ControlModifier | Qt::MetaModifier);
        if (wantZoom) {
            const qreal factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);
            m_fitMode = false;
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            emit statusChanged();
            event->accept();
            return;
        }
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

        // Most mice only report a vertical wheel. If the gallery only scrolls on
        // the other axis (e.g. Horizontal strip), map the delta to that axis.
        if (dx == 0 && dy != 0 && !canV && canH) {
            dx = dy;
            dy = 0;
        } else if (dy == 0 && dx != 0 && !canH && canV) {
            dy = dx;
            dx = 0;
        }

        bool moved = false;
        if (canH && dx != 0) {
            hBar->setValue(hBar->value() - dx);
            moved = true;
        }
        if (canV && dy != 0) {
            vBar->setValue(vBar->value() - dy);
            moved = true;
        }
        if (!moved) {
            // No overflow (common for Side-by-Side / Vertical fit): zoom the
            // view so Horz/Vert can still inspect pixels. Default enter is 1:1.
            const qreal factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);
            m_fitMode = false;
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            scale(factor, factor);
            emit statusChanged();
            event->accept();
            return;
        }
        event->accept();
        return;
    }

    const qreal factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);

    // Image mode and free-form Workspace: zoom the view about the cursor
    m_fitMode = false;
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            item->updateHandleLayout();
        }
    }
    emit statusChanged();
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_applyingLayout) {
        return;
    }
    if (isGalleryMode() && !m_items.isEmpty()) {
        // Defer so scrollbar/geometry changes from setSceneRect settle first.
        scheduleApplyLayout();
        return;
    }
    if (m_fitMode && m_items.size() == 1) {
        fitItem(m_items.first(), currentFitAspectMode());
    }
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    // Workspace chrome hit-testing is view-owned (DOMAIN: free-object transforms).
    // ImageItem::paint draws chrome in device pixels; shape() alone can miss those
    // controls under rotation / anisotropic scale. This path is authoritative;
    // item mouse handlers remain a fallback when the scene delivers the event.
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && m_tool == Tool::Select) {
        const QPointF scenePos = mapToScene(event->pos());
        QList<ImageItem *> candidates;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive()) {
                    candidates.append(ii);
                }
            }
        }
        // Also consider the topmost interactive item under the cursor (for
        // clicking a handle after selecting via the body).
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && !candidates.contains(ii)) {
                    candidates.append(ii);
                }
                break; // topmost under cursor only as extra candidate
            }
        }
        // Highest stackZ first
        std::sort(candidates.begin(), candidates.end(),
                  [](ImageItem *a, ImageItem *b) {
                      return a->stackZ() > b->stackZ();
                  });
        for (ImageItem *item : candidates) {
            // Ensure selection so chrome is active
            if (!item->isSelected()) {
                continue; // only selected items show chrome
            }
            if (item->beginHandleInteraction(scenePos, event->modifiers())) {
                m_handleDragItem = item;
                m_dragItem = item;
                m_dragStartState = captureState(item);
                event->accept();
                return;
            }
        }
        // Empty-space clicks still fall through (clear selection). Clicks on a
        // selected item body use the default move/select path below.
    }

    // Image mode: edge clicks — top returns to Gallery; left/right navigate
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
    updateMouseInfo(event->pos());

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
    if (isWorkspaceMode() && m_tool == Tool::Select && !m_handleDragItem && !m_panning) {
        const QPointF scenePos = mapToScene(event->pos());
        ImageItem *hoverOwner = nullptr;
        ImageItem::Handle hoverH = ImageItem::Handle::None;
        QList<ImageItem *> candidates;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive()) {
                    candidates.append(ii);
                }
            }
        }
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
            // Chrome is painted in paintEvent — refresh viewport when highlight moves.
            viewport()->update();
        }
        // Cursor for chrome even when QGraphicsItem hover is not delivered
        // (handle outside shape / under another pixmap).
        if (hoverOwner && hoverH != ImageItem::Handle::None) {
            using H = ImageItem::Handle;
            switch (hoverH) {
            case H::RotateTop: case H::RotateRight:
            case H::RotateBottom: case H::RotateLeft:
                viewport()->setCursor(Qt::CrossCursor);
                break;
            case H::ScaleTopLeft: case H::ScaleBottomRight:
            case H::ScaleTopRight: case H::ScaleBottomLeft:
                viewport()->setCursor(Qt::SizeFDiagCursor);
                break;
            case H::ScaleTop: case H::ScaleBottom:
                viewport()->setCursor(Qt::SizeVerCursor);
                break;
            case H::ScaleLeft: case H::ScaleRight:
                viewport()->setCursor(Qt::SizeHorCursor);
                break;
            case H::OpacitySlider:
                viewport()->setCursor(Qt::SizeHorCursor);
                break;
            default:
                viewport()->setCursor(Qt::PointingHandCursor);
                break;
            }
        } else if (!m_panning && !m_handleDragItem) {
            viewport()->unsetCursor();
        }
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
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
        || (event->key() == Qt::Key_Backspace && isWorkspaceMode())) {
        // Remove selected items from the workspace (session list is unchanged);
        // remember transform so re-adding restores position.
        const QList<QGraphicsItem *> selected = m_scene->selectedItems();
        bool removed = false;
        for (QGraphicsItem *gi : selected) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (m_items.contains(item)) {
                    destroyCanvasItem(item);
                    removed = true;
                }
            }
        }
        if (removed) {
            emit statusChanged();
            emit workspacePathsChanged();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}
