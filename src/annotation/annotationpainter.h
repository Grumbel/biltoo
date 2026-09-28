// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONPAINTER_H
#define ANNOTATIONPAINTER_H

#include "annotation/annotationtypes.h"

#include <QPainter>
#include <QVector>

class ImageItem;
class ImageView;

/**
 * Read-only presentation of annotation data in scene space.
 * No tool/session mutation — pure mapping + draw over Annotation::Object/Page.
 */
class AnnotationPainter
{
public:
    static QRectF pageRectToDisplay(ImageView *view, ImageItem *item,
                                    const QRectF &pageRect, const QRectF &pageBounds,
                                    bool pageYUp, const QSize &sourceSize);

    static QPointF pageToScene(ImageView *view, ImageItem *item, const QPointF &pagePt,
                               const QRectF &pageBounds, bool pageYUp,
                               const QSize &sourceSize);

    static QRectF pageRectToScene(ImageView *view, ImageItem *item, const QRectF &pageRect,
                                  const QRectF &pageBounds, bool pageYUp,
                                  const QSize &sourceSize);

    static void applyHighlightBlend(QPainter &painter, const QColor &color);

    static void paintStroke(QPainter &painter, ImageView *view, ImageItem *item,
                            const Annotation::Object &obj, const QRectF &pageBounds,
                            bool pageYUp, const QSize &sourceSize);

    static void paintQuads(QPainter &painter, ImageView *view, ImageItem *item,
                           const Annotation::Object &obj, const QRectF &pageBounds,
                           bool pageYUp, const QSize &sourceSize);

    static void paintShape(QPainter &painter, ImageView *view, ImageItem *item,
                           const Annotation::Object &obj, const QRectF &pageBounds,
                           bool pageYUp, const QSize &sourceSize);

    static void paintSticky(QPainter &painter, ImageView *view, ImageItem *item,
                            const Annotation::Object &obj, const QRectF &pageBounds,
                            bool pageYUp, const QSize &sourceSize);

    static void paintObject(QPainter &painter, ImageView *view, ImageItem *item,
                            const Annotation::Object &obj, const QRectF &pageBounds,
                            bool pageYUp, const QSize &sourceSize);

    static void paintPageObjects(QPainter &painter, ImageView *view, ImageItem *item,
                                 const Annotation::Page &page, const QRectF &fallbackBounds,
                                 bool fallbackYUp, const QSize &sourceSize);

    static void paintSelectionChrome(QPainter &painter, ImageView *view, ImageItem *item,
                                     const Annotation::Page &page, const QRectF &pageBounds,
                                     bool pageYUp, const QSize &sourceSize,
                                     const QVector<quint64> &selectedIds);
};

#endif // ANNOTATIONPAINTER_H
