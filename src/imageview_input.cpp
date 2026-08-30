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

int ImageView::edgeZoneWidth() const
{
    return qMax(48, static_cast<int>(width() * 0.12));
}

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    if (isMultiItemMode() || !m_imageModeNavEnabled) {
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
    if (m_hoverEdge == EdgeZone::Previous || m_hoverEdge == EdgeZone::Next) {
        setCursor(Qt::PointingHandCursor);
    } else if (!m_panning && !m_rotating) {
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    viewport()->update();
}

void ImageView::drawEdgeAffordances(QPainter &painter)
{
    if (m_hoverEdge == EdgeZone::None || isMultiItemMode() || !m_imageModeNavEnabled) {
        return;
    }

    const QRect vr = viewport()->rect();
    const int zone = edgeZoneWidth();
    const int cy = vr.center().y();

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Soft hit-zone wash
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

    // Circular button with chevron — sit near the window edge
    const int r = 22;
    constexpr int kEdgeMargin = 10; // gap from window edge to button rim
    const int cx = (m_hoverEdge == EdgeZone::Previous)
                       ? (kEdgeMargin + r)
                       : (vr.width() - kEdgeMargin - r);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawEllipse(QPoint(cx, cy), r, r);
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawEllipse(QPoint(cx, cy), r - 3, r - 3);

    QPainterPath chevron;
    if (m_hoverEdge == EdgeZone::Previous) {
        chevron.moveTo(cx + 5, cy - 10);
        chevron.lineTo(cx - 6, cy);
        chevron.lineTo(cx + 5, cy + 10);
    } else {
        chevron.moveTo(cx - 5, cy - 10);
        chevron.lineTo(cx + 6, cy);
        chevron.lineTo(cx - 5, cy + 10);
    }
    QPen pen(QColor(40, 40, 40), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.strokePath(chevron, pen);
}

void ImageView::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);
    QPainter painter(viewport());
    if (m_hoverEdge != EdgeZone::None && isImageMode() && m_imageModeNavEnabled) {
        drawEdgeAffordances(painter);
    }
    // HUD layout:
    //   top-left  — transient actions (slideshow, fit, …), never Next/Prev
    //   top-right — session index [i/n]
    //   bottom    — filename (+ technical detail when the HUD is pinned)
    if (m_hudVisible || m_hudFlashVisible || m_sessionTotal > 0) {
        QFont f = font();
        f.setPointSizeF(qMax(10.0, f.pointSizeF()));
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
            painter.setBrush(QColor(0, 0, 0, 170));
            painter.drawRoundedRect(bg, 6, 6);
            painter.setPen(QColor(240, 240, 240));
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
            drawPanel({{badge, false}}, 0, margin, true, false);
        }

        // Bottom: filename — pinned, or briefly after navigation / action flash
        if (m_hudVisible || m_hudIdentityPulse || m_hudFlashVisible) {
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

    // Checkerboard in scene coordinates so it pans/zooms with the view
    constexpr int kCell = 16;
    const QColor a = m_bgColor;
    const QColor b = m_bgColorAlt.isValid() ? m_bgColorAlt : m_bgColor.lighter(120);

    const int x0 = static_cast<int>(std::floor(rect.left() / kCell)) * kCell;
    const int y0 = static_cast<int>(std::floor(rect.top() / kCell)) * kCell;
    const int x1 = static_cast<int>(std::ceil(rect.right() / kCell)) * kCell;
    const int y1 = static_cast<int>(std::ceil(rect.bottom() / kCell)) * kCell;

    for (int y = y0; y < y1; y += kCell) {
        for (int x = x0; x < x1; x += kCell) {
            const bool dark = ((x / kCell) + (y / kCell)) & 1;
            painter->fillRect(QRect(x, y, kCell, kCell), dark ? a : b);
        }
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
    // Gallery: scroll the view (do not zoom — packing owns item scale).
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
            // Layout still settling or no overflow: use QAbstractScrollArea
            // (QGraphicsView does not scroll on wheel by default).
            QAbstractScrollArea::wheelEvent(event);
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
    // Image mode: left/right edge clicks navigate the session
    if (isImageMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        const EdgeZone zone = edgeZoneAt(event->pos());
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

    // Workspace Select tool: let QGraphicsView handle selection / move
    if (isMultiItemMode() && event->button() == Qt::LeftButton) {
        if (isGalleryLayout()
            && !(event->modifiers()
                 & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
            m_galleryClickCandidate = false;
            m_galleryPressItem = nullptr;
            const QPointF scenePos = mapToScene(event->pos());
            for (QGraphicsItem *gi : m_scene->items(scenePos)) {
                if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                    m_galleryPressItem = item;
                    m_galleryPressPos = event->pos();
                    m_galleryClickCandidate = true;
                    break;
                }
            }
        }
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
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (m_galleryClickCandidate
        && (event->pos() - m_galleryPressPos).manhattanLength()
               >= QApplication::startDragDistance()) {
        m_galleryClickCandidate = false;
        m_galleryPressItem = nullptr;
    }

    if (isImageMode()) {
        updateHoverEdge(event->pos());
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
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
    }

    // Gallery: click (press+release without drag) opens Image mode for that item.
    if (m_galleryClickCandidate && event->button() == Qt::LeftButton
        && isGalleryLayout() && m_galleryPressItem) {
        const QString path = m_galleryPressItem->path();
        m_galleryClickCandidate = false;
        m_galleryPressItem = nullptr;
        if (!path.isEmpty()) {
            emit galleryItemOpenRequested(path);
            event->accept();
            return;
        }
    }
    m_galleryClickCandidate = false;
    m_galleryPressItem = nullptr;

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

    if (event->key() == Qt::Key_Delete
        || (event->key() == Qt::Key_Backspace && isWorkspaceMode())) {
        // Remove selected items from the workspace (session list is unchanged);
        // remember transform so re-selecting the thumbnail restores position.
        const QList<QGraphicsItem *> selected = m_scene->selectedItems();
        bool removed = false;
        for (QGraphicsItem *gi : selected) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                rememberItemState(item);
                m_items.removeOne(item);
                m_scene->removeItem(item);
                delete item;
                removed = true;
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
