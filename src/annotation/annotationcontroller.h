// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONCONTROLLER_H
#define ANNOTATIONCONTROLLER_H

#include "annotation/annotationsession.h"

#include <QColor>
#include <QPointF>
#include <QVector>

class ImageView;
class ImageItem;
class QPainter;
class QMouseEvent;

/**
 * Freehand highlighter (Multiply) and annotation paint/input.
 * Mouse-only for Phase A; geometry in page/source space.
 */
class AnnotationController
{
public:
    explicit AnnotationController(ImageView *view);

    AnnotationSession &session() { return m_session; }
    const AnnotationSession &session() const { return m_session; }

    bool isToolActive() const { return m_toolActive; }
    void setToolActive(bool on);

    QColor color() const { return m_color; }
    void setColor(const QColor &c) { m_color = c; }

    qreal width() const { return m_width; }
    void setWidth(qreal w) { m_width = qMax(1.0, w); }

    void paintOverlay(QPainter &painter);
    bool tryMousePress(QMouseEvent *event);
    bool tryMouseMove(QMouseEvent *event);
    bool tryMouseRelease(QMouseEvent *event);

    void clearCurrentPage();

private:
    ImageItem *targetItem() const;
    SessionImageId targetSid(ImageItem *item) const;
    bool pageSpaceForItem(ImageItem *item, QRectF *boundsOut, bool *yUpOut,
                          QSize *sourceSizeOut) const;
    QPointF viewToPage(ImageItem *item, const QPoint &viewPos,
                       const QRectF &pageBounds, bool pageYUp,
                       const QSize &sourceSize) const;
    QPointF pageToItemLocal(ImageItem *item, const QPointF &pagePt,
                            const QRectF &pageBounds, bool pageYUp,
                            const QSize &sourceSize) const;
    void paintStroke(QPainter &painter, ImageItem *item,
                     const Annotation::Object &obj, const QRectF &pageBounds,
                     bool pageYUp, const QSize &sourceSize) const;

    ImageView *m_view = nullptr;
    AnnotationSession m_session;
    bool m_toolActive = false;
    bool m_drawing = false;
    QColor m_color = QColor(246, 211, 45);
    qreal m_width = 18.0;
    QVector<QPointF> m_draftPoints; // page space while drawing
    SessionImageId m_draftSid = kInvalidSessionImageId;
    QRectF m_draftBounds;
    bool m_draftYUp = false;
    QSize m_draftSourceSize;
};

#endif // ANNOTATIONCONTROLLER_H
