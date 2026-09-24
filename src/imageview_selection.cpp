// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas selection and transform targets — WorkspaceController thin routers.
// validateUniqueLiveSessionIds body lives on WorkspaceController (live + stashes).

#include "imageview.h"

void ImageView::selectBySessionIndices(const QList<int> &indices)
{
    m_workspace.selectBySessionIndices(indices);
}

QList<SessionImageId> ImageView::selectedSessionIds() const
{
    return m_workspace.selectedSessionIds();
}

void ImageView::selectBySessionIds(const QList<SessionImageId> &ids)
{
    m_workspace.selectBySessionIds(ids);
}

void ImageView::selectPathsByOccurrence(const QStringList &paths)
{
    m_workspace.selectPathsByOccurrence(paths);
}

bool ImageView::hasTransformTargets() const
{
    return m_workspace.hasTransformTargets();
}

bool ImageView::hasSingleCropTarget() const
{
    return m_workspace.hasSingleCropTarget();
}

bool ImageView::validateUniqueLiveSessionIds(const char *context) const
{
    return m_workspace.validateUniqueLiveSessionIds(context);
}

QList<int> ImageView::selectedSessionIndices() const
{
    return m_workspace.selectedSessionIndices();
}

void ImageView::selectAllCanvasItems()
{
    m_workspace.selectAllCanvasItems();
}

QList<ImageItem *> ImageView::transformTargets() const
{
    return m_workspace.transformTargets();
}

// --- Clipboard / duplicate (was imageview_clipboard.cpp) ---

void ImageView::duplicateSelected(const QVector<SessionImageId> &newIds,
                                  int firstSessionIndex)
{
    m_workspace.duplicateSelected(newIds, firstSessionIndex);
}

QList<WorkspaceItemState> ImageView::captureSelectedWorkspaceClipboard() const
{
    return m_workspace.captureSelectedClipboard();
}

void ImageView::placeWorkspaceClipboardItems(const QList<WorkspaceItemState> &items,
                                             const QVector<SessionImageId> &newIds,
                                             const QList<int> &sessionIndices)
{
    m_workspace.placeClipboardItems(items, newIds, sessionIndices);
}
