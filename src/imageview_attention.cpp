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

QPointF ImageView::attentionNormForTarget() const
{
    ImageItem *item = targetItem();
    if (!item) {
        return QPointF(0.5, 0.5);
    }
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_appearance.get(sid)) {
            if (st->hasAttention) {
                return st->attentionNorm;
            }
        }
    }
    return QPointF(0.5, 0.5);
}

void ImageView::setAttentionNormForTarget(const QPointF &norm)
{
    ImageItem *item = targetItem();
    if (!item) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    WorkspaceItemState st = m_appearance.value(sid);
    st.hasAttention = true;
    st.attentionNorm = QPointF(qBound(0.0, norm.x(), 1.0), qBound(0.0, norm.y(), 1.0));
    m_appearance.set(sid, st);
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::ensureAttentionPoint()
{
    ImageItem *item = targetItem();
    if (!item) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    if (const WorkspaceItemState *st = m_appearance.get(sid)) {
        if (st->hasAttention) {
            return;
        }
    }
    detectAttentionPoint();
}

void ImageView::detectAttentionPoint()
{
    ImageItem *item = targetItem();
    if (!item) {
        return;
    }
    QImage src = item->sourceImage();
    QPointF att01(0.5, 0.5);
    if (!src.isNull()) {
        ImageLoader::attentionPoint(src, &att01);
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
        ensureAttentionPoint();
        setCursor(Qt::CrossCursor);
    } else {
        m_attentionMode = false;
        m_attentionDragging = false;
        unsetCursor();
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
    const qreal d = QLineF(view, QPointF(viewPos)).length();
    return d <= kHandleScreenPx;
}

void ImageView::paintAttentionOverlay(QPainter &painter)
{
    if (!m_attentionMode || !isImageMode()) {
        return;
    }
    ImageItem *item = targetItem();
    if (!item || item->contentRect().isEmpty()) {
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

    // Dim the image slightly so the marker reads clearly.
    painter.fillRect(viewport()->rect(), QColor(0, 0, 0, 40));

    const qreal r = kHandleScreenPx;
    QPen ring(QColor(255, 220, 60), 2.0);
    painter.setPen(ring);
    painter.setBrush(QColor(255, 220, 60, 60));
    painter.drawEllipse(view, r, r);
    painter.setBrush(QColor(255, 220, 60, 200));
    painter.drawEllipse(view, 3.5, 3.5);

    QPen cross(QColor(20, 20, 20, 220), 1.5);
    painter.setPen(cross);
    painter.drawLine(QPointF(view.x() - r - 4, view.y()),
                     QPointF(view.x() + r + 4, view.y()));
    painter.drawLine(QPointF(view.x(), view.y() - r - 4),
                     QPointF(view.x(), view.y() + r + 4));

    // HUD hint
    QFont f = painter.font();
    f.setPointSize(qMax(9, f.pointSize()));
    painter.setFont(f);
    painter.setPen(QColor(255, 255, 255, 230));
    painter.drawText(viewport()->rect().adjusted(12, 12, -12, -12),
                     Qt::AlignTop | Qt::AlignLeft,
                     tr("Attention: drag the marker · click to place · Esc exits"));

    painter.restore();
}
