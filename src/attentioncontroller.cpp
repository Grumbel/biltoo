// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "attentioncontroller.h"
#include "imageview.h"

#include "attentiongeometry.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QCursor>
#include <QPainter>
#include <QMouseEvent>
#include <QSet>
#include <QUndoCommand>
#include <algorithm>

SessionImageId AttentionController::attentionSessionId() const
{
    if (ImageItem *item = m_view->targetItem()) {
        if (item->sessionId() != kInvalidSessionImageId) {
            return item->sessionId();
        }
    }
    if (m_view->hostSessionId().hasCurrentId()) {
        return m_view->hostSessionId().currentIdValue();
    }
    return kInvalidSessionImageId;
}

QPointF AttentionController::attentionViewPos(ImageItem *item, const QPointF &norm) const
{
    if (!item || item->contentRect().isEmpty()) {
        return {};
    }
    const QPointF local = AttentionGeometry::localFromNorm(norm, item->contentRect());
    return m_view->mapFromScene(item->mapToScene(local));
}

QVector<QPointF> AttentionController::attentionPointsForTarget() const
{
    const SessionImageId sid = attentionSessionId();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_view->hostAppearance().get(sid)) {
            if (!st->attentionPoints.isEmpty()) {
                return st->attentionPoints;
            }
            if (st->hasAttention) {
                return {st->attentionNorm};
            }
        }
    }
    if (session().hasDraftFor(sid)) {
        return session().draftPtsRef();
    }
    return {};
}

QPointF AttentionController::attentionNormForTarget() const
{
    const QVector<QPointF> pts = attentionPointsForTarget();
    if (!pts.isEmpty()) {
        return pts.first();
    }
    return QPointF(0.5, 0.5);
}

void AttentionController::setAttentionPointsForTarget(const QVector<QPointF> &pts)
{
    const QVector<QPointF> clamped = AttentionGeometry::clampNormPoints(pts);
    SessionImageId sid = attentionSessionId();
    ImageItem *item = m_view->targetItem();
    if (item && sid != kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
        item->setSessionId(sid);
    }
    session().setDraft(clamped, sid);

    QVector<int> kept;
    for (int i : session().selectedRef()) {
        if (i >= 0 && i < clamped.size()) {
            kept.append(i);
        }
    }
    session().setSelected(kept);

    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState st = m_view->hostAppearance().value(sid);
        st.attentionPoints = clamped;
        st.syncAttentionPrimary();
        m_view->hostAppearance().set(sid, st);
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit m_view->statusChanged();
}

void AttentionController::setAttentionNormForTarget(const QPointF &norm)
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

void AttentionController::ensureAttentionPoint()
{
    const SessionImageId sid = attentionSessionId();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_view->hostAppearance().get(sid)) {
            if (!st->attentionPoints.isEmpty() || st->hasAttention) {
                session().setDraft(
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

void AttentionController::restoreAttentionPoints(const QVector<QPointF> &pts)
{
    setAttentionPointsForTarget(pts);
    session().selectAllIndices(pts.size());
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AttentionController::pushAttentionPointsUndo(const QVector<QPointF> &before,
                                        const QVector<QPointF> &after,
                                        const QString &text)
{
    if (!m_view->undoStack() || before == after) {
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
    m_view->undoStack()->push(new AttentionPointsCommand(this, before, after, text));
}

void AttentionController::detectAttentionPoint()
{
    ImageItem *item = m_view->targetItem();
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
    setAttentionPointsForTarget(pts);
    session().selectAllIndices(pts.size());
    pushAttentionPointsUndo(before, pts, tr("Detect attention points"));
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AttentionController::setAttentionMode(bool on)
{
    if (on == session().active()) {
        return;
    }
    if (on) {
        if (!m_view->isImageMode()) {
            emit m_view->attentionModeChanged(false);
            return;
        }
        if (m_view->isCropMode()) {
            m_view->cancelCrop();
        }
        session().enterMode();
        if (m_view->hostHoverEdge() != ImageView::EdgeZone::None) {
            m_view->setHostHoverEdge(ImageView::EdgeZone::None);
        }
        ensureAttentionPoint();
        if (m_view->viewport()) {
            m_view->viewport()->setCursor(Qt::CrossCursor);
        }
    } else {
        session().leaveMode();
        if (m_view->viewport()) {
            m_view->viewport()->unsetCursor();
        }
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit m_view->attentionModeChanged(session().active());
    emit m_view->statusChanged();
}

void AttentionController::toggleAttentionMode()
{
    setAttentionMode(!session().active());
}

int AttentionController::attentionHandleIndexAt(const QPoint &viewPos) const
{
    ImageItem *item = m_view->targetItem();
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

bool AttentionController::attentionHandleAt(const QPoint &viewPos) const
{
    return attentionHandleIndexAt(viewPos) >= 0;
}

void AttentionController::attentionDeleteSelected()
{
    if (!session().hasSelection()) {
        return;
    }
    QVector<QPointF> pts = attentionPointsForTarget();
    const QVector<QPointF> before = pts;
    QSet<int> kill(session().selectedRef().begin(), session().selectedRef().end());
    QVector<QPointF> kept;
    for (int i = 0; i < pts.size(); ++i) {
        if (!kill.contains(i)) {
            kept.append(pts.at(i));
        }
    }
    session().clearSelected();
    setAttentionPointsForTarget(kept);
    pushAttentionPointsUndo(before, kept, tr("Delete attention points"));
}

void AttentionController::attentionCommitSelectionMove()
{
    if (session().isGestureActive()) {
        const QVector<QPointF> after = attentionPointsForTarget();
        pushAttentionPointsUndo(session().gestureBeforeRef(), after,
                                tr("Edit attention points"));
    }
    session().clearGesture();
}

void AttentionController::paintAttentionOverlay(QPainter &painter)
{
    if (!session().active() || !m_view->isImageMode()) {
        return;
    }
    ImageItem *item = m_view->targetItem();
    if (!item || item->contentRect().isEmpty()) {
        painter.save();
        painter.setPen(QColor(255, 255, 255, 230));
        painter.drawText(m_view->viewport()->rect().adjusted(12, 12, -12, -12),
                         Qt::AlignTop | Qt::AlignLeft,
                         tr("Attention mode: no image loaded"));
        painter.restore();
        return;
    }

    const QVector<QPointF> pts = attentionPointsForTarget();
    QSet<int> selected(session().selectedRef().begin(), session().selectedRef().end());

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(m_view->viewport()->rect(), QColor(0, 0, 0, 30));

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

    if (session().isRubberbanding() && !session().rubberRectRef().isEmpty()) {
        painter.setPen(QPen(QColor(80, 180, 255, 220), 1.2, Qt::DashLine));
        painter.setBrush(QColor(80, 180, 255, 40));
        painter.drawRect(session().rubberRectRef().normalized());
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
            .arg(session().selectedRef().size());
    painter.drawText(m_view->viewport()->rect().adjusted(12, 12, -12, -12),
                     Qt::AlignTop | Qt::AlignLeft, hint);
    painter.restore();
}

// --- Tier 6 input handlers ---

bool AttentionController::tryMousePressAttention(QMouseEvent *event)
{
    if (!session().active() || event->button() != Qt::LeftButton || !m_view->isImageMode()
        || m_view->edgeZoneAt(event->pos()) != ImageView::EdgeZone::None) {
        return false;
    }
    ImageItem *item = m_view->targetItem();
    if (!item || item->contentRect().isEmpty()) {
        return false;
    }
    const int hit = attentionHandleIndexAt(event->pos());
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    // Handle hit: standard selection (Shift/Ctrl toggle; plain exclusive unless
    // already selected so multi-drag keeps the set).
    if (hit >= 0) {
        if (shift || ctrl) {
            session().setSelected(
                AttentionGeometry::toggleSelectionIndex(session().selectedMutable(), hit));
        } else if (!session().selectedRef().contains(hit)) {
            session().setSelected({hit});
        }
        const QVector<QPointF> startPts = attentionPointsForTarget();
        session().beginPointDrag(event->pos(), startPts, startPts);
        m_view->viewport()->update();
        event->accept();
        return true;
    }
    // Ctrl+click empty content: insert a point (then drag to place).
    const QPointF scene = m_view->mapToScene(event->pos());
    const QPointF local = item->mapFromScene(scene);
    const QRectF cr = item->contentRect();
    if (ctrl && cr.contains(local)) {
        const QPointF n = AttentionGeometry::normFromLocal(local, cr);
        const QVector<QPointF> before = attentionPointsForTarget();
        QVector<QPointF> pts = before;
        pts.append(n);
        setAttentionPointsForTarget(pts);
        session().setSelected({int(pts.size() - 1)});
        const QVector<QPointF> startPts = attentionPointsForTarget();
        session().beginPointDrag(event->pos(), startPts, before);
        event->accept();
        return true;
    }
    // Plain / Shift click on empty: rubber-band select (additive with Shift).
    session().beginRubber(event->pos(), !shift && !ctrl);
    m_view->viewport()->update();
    event->accept();
    return true;
}

bool AttentionController::tryMouseMoveAttention(QMouseEvent *event)
{
    if (!session().active() || !m_view->isImageMode()) {
        return false;
    }
    if (session().isRubberbanding()) {
        session().updateRubber(event->pos());
        m_view->viewport()->update();
        event->accept();
        return true;
    }
    if (session().isDragging()) {
        ImageItem *item = m_view->targetItem();
        if (item && !item->contentRect().isEmpty()
            && session().hasSelection()
            && session().dragStartPtsRef().size() == attentionPointsForTarget().size()) {
            // Translate selected points by view-delta mapped through content.
            const QPointF scene0 = m_view->mapToScene(session().dragOriginViewRef());
            const QPointF scene1 = m_view->mapToScene(event->pos());
            const QPointF local0 = item->mapFromScene(scene0);
            const QPointF local1 = item->mapFromScene(scene1);
            const QRectF cr = item->contentRect();
            const QPointF dNorm = AttentionGeometry::normDeltaFromLocalDelta(
                local1 - local0, cr);
            const QVector<QPointF> pts = AttentionGeometry::translateSelectedNorms(
                session().dragStartPtsRef(), session().selectedMutable(), dNorm);
            setAttentionPointsForTarget(pts);
        }
        event->accept();
        return true;
    }
    m_view->viewport()->setCursor(attentionHandleAt(event->pos()) ? Qt::SizeAllCursor
                                                          : Qt::CrossCursor);
    return false;
}
