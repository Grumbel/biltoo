// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONCONTROLLER_H
#define ANNOTATIONCONTROLLER_H

#include "annotation/annotationsession.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>

class ImageView;
class ImageItem;
class QPainter;
class QMouseEvent;

/**
 * Annotation tools: freehand Multiply highlighter and text-snapped highlight.
 * Mouse-only for Phase A/B; geometry in page/source space.
 */
class AnnotationController
{
public:
    explicit AnnotationController(ImageView *view);

    AnnotationSession &session() { return m_session; }
    const AnnotationSession &session() const { return m_session; }

    Annotation::Tool tool() const { return m_tool; }
    void setTool(Annotation::Tool tool);

    bool isToolActive() const { return m_tool != Annotation::Tool::None; }
    /** Back-compat: freehand on/off. Prefer setTool. */
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
    void commitObject(SessionImageId sid, const Annotation::Object &obj,
                      const QRectF &pageBounds, bool pageYUp, const QString &undoText);

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
    QRectF pageRectToView(ImageItem *item, const QRectF &pageRect,
                          const QRectF &pageBounds, bool pageYUp,
                          const QSize &sourceSize) const;
    void paintStroke(QPainter &painter, ImageItem *item,
                     const Annotation::Object &obj, const QRectF &pageBounds,
                     bool pageYUp, const QSize &sourceSize) const;
    void paintQuads(QPainter &painter, ImageItem *item,
                    const Annotation::Object &obj, const QRectF &pageBounds,
                    bool pageYUp, const QSize &sourceSize) const;
    void paintObject(QPainter &painter, ImageItem *item, const Annotation::Object &obj,
                     const QRectF &pageBounds, bool pageYUp, const QSize &sourceSize) const;
    void finishFreehand();
    void finishTextHighlight();

    ImageView *m_view = nullptr;
    AnnotationSession m_session;
    Annotation::Tool m_tool = Annotation::Tool::None;
    bool m_drawing = false;
    QColor m_color = QColor(246, 211, 45);
    qreal m_width = 18.0;
    QVector<QPointF> m_draftPoints;
    QPoint m_rubberOriginView;
    QRect m_rubberView;
    SessionImageId m_draftSid = kInvalidSessionImageId;
    QRectF m_draftBounds;
    bool m_draftYUp = false;
    QSize m_draftSourceSize;
};

#endif // ANNOTATIONCONTROLLER_H
