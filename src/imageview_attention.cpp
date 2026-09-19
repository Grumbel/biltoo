// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QVector>

SessionImageId ImageView::attentionSessionId() const
{
    return m_attentionCtrl.attentionSessionId();
}

QPointF ImageView::attentionViewPos(ImageItem *item, const QPointF &norm) const
{
    return m_attentionCtrl.attentionViewPos(item, norm);
}

QVector<QPointF> ImageView::attentionPointsForTarget() const
{
    return m_attentionCtrl.attentionPointsForTarget();
}

QPointF ImageView::attentionNormForTarget() const
{
    return m_attentionCtrl.attentionNormForTarget();
}

void ImageView::setAttentionPointsForTarget(const QVector<QPointF> &pts)
{
    m_attentionCtrl.setAttentionPointsForTarget(pts);
}

void ImageView::setAttentionNormForTarget(const QPointF &norm)
{
    m_attentionCtrl.setAttentionNormForTarget(norm);
}

void ImageView::ensureAttentionPoint()
{
    m_attentionCtrl.ensureAttentionPoint();
}

void ImageView::restoreAttentionPoints(const QVector<QPointF> &pts)
{
    m_attentionCtrl.restoreAttentionPoints(pts);
}

void ImageView::pushAttentionPointsUndo(const QVector<QPointF> &before,
                                        const QVector<QPointF> &after, const QString &text)
{
    m_attentionCtrl.pushAttentionPointsUndo(before, after, text);
}

void ImageView::detectAttentionPoint()
{
    m_attentionCtrl.detectAttentionPoint();
}

void ImageView::setAttentionMode(bool on)
{
    m_attentionCtrl.setAttentionMode(on);
}

void ImageView::toggleAttentionMode()
{
    m_attentionCtrl.toggleAttentionMode();
}

int ImageView::attentionHandleIndexAt(const QPoint &viewPos) const
{
    return m_attentionCtrl.attentionHandleIndexAt(viewPos);
}

bool ImageView::attentionHandleAt(const QPoint &viewPos) const
{
    return m_attentionCtrl.attentionHandleAt(viewPos);
}

void ImageView::attentionDeleteSelected()
{
    m_attentionCtrl.attentionDeleteSelected();
}

void ImageView::attentionCommitSelectionMove()
{
    m_attentionCtrl.attentionCommitSelectionMove();
}

void ImageView::paintAttentionOverlay(QPainter &painter)
{
    m_attentionCtrl.paintAttentionOverlay(painter);
}
