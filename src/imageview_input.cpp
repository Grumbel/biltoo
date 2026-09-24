// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallery/gallerydecodesm.h"
#include "image/toolpolicy.h"
#include "workspace/workspacenavgeometry.h"
#include "workspace/grouptransformgeometry.h"
#include "item/selectiongeometry.h"
#include "view/viewtransform.h"
#include "item/placementlinear.h"
#include "attention/attentiongeometry.h"
#include "image/edgenavpolicy.h"
#include "host/pagepath.h"
#include "gallery/gallerylayout.h"
#include "imageitem.h"
#include "host/imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QSet>
#include <QThreadPool>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QWheelEvent>
#include <QRubberBand>
#include <QtMath>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <QGraphicsItem>
#include "host/thumtoocache.h"

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    return edgeZoneFromPolicy(m_image.edgeZoneAt(viewPos));
}
void ImageView::updateMouseInfo(const QPoint &viewPos)
{
    m_shell.updateMouseInfo(viewPos);
}
void ImageView::wheelEvent(QWheelEvent *event)
{
    m_shell.handleWheel(event);
}
void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    m_shell.handleResize();
}
bool ImageView::setHoverEdge(EdgeZone zone)
{
    return m_image.setHoverEdge(edgeZoneToPolicy(zone));
}
