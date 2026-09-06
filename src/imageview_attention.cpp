// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include "imageitem.h"
#include "imageloader.h"

#include <QCursor>
#include <QLineF>
#include <QPainter>
#include <QSet>
#include <algorithm>

namespace {
constexpr qreal kHandleScreenPx = 12.0;
constexpr qreal kPrimaryScreenPx = 15.0;
}

SessionImageId ImageView::attentionSessionId() const
{
    if (ImageItem *item = targetItem()) {
        if (item->sessionId() != kInvalidSessionImageId) {
            return item->sessionId();
        }
    }
    if (m_currentSessionId != kInvalidSessionImageId) {
        return m_currentSessionId;
    }
    return kInvalidSessionImageId;
}

QPointF ImageView::attentionViewPos(ImageItem *item, const QPointF &norm) const
{
    if (!item) {
        return {};
    }
    const QRectF cr = item->contentRect();
    if (cr.isEmpty()) {
        return {};
    }
    const QPointF local(cr.left() + norm.x() * cr.width(),
                        cr.top() + norm.y() * cr.height());
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
    if (m_attentionMode && m_attentionDraftValid
        && sid != kInvalidSessionImageId
        && sid == m_attentionDraftSessionId
        && !m_attentionDraftPts.isEmpty()) {
        return m_attentionDraftPts;
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
    QVector<QPointF> clamped;
    clamped.reserve(pts.size());
    for (const QPointF &p : pts) {
        clamped.append(QPointF(qBound(0.0, p.x(), 1.0), qBound(0.0, p.y(), 1.0)));
    }
    SessionImageId sid = attentionSessionId();
    ImageItem *item = targetItem();
    if (item && sid != kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
        item->setSessionId(sid);
    }
    m_attentionDraftPts = clamped;
    m_attentionDraftValid = true;
    m_attentionDraftSessionId = sid;

    QVector<int> kept;
    for (int i : m_attentionSelected) {
        if (i >= 0 && i < clamped.size()) {
            kept.append(i);
        }
    }
    m_attentionSelected = kept;

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
    const QPointF clamped(qBound(0.0, norm.x(), 1.0), qBound(0.0, norm.y(), 1.0));
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
                m_attentionDraftPts = !st->attentionPoints.isEmpty()
                    ? st->attentionPoints
                    : QVector<QPointF>{st->attentionNorm};
                m_attentionDraftValid = true;
                m_attentionDraftSessionId = sid;
                return;
            }
        }
    }
    detectAttentionPoint();
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
    m_attentionSelected.clear();
    setAttentionPointsForTarget(pts);
    for (int i = 0; i < pts.size(); ++i) {
        m_attentionSelected.append(i);
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::setAttentionMode(bool on)
{
    if (on == m_attentionMode) {
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
        m_attentionMode = true;
        m_attentionDragging = false;
        m_attentionRubberbanding = false;
        m_attentionSelected.clear();
        if (m_hoverEdge != EdgeZone::None) {
            m_hoverEdge = EdgeZone::None;
        }
        ensureAttentionPoint();
        if (viewport()) {
            viewport()->setCursor(Qt::CrossCursor);
        }
    } else {
        m_attentionMode = false;
        m_attentionDragging = false;
        m_attentionRubberbanding = false;
        m_attentionSelected.clear();
        m_attentionRubberRect = QRect();
        if (viewport()) {
            viewport()->unsetCursor();
        }
    }
    if (viewport()) {
        viewport()->update();
    }
    emit attentionModeChanged(m_attentionMode);
    emit statusChanged();
}

void ImageView::toggleAttentionMode()
{
    setAttentionMode(!m_attentionMode);
}

int ImageView::attentionHandleIndexAt(const QPoint &viewPos) const
{
    ImageItem *item = targetItem();
    if (!item || item->contentRect().isEmpty()) {
        return -1;
    }
    const QVector<QPointF> pts = attentionPointsForTarget();
    int best = -1;
    qreal bestDist = kHandleScreenPx + 1.0;
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF v = attentionViewPos(item, pts.at(i));
        const qreal d = QLineF(v, QPointF(viewPos)).length();
        const qreal lim = (i == 0) ? kPrimaryScreenPx : kHandleScreenPx;
        if (d <= lim && d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

bool ImageView::attentionHandleAt(const QPoint &viewPos) const
{
    return attentionHandleIndexAt(viewPos) >= 0;
}

void ImageView::attentionDeleteSelected()
{
    if (m_attentionSelected.isEmpty()) {
        return;
    }
    QVector<QPointF> pts = attentionPointsForTarget();
    QSet<int> kill(m_attentionSelected.begin(), m_attentionSelected.end());
    QVector<QPointF> kept;
    for (int i = 0; i < pts.size(); ++i) {
        if (!kill.contains(i)) {
            kept.append(pts.at(i));
        }
    }
    m_attentionSelected.clear();
    setAttentionPointsForTarget(kept);
}

void ImageView::attentionCommitSelectionMove()
{
    m_attentionDragStartPts.clear();
}

void ImageView::paintAttentionOverlay(QPainter &painter)
{
    if (!m_attentionMode || !isImageMode()) {
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
    QSet<int> selected(m_attentionSelected.begin(), m_attentionSelected.end());

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(viewport()->rect(), QColor(0, 0, 0, 30));

    for (int i = 0; i < pts.size(); ++i) {
        const QPointF view = attentionViewPos(item, pts.at(i));
        const bool isPrimary = (i == 0);
        const bool isSel = selected.contains(i);
        const qreal r = isPrimary ? kPrimaryScreenPx : kHandleScreenPx;
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

    if (m_attentionRubberbanding && !m_attentionRubberRect.isEmpty()) {
        painter.setPen(QPen(QColor(80, 180, 255, 220), 1.2, Qt::DashLine));
        painter.setBrush(QColor(80, 180, 255, 40));
        painter.drawRect(m_attentionRubberRect.normalized());
    }

    QFont f = painter.font();
    f.setPointSize(qMax(10, f.pointSize() + 1));
    painter.setFont(f);
    painter.setPen(QColor(255, 255, 255, 240));
    const QString hint =
        tr("Attention — click: add · drag handle: move · Shift+click: multi-select\n"
           "Drag empty: rubber-band · Del: delete · Detect: auto peaks · Esc: exit\n"
           "%1 point(s), %2 selected  (primary = Ken Burns)")
            .arg(pts.size())
            .arg(m_attentionSelected.size());
    painter.drawText(viewport()->rect().adjusted(12, 12, -12, -12),
                     Qt::AlignTop | Qt::AlignLeft, hint);
    painter.restore();
}
