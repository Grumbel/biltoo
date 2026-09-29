// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONCONTROLLER_H
#define ANNOTATIONCONTROLLER_H

#include "annotation/annotationsession.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVector>
#include <QSet>
#include <QImage>
#include <Qt>

class ImageView;
class ImageItem;
class QPainter;
class QMouseEvent;
class QKeyEvent;

/**
 * Annotation tools: freehand Multiply highlighter and text-snapped highlight.
 *
 * Geometry is document page space (docs/OCR_COORDINATES.md). Paint maps
 * page → source → display → scene like TextLayerController region overlays.
 * Mouse-only for Phase A/B.
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
    void setToolActive(bool on);

    QColor color() const { return m_color; }
    void setColor(const QColor &c);

    qreal width() const { return m_width; }
    void setWidth(qreal w);

    /** Show/hide the whole annotation layer (paint + hit-test for select). */
    bool layerVisible() const { return m_session.isVisible(); }
    void setLayerVisible(bool on);

    /** Scene-space paint; call while the view transform is still active. */
    void paintOverlay(QPainter &painter);
    bool tryMousePress(QMouseEvent *event);
    bool tryMouseMove(QMouseEvent *event);
    bool tryMouseRelease(QMouseEvent *event);
    /** Select tool: double-click sticky → edit text (routed from ViewShellChrome). */
    bool tryMouseDoubleClick(QMouseEvent *event);

    void clearCurrentPage();
    /** Page pixels + annotations (display space of primary item). Null if none. */
    QImage renderFlattenedDisplay() const;
    void clearSelection();
    /** Select every object on the current page (Select tool / Ctrl+A). */
    void selectAllCurrentPage();
    QVector<quint64> selectedIds() const { return m_selectedIds; }
    bool tryKeyPress(QKeyEvent *event);
    void commitObject(SessionImageId sid, const Annotation::Object &obj,
                      const QRectF &pageBounds, bool pageYUp, const QString &undoText);
    /** Write page for @p sid to locator durable store (path-keyed like orient). */
    void persistPageForSid(SessionImageId sid);
    /** Load durable annotations for @p item path into its session id if needed. */
    void ensureHydrated(ImageItem *item);

private:
    ImageItem *targetItem() const;
    /** Top-most live ImageItem under view pos (Gallery/Workspace hit-test). */
    ImageItem *itemAtViewPos(const QPoint &viewPos) const;
    ImageItem *itemForDraftSid() const;
    QString pathForSid(SessionImageId sid) const;
    /** Committed page objects (+ optional select chrome) for one item. */
    void paintDraftChrome(QPainter &painter, ImageItem *item);
    void paintItemAnnotations(QPainter &painter, ImageItem *item,
                              bool selectionChrome);
    SessionImageId targetSid(ImageItem *item) const;
    bool pageSpaceForItem(ImageItem *item, QRectF *boundsOut, bool *yUpOut,
                          QSize *sourceSizeOut) const;
    QPointF viewToPage(ImageItem *item, const QPoint &viewPos,
                       const QRectF &pageBounds, bool pageYUp,
                       const QSize &sourceSize) const;
    void finishFreehand();
    void finishTextHighlight();
    void finishShape();
    void finishLine();
    void placeStickyAt(const QPointF &pagePt, const QRectF &pageBounds, bool pageYUp);
    void eraseAtPagePoint(const QPointF &pagePt, const QRectF &pageBounds,
                          bool pageYUp);
    quint64 hitTestTopObject(SessionImageId sid, const QPointF &pagePt,
                             qreal radius) const;
    void selectAtPagePoint(const QPointF &pagePt,
                          Qt::KeyboardModifiers mods = Qt::NoModifier);
    void deleteSelected();

    ImageView *m_view = nullptr;
    AnnotationSession m_session;
    Annotation::Tool m_tool = Annotation::Tool::None;
    QSet<SessionImageId> m_hydratedSids;
    bool m_drawing = false;
    QColor m_color = QColor(246, 211, 45);
    qreal m_width = 18.0;
    QVector<QPointF> m_draftPoints;
    QPoint m_rubberOriginView;
    QPoint m_shapeEndView;
    QRect m_rubberView;
    SessionImageId m_draftSid = kInvalidSessionImageId;
    QRectF m_draftBounds;
    bool m_draftYUp = false;
    QSize m_draftSourceSize;
    QVector<quint64> m_selectedIds;

    /** Select-tool drag move (page space). */
    bool m_moving = false;
    bool m_moveDidDrag = false;
    QPointF m_moveOriginPage;
    QVector<Annotation::Object> m_moveBaseline;

    static Annotation::Object translatedObject(const Annotation::Object &o,
                                               const QPointF &delta);
    void beginMoveSelection(const QPointF &pageOrigin);
    void applyMoveDelta(const QPointF &delta);
    void finishMoveSelection();
    void cancelMoveSelection();

    /** Corner resize for single selected object with one quad (0=TL..3=BL). */
    enum class ResizeCorner : int { None = -1, TL = 0, TR = 1, BR = 2, BL = 3 };
    static ResizeCorner hitTestQuadHandle(const QRectF &quad, const QPointF &pagePt,
                                          qreal radius);
    static QRectF resizedQuad(const QRectF &base, ResizeCorner corner,
                              const QPointF &pagePt);
    void beginResizeSelection(ResizeCorner corner, quint64 id,
                              const Annotation::Object &baseline);
    /** Endpoint drag for ShapeLine (and 2-point strokes); endpointIndex 0 or 1. */
    void beginResizeEndpoint(int endpointIndex, quint64 id,
                             const Annotation::Object &baseline);
    void applyResizeTo(const QPointF &pagePt);
    void finishResizeSelection();
    void cancelResizeSelection();
    void recordPageSourceKey(SessionImageId sid, const QRectF &pageBounds, bool pageYUp);

    bool m_resizing = false;
    ResizeCorner m_resizeCorner = ResizeCorner::None;
    int m_resizeEndpoint = -1; /**< ≥0 → moving points[index]; -1 → corner quad resize */
    quint64 m_resizeId = 0;
    Annotation::Object m_resizeBaseline;
};


#endif // ANNOTATIONCONTROLLER_H
