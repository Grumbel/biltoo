// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas selection and transform targets — WorkspaceController thin routers.
// validateUniqueLiveSessionIds stays on ImageView (live + both stashes).

#include "imageview.h"
#include "item/itemcomponents.h"
#include "imageitem.h"
#include "session/sessionappearance.h"

#include <QSet>
#include <QUndoStack>
#include <QGraphicsItem>
#include "display/imagecache.h"
#include "content/contentxform.h"

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
    // Uniqueness is per list. The same SessionImageId on a *live* Image-mode
    // item and a *stashed* Gallery/Workspace tile is intentional (open-from-
    // Gallery keeps the packed tile in the stash while Image edits that id).
    bool ok = true;
    auto checkList = [&](const QList<ImageItem *> &list, const char *where) {
        // Store paths (not item pointers) so the diagnostic never dereferences
        // a hash miss under -Wnull-dereference.
        QHash<SessionImageId, QString> seenPath;
        for (const ImageItem *item : list) {
            if (!item) {
                continue;
            }
            const SessionImageId sid = item->sessionId();
            if (sid == kInvalidSessionImageId) {
                continue;
            }
            const auto it = seenPath.constFind(sid);
            if (it != seenPath.cend()) {
                const QString pathB = item->path();
                qCritical("ImageView: duplicate SessionImageId %lld within %s (%s) path=%s vs %s",
                          static_cast<long long>(sid),
                          where,
                          context ? context : "validate",
                          qPrintable(it.value()),
                          qPrintable(pathB));
                ok = false;
            } else {
                seenPath.insert(sid, item->path());
            }
        }
    };
    checkList(m_items, "live");
    checkList(m_workspace.stashedItems(), "workspace-stash");
    checkList(m_gallery.stashedItems(), "gallery-stash");
    return ok;
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

