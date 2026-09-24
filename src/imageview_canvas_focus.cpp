// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas focus, reveal, destroy, and Workspace scene placement
// (split from imageview_canvas.cpp).

#include <cstdio>
#include <cstdlib>
#include "imageview.h"
#include <QScrollBar>
#include <QtMath>
#include "view/viewtransform.h"
#include "session/sessionappearance.h"
#include "util/ttfp_trace.h"
#include "display/imagecache.h"
#include "imageitem.h"
#include "host/imageloader.h"

#include <QSet>
#include <QDebug>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>
#include <QGraphicsItem>

// --- Focus / destroy (was imageview_canvas_focus.cpp) ---

ImageItem *ImageView::primaryItem() const
{
    return m_workspace.primaryItem();
}

ImageItem *ImageView::targetItem() const
{
    return m_workspace.targetItem();
}

// --- from imageview_layout.cpp (canvas) ---










int ImageView::workspacePathOccurrenceCount(const QString &path) const
{
    return m_workspace.pathOccurrenceCount(path);
}

void ImageView::focusGalleryItem(ImageItem *item)
{
    m_gallery.focusItem(item);
}

void ImageView::focusSessionId(SessionImageId sessionId)
{
    m_gallery.focusSessionId(sessionId);
}

void ImageView::focusSessionPath(const QString &path)
{
    m_gallery.focusSessionPath(path);
}

void ImageView::revealGalleryPath(const QString &path)
{
    m_gallery.revealPath(path);
}

void ImageView::revealGallerySessionId(SessionImageId sessionId)
{
    m_gallery.revealSessionId(sessionId);
}

void ImageView::destroyCanvasItem(ImageItem *item, bool persistState)
{
    m_workspace.destroyCanvasItem(item, persistState);
}


// --- session remove / bind residual from layout ---












// --- canvas clipboard / session place-remove (from transform) ---

// --- Workspace scene / placement (was imageview_workspace.cpp) ---

bool ImageView::hasWorkspaceContent() const
{
    return m_workspace.hasContent();
}

void ImageView::updateWorkspaceSceneRect()
{
    m_workspace.updateSceneRect();
}

QPointF ImageView::findEmptyPlacement(const QSizeF &itemSize) const
{
    return m_workspace.findEmptyPlacement(itemSize);
}

WorkspaceItemState ImageView::defaultStateForPath(const QString &path, int ordinal) const
{
    return m_workspace.defaultStateForPath(path, ordinal);
}

