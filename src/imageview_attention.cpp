// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include "attentiongeometry.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QCursor>
#include <QPainter>
#include <QSet>
#include <QUndoCommand>
#include <algorithm>

SessionImageId ImageView::attentionSessionId() const
{
    if (ImageItem *item = targetItem()) {
        if (item->sessionId() != kInvalidSessionImageId) {
            return item->sessionId();
        }
    }
    if (m_sessionId.hasCurrentId()) {
        return m_sessionId.currentId;
    }
    return kInvalidSessionImageId;
}

QPointF ImageView::attentionViewPos(ImageItem *item, const QPointF &norm) const
{
    if (!item || item->contentRect().isEmpty()) {
        return {};
    }
    const QPointF local = AttentionGeometry::localFromNorm(norm, item->contentRect());
    return mapFromScene(item->mapToScene(local));
}

QVector<QPointF> ImageView::attentionPointsForTarget() const
{
    const SessionImageId sid = attentionSessionId();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_appearance.get(sid)) {
            if (!st->attentionPoints.isEmpty()) {
                return st->attentionPoints;
            }
            if (st->hasAttention) {
                return {st->attentionNorm};
            }
        }
    }
    if (m_attention.hasDraftFor(sid)) {
        return m_attention.draftPts;
    }
    return {};
}

QPointF ImageView::attentionNormForTarget() const
{
    const QVector<QPointF> pts = attentionPointsForTarget();
    if (!pts.isEmpty()) {
        return pts.first();
    }
    return QPointF(0.5, 0.5);
}

void ImageView::setAttentionPointsForTarget(const QVector<QPointF> &pts)
{
    const QVector<QPointF> clamped = AttentionGeometry::clampNormPoints(pts);
    SessionImageId sid = attentionSessionId();
    ImageItem *item = targetItem();
    if (item && sid != kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
        item->setSessionId(sid);
    }
    m_attention.setDraft(clamped, sid);

    QVector<int> kept;
    for (int i : m_attention.selected) {
        if (i >= 0 && i < clamped.size()) {
            kept.append(i);
        }
    }
    m_attention.setSelected(kept);

    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState st = m_appearance.value(sid);
        st.attentionPoints = clamped;
        st.syncAttentionPrimary();
        m_appearance.set(sid, st);
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::setAttentionNormForTarget(const QPointF &norm)
{
    QVector<QPointF> pts = attentionPointsForTarget();
    const QPointF clamped = AttentionGeometry::clampNorm(norm);
    if (pts.isEmpty()) {
        pts.append(clamped);
    } else {
        pts[0] = clamped;
    }
    setAttentionPointsForTarget(pts);
}

void ImageView::ensureAttentionPoint()
{
    const SessionImageId sid = attentionSessionId();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_appearance.get(sid)) {
            if (!st->attentionPoints.isEmpty() || st->hasAttention) {
                m_attention.setDraft(
                    !st->attentionPoints.isEmpty()
                        ? st->attentionPoints
                        : QVector<QPointF>{st->attentionNorm},
                    sid);
                return;
            }
        }
    }
    detectAttentionPoint();
}

void ImageView::restoreAttentionPoints(const QVector<QPointF> &pts)
{
    setAttentionPointsForTarget(pts);
    m_attention.clearSelected();
    for (int i = 0; i < pts.size(); ++i) {
        m_attention.selected.append(i);
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::pushAttentionPointsUndo(const QVector<QPointF> &before,
                                        const QVector<QPointF> &after,
                                        const QString &text)
{
    if (!m_undoStack || before == after) {
        return;
    }
    class AttentionPointsCommand : public QUndoCommand {
    public:
        AttentionPointsCommand(ImageView *view,
                               const QVector<QPointF> &before,
                               const QVector<QPointF> &after,
                               const QString &text)
            : m_view(view)
            , m_before(before)
            , m_after(after)
        {
            setText(text);
        }
        void undo() override
        {
            if (m_view) {
                m_view->restoreAttentionPoints(m_before);
            }
        }
        void redo() override
        {
            if (m_view) {
                m_view->restoreAttentionPoints(m_after);
            }
        }
    private:
        ImageView *m_view = nullptr;
        QVector<QPointF> m_before;
        QVector<QPointF> m_after;
    };
    m_undoStack->push(new AttentionPointsCommand(this, before, after, text));
}

void ImageView::detectAttentionPoint()
{
    ImageItem *item = targetItem();
    QImage src;
    if (item) {
        src = item->sourceImage();
    }
    QVector<QPointF> pts;
    if (!src.isNull()) {
        ImageLoader::attentionPoints(src, &pts, 5);
    }
    if (pts.isEmpty()) {
        pts.append(QPointF(0.5, 0.5));
    }
    const QVector<QPointF> before = attentionPointsForTarget();
    m_attention.clearSelected();
    setAttentionPointsForTarget(pts);
    for (int i = 0; i < pts.size(); ++i) {
        m_attention.selected.append(i);
    }
    pushAttentionPointsUndo(before, pts, tr("Detect attention points"));
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::setAttentionMode(bool on)
{
    if (on == m_attention.active()) {
        return;
    }
    if (on) {
        if (!isImageMode()) {
            emit attentionModeChanged(false);
            return;
        }
        if (isCropMode()) {
            cancelCrop();
        }
        m_attention.enterMode();
        if (m_hoverEdge != EdgeZone::None) {
            clearHoverEdge();
        }
        ensureAttentionPoint();
        if (viewport()) {
            viewport()->setCursor(Qt::CrossCursor);
        }
    } else {
        m_attention.leaveMode();
        if (viewport()) {
            viewport()->unsetCursor();
        }
    }
    if (viewport()) {
        viewport()->update();
    }
    emit attentionModeChanged(m_attention.active());
    emit statusChanged();
}

void ImageView::toggleAttentionMode()
{
    setAttentionMode(!m_attention.active());
}

int ImageView::attentionHandleIndexAt(const QPoint &viewPos) const
{
    ImageItem *item = targetItem();
    if (!item || item->contentRect().isEmpty()) {
        return -1;
    }
    const QVector<QPointF> pts = attentionPointsForTarget();
    QVector<QPointF> viewPts;
    viewPts.reserve(pts.size());
    for (const QPointF &n : pts) {
        viewPts.append(attentionViewPos(item, n));
    }
    return AttentionGeometry::handleIndexAt(viewPos, viewPts);
}

bool ImageView::attentionHandleAt(const QPoint &viewPos) const
{
    return attentionHandleIndexAt(viewPos) >= 0;
}

void ImageView::attentionDeleteSelected()
{
    if (m_attention.selected.isEmpty()) {
        return;
    }
    QVector<QPointF> pts = attentionPointsForTarget();
    const QVector<QPointF> before = pts;
    QSet<int> kill(m_attention.selected.begin(), m_attention.selected.end());
    QVector<QPointF> kept;
    for (int i = 0; i < pts.size(); ++i) {
        if (!kill.contains(i)) {
            kept.append(pts.at(i));
        }
    }
    m_attention.clearSelected();
    setAttentionPointsForTarget(kept);
    pushAttentionPointsUndo(before, kept, tr("Delete attention points"));
}

void ImageView::attentionCommitSelectionMove()
{
    if (m_attention.isGestureActive()) {
        const QVector<QPointF> after = attentionPointsForTarget();
        pushAttentionPointsUndo(m_attention.gestureBefore, after,
                                tr("Edit attention points"));
    }
    m_attention.clearGesture();
    m_attention.gestureBefore.clear();
    m_attention.dragStartPts.clear();
}

void ImageView::paintAttentionOverlay(QPainter &painter)
{
    if (!m_attention.active() || !isImageMode()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->contentRect().isEmpty()) {
        painter.save();
        painter.setPen(QColor(255, 255, 255, 230));
        painter.drawText(viewport()->rect().adjusted(12, 12, -12, -12),
                         Qt::AlignTop | Qt::AlignLeft,
                         tr("Attention mode: no image loaded"));
        painter.restore();
        return;
    }

    const QVector<QPointF> pts = attentionPointsForTarget();
    QSet<int> selected(m_attention.selected.begin(), m_attention.selected.end());

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(viewport()->rect(), QColor(0, 0, 0, 30));

    for (int i = 0; i < pts.size(); ++i) {
        const QPointF view = attentionViewPos(item, pts.at(i));
        const bool isPrimary = (i == 0);
        const bool isSel = selected.contains(i);
        const qreal r = isPrimary ? AttentionGeometry::kPrimaryScreenPx
                                  : AttentionGeometry::kHandleScreenPx;
        const QColor ring = isSel ? QColor(80, 180, 255)
                                  : (isPrimary ? QColor(255, 220, 60)
                                               : QColor(255, 255, 255));
        painter.setPen(QPen(ring, isSel ? 3.0 : 2.0));
        painter.setBrush(QColor(ring.red(), ring.green(), ring.blue(), 55));
        painter.drawEllipse(view, r, r);
        painter.setBrush(QColor(ring.red(), ring.green(), ring.blue(), 230));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(view, isPrimary ? 4.5 : 3.5, isPrimary ? 4.5 : 3.5);
        if (isPrimary) {
            painter.setPen(QPen(QColor(20, 20, 20, 220), 1.6));
            painter.drawLine(QPointF(view.x() - r - 5, view.y()),
                             QPointF(view.x() + r + 5, view.y()));
            painter.drawLine(QPointF(view.x(), view.y() - r - 5),
                             QPointF(view.x(), view.y() + r + 5));
        }
        painter.setPen(QColor(255, 255, 255, 230));
        painter.drawText(QRectF(view.x() + r + 2, view.y() - 8, 28, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         isPrimary ? tr("P") : QString::number(i + 1));
    }

    if (m_attention.isRubberbanding() && !m_attention.rubberRect.isEmpty()) {
        painter.setPen(QPen(QColor(80, 180, 255, 220), 1.2, Qt::DashLine));
        painter.setBrush(QColor(80, 180, 255, 40));
        painter.drawRect(m_attention.rubberRect.normalized());
    }

    QFont f = painter.font();
    f.setPointSize(AttentionGeometry::clampHintPointSize(f.pointSize()));
    painter.setFont(f);
    painter.setPen(QColor(255, 255, 255, 240));
    const QString hint =
        tr("Attention — click: select · Shift/Ctrl+click: multi-select · drag empty: rubber-band\n"
           "Ctrl+click empty: add · drag handle: move · Del: delete · Ctrl+Z: undo · Esc: exit\n"
           "%1 point(s), %2 selected  (primary “P” = Ken Burns)")
            .arg(pts.size())
            .arg(m_attention.selected.size());
    painter.drawText(viewport()->rect().adjusted(12, 12, -12, -12),
                     Qt::AlignTop | Qt::AlignLeft, hint);
    painter.restore();
}
