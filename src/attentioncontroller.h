// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ATTENTIONCONTROLLER_H
#define ATTENTIONCONTROLLER_H

#include "attentionsession.h"
#include "imageview_types.h"

#include <QPointF>
#include <QVector>
#include <QString>

class ImageView;
class ImageItem;
class QPainter;
class QMouseEvent;
class QKeyEvent;

/**
 * Attention-mode collaborator for ImageView (Phase 6 Tier 2a).
 *
 * Owns AttentionSession draft points. Orchestration remains on ImageView
 * until Tier 2b method extraction.
 */
class AttentionController
{
public:
    explicit AttentionController(ImageView *view);

    ImageView *view() const { return m_view; }

    AttentionSession &session() { return m_attention; }
    const AttentionSession &session() const { return m_attention; }

    bool active() const { return m_attention.active(); }

    SessionImageId attentionSessionId() const;
    QPointF attentionViewPos(ImageItem *item, const QPointF &norm) const;
    QVector<QPointF> attentionPointsForTarget() const;
    void setAttentionPointsForTarget(const QVector<QPointF> &pts);
    void ensureAttentionPoint();
    void restoreAttentionPoints(const QVector<QPointF> &pts);
    void pushAttentionPointsUndo(const QVector<QPointF> &before,
                                 const QVector<QPointF> &after, const QString &text);
    void detectAttentionPoint();
    void setAttentionMode(bool on);
    void toggleAttentionMode();
    int attentionHandleIndexAt(const QPoint &viewPos) const;
    bool attentionHandleAt(const QPoint &viewPos) const;
    void attentionDeleteSelected();
    void attentionCommitSelectionMove();
    void paintAttentionOverlay(QPainter &painter);

    bool tryMousePressAttention(QMouseEvent *event);
    bool tryMouseMoveAttention(QMouseEvent *event);
    bool tryMouseReleaseAttention(QMouseEvent *event);
    bool tryKeyPressAttention(QKeyEvent *event);

private:
    ImageView *m_view = nullptr; // not owned
    AttentionSession m_attention;
};

#endif // ATTENTIONCONTROLLER_H
