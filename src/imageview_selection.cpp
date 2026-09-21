// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas selection, transform targets, and workspace clipboard.

#include "imageview.h"
#include "itemcomponents.h"
#include "imageitem.h"
#include "sessionappearance.h"

#include <QSet>
#include <QUndoStack>
#include <QGraphicsItem>
#include "imagecache.h"
#include "contentxform.h"

void ImageView::selectBySessionIndices(const QList<int> &indices)
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    for (int idx : indices) {
        if (ImageItem *item = findItemBySessionIndex(idx)) {
            item->setSelected(true);
        }
    }
}


QList<SessionImageId> ImageView::selectedSessionIds() const
{
    QList<SessionImageId> out;
    for (ImageItem *item : m_items) {
        if (item && item->isSelected() && item->sessionId() != kInvalidSessionImageId) {
            out.append(item->sessionId());
        }
    }
    return out;
}


void ImageView::selectBySessionIds(const QList<SessionImageId> &ids)
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    for (SessionImageId id : ids) {
        if (id == kInvalidSessionImageId) {
            continue;
        }
        if (ImageItem *item = findItemBySessionId(id)) {
            item->setSelected(true);
        }
    }
}


void ImageView::selectPathsByOccurrence(const QStringList &paths)
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    QHash<QString, int> nextOccurrence;
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        const int want = nextOccurrence.value(path, 0);
        int seen = 0;
        for (ImageItem *item : m_items) {
            if (!item || item->path() != path) {
                continue;
            }
            if (seen == want) {
                item->setSelected(true);
                nextOccurrence[path] = want + 1;
                break;
            }
            ++seen;
        }
    }
}


bool ImageView::hasTransformTargets() const
{
    return !transformTargets().isEmpty();
}


bool ImageView::hasSingleCropTarget() const
{
    return transformTargets().size() == 1;
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
    QList<int> out;
    for (ImageItem *item : m_items) {
        if (!item || !item->isSelected()) {
            continue;
        }
        const int idx = sessionListIndex(item);
        if (idx >= 0) {
            out.append(idx);
        }
    }
    return out;
}


void ImageView::selectAllCanvasItems()
{
    if (!m_scene || isImageMode() || m_items.isEmpty()) {
        return;
    }
    m_scene->blockSignals(true);
    for (ImageItem *item : m_items) {
        if (item) {
            if (isGalleryMode()
                && !(item->flags() & QGraphicsItem::ItemIsSelectable)) {
                item->setGallerySelectable(true);
            }
            item->setSelected(true);
        }
    }
    m_scene->blockSignals(false);
    if (isGalleryMode() && viewport()) {
        viewport()->update();
    }
    if (!m_items.isEmpty()) {
        m_gallery.setSelectionAnchor(m_items.first());
    }
    emit canvasSelectionChanged();
    emit statusChanged();
}




QList<ImageItem *> ImageView::transformTargets() const
{
    QList<ImageItem *> out;
    if (!m_scene) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            out.append(item);
        }
    }
    if (!out.isEmpty()) {
        return out;
    }
    if (isImageMode() || m_items.size() == 1) {
        if (!m_items.isEmpty()) {
            out.append(m_items.first());
        }
    }
    return out;
}

// --- Clipboard / duplicate (was imageview_clipboard.cpp) ---

void ImageView::duplicateSelected()
{
    if (!isWorkspaceMode() && !isGalleryMode()) {
        return;
    }
    QList<ImageItem *> sources;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        return;
    }

    m_scene->clearSelection();
    for (ImageItem *src : sources) {
        // Display-ready copy of current pixels — never createItemFromImage with
        // sourceImage/preview: that ImageCache::put's baked samples as host.
        QImage display = src->sourceImage();
        SessionAppearance::PixelKind kind = SessionAppearance::PixelKind::FullSource;
        if (display.isNull()) {
            display = src->previewImage();
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        if (display.isNull()) {
            display = src->displayImage();
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        if (display.isNull()) {
            continue;
        }

        // Freeze policy: store + live when durable and not mid-edit.
        WorkspaceItemState content = freezeItemAppearance(src);
        content.path = src->path();

        QSize intrinsic = src->imageSize();
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            const QSize native = layoutSizeForPath(src->path(), QImage());
            intrinsic = ContentXform::layoutSize(native, content);
        }
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            // Do not adopt sample pixel size (LQIP/soft).
            intrinsic = QSize(1, 1);
        }

        auto *copy = new ImageItem(src->path(), intrinsic);
        applyItemModeFlags(copy);
        m_scene->addItem(copy);
        m_items.append(copy);
        // Attach already-baked display; do not put into ImageCache.
        attachDisplaySample(copy, display, content, kind);
        m_pendingAppearance.insert(copy, content);
        if (isWorkspaceMode()) {
            ItemComponents::Placement pl = src->placement();
            pl.pos += QPointF(40.0, 40.0); // visible beside the original
            pl.z += 0.01;
            copy->applyPlacement(pl);
        } else {
            // Gallery: upright tile; MainWindow packs after binding session ids.
            ItemComponents::Placement pl;
            pl.pos = src->pos();
            copy->applyPlacement(pl);
        }
        copy->setSelected(true);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}


QList<WorkspaceItemState> ImageView::captureSelectedWorkspaceClipboard() const
{
    QList<WorkspaceItemState> out;
    if (!isWorkspaceMode()) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        // Stage 4a / 2: freeze policy (store + live when durable, not mid-edit).
        const SessionImageId sid = item->sessionId();
        WorkspaceItemState s = freezeItemAppearance(item);
        s.path = item->path();
        s.sessionId = sid;
        out.append(s);
    }
    return out;
}




void ImageView::placeWorkspaceClipboardItems(const QList<WorkspaceItemState> &items,
                                             const QVector<SessionImageId> &newIds,
                                             const QList<int> &sessionIndices)
{
    if (!isWorkspaceMode() || items.isEmpty() || newIds.size() != items.size()) {
        return;
    }
    if (m_scene) {
        m_scene->clearSelection();
    }
    m_bindBook.clearSelectIds();
    for (int i = 0; i < items.size(); ++i) {
        const WorkspaceItemState &st = items.at(i);
        const SessionImageId sid = newIds.at(i);
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        if (st.path.isEmpty() || sid == kInvalidSessionImageId) {
            continue;
        }
        m_bindBook.addSelectId(sid);
        // Appearance (content + pose) must already be in the store under sid.
        addImageForSession(st.path, sid, idx);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}
