// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include "imageitem.h"
#include "imageloader.h"

#include <QCursor>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr qreal kHandleScreenPx = 14.0;
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

QPointF ImageView::attentionNormForTarget() const
{
    const SessionImageId sid = attentionSessionId();
    // Prefer appearance for *this* session image only.
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_appearance.get(sid)) {
            if (st->hasAttention) {
                return st->attentionNorm;
            }
        }
    }
    // Draft is only valid for the session id it was edited under.
    if (m_attentionMode && m_attentionDraftValid
        && sid != kInvalidSessionImageId
        && sid == m_attentionDraftSessionId) {
        return m_attentionDraftNorm;
    }
    return QPointF(0.5, 0.5);
}

void ImageView::setAttentionNormForTarget(const QPointF &norm)
{
    const QPointF clamped(qBound(0.0, norm.x(), 1.0), qBound(0.0, norm.y(), 1.0));
    SessionImageId sid = attentionSessionId();
    ImageItem *item = targetItem();
    if (item && sid != kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
        item->setSessionId(sid);
    }
    m_attentionDraftNorm = clamped;
    m_attentionDraftValid = true;
    m_attentionDraftSessionId = sid;

    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState st = m_appearance.value(sid);
        st.hasAttention = true;
        st.attentionNorm = clamped;
        m_appearance.set(sid, st);
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::ensureAttentionPoint()
{
    const SessionImageId sid = attentionSessionId();
    // Always rebind draft to the *current* session image.
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_appearance.get(sid)) {
            if (st->hasAttention) {
                m_attentionDraftNorm = st->attentionNorm;
                m_attentionDraftValid = true;
                m_attentionDraftSessionId = sid;
                return;
            }
        }
    }
    // No stored point for this image — centre draft, do not steal another image's point.
    m_attentionDraftNorm = QPointF(0.5, 0.5);
    m_attentionDraftValid = true;
    m_attentionDraftSessionId = sid;
}

void ImageView::detectAttentionPoint()
{
    ImageItem *item = targetItem();
    QImage src;
    if (item) {
        src = item->sourceImage();
    }
    QPointF att01(0.5, 0.5);
    if (!src.isNull()) {
        if (!ImageLoader::attentionPoint(src, &att01)) {
            att01 = QPointF(0.5, 0.5);
        }
    }
    setAttentionNormForTarget(att01);
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
        // Drop edge hover so Up-to-Gallery chrome disappears immediately.
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

bool ImageView::attentionHandleAt(const QPoint &viewPos) const
{
    ImageItem *item = targetItem();
    if (!item || item->contentRect().isEmpty()) {
        return false;
    }
    const QPointF norm = attentionNormForTarget();
    const QRectF cr = item->contentRect();
    const QPointF local(cr.left() + norm.x() * cr.width(),
                        cr.top() + norm.y() * cr.height());
    const QPointF scene = item->mapToScene(local);
    const QPointF view = mapFromScene(scene);
    return QLineF(view, QPointF(viewPos)).length() <= kHandleScreenPx;
}

void ImageView::paintAttentionOverlay(QPainter &painter)
{
    if (!m_attentionMode || !isImageMode()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->contentRect().isEmpty()) {
        // Still show a HUD so the mode is obvious.
        painter.save();
        painter.setPen(QColor(255, 255, 255, 230));
        painter.drawText(viewport()->rect().adjusted(12, 12, -12, -12),
                         Qt::AlignTop | Qt::AlignLeft,
                         tr("Attention mode: no image loaded"));
        painter.restore();
        return;
    }
    const QPointF norm = attentionNormForTarget();
    const QRectF cr = item->contentRect();
    const QPointF local(cr.left() + norm.x() * cr.width(),
                        cr.top() + norm.y() * cr.height());
    const QPointF scene = item->mapToScene(local);
    const QPointF view = mapFromScene(scene);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(viewport()->rect(), QColor(0, 0, 0, 35));

    const qreal r = kHandleScreenPx;
    painter.setPen(QPen(QColor(255, 220, 60), 2.5));
    painter.setBrush(QColor(255, 220, 60, 70));
    painter.drawEllipse(view, r, r);
    painter.setBrush(QColor(255, 220, 60, 230));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(view, 4.0, 4.0);

    painter.setPen(QPen(QColor(20, 20, 20, 230), 1.8));
    painter.drawLine(QPointF(view.x() - r - 6, view.y()),
                     QPointF(view.x() + r + 6, view.y()));
    painter.drawLine(QPointF(view.x(), view.y() - r - 6),
                     QPointF(view.x(), view.y() + r + 6));

    QFont f = painter.font();
    f.setPointSize(qMax(10, f.pointSize() + 1));
    painter.setFont(f);
    painter.setPen(QColor(255, 255, 255, 240));
    const QString hint =
        tr("Attention point — drag or click to place · Detect on toolbar · Esc exits\n"
           "(Up to Gallery is off while editing)  (%1, %2)")
            .arg(norm.x(), 0, 'f', 3)
            .arg(norm.y(), 0, 'f', 3);
    painter.drawText(viewport()->rect().adjusted(12, 12, -12, -12),
                     Qt::AlignTop | Qt::AlignLeft, hint);

    painter.restore();
}
