// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Thin ImageView accessors and pack/decode status queries.

#include "imageview.h"
#include "imageitem.h"

#include <QGraphicsItem>

void ImageView::requestDebouncedGalleryPack(GalleryPackReason reason)
{
    m_gallery.requestDebouncedPack(reason);
}

int ImageView::pendingDecodeCount() const
{
    // Remaining work overview — not concurrent inflight. Counting only inflight
    // flickered 1↔0 as each soft job finished before the next was claimed.
    // Mode-specific path walks live on Gallery / Workspace / Slideshow controllers.
    int n = m_displayPipeline->loadGate().pendingWorkspaceAddCount()
          + m_displayPipeline->loadGate().pendingRestoreCount();

    if (isGalleryMode()) {
        // Gallery: blanks still need LQIP. LQIP-only is intentional underlay
        // (tiles own sharpness) — do not count as remaining soft work.
        n += m_gallery.uniqueBlankPathCount();
    } else if (isWorkspaceMode()) {
        // Workspace may still climb PreferCache for soft+ samples.
        n += m_workspace.uniqueWeakPathCount();
    }

    // Slideshow preload queue (inflight + pending neighbours + soft current).
    n += m_slideshow.pendingQualityWorkCount();
    return n;
}















































Qt::AspectRatioMode ImageView::currentFitAspectMode() const
{
    return m_image.framing().aspectMode();
}



QSize ImageView::imageSize() const
{
    if (ImageItem *item = targetItem()) {
        return item->imageSize();
    }
    if (ImageItem *item = primaryItem()) {
        return item->imageSize();
    }
    return {};
}

int ImageView::itemCount() const
{
    return m_items.size();
}

QStringList ImageView::itemPaths() const
{
    QStringList paths;
    for (ImageItem *item : m_items) {
        paths.append(item->path());
    }
    return paths;
}

QVector<SessionImageId> ImageView::itemSessionIds() const
{
    QVector<SessionImageId> ids;
    ids.reserve(m_items.size());
    for (ImageItem *item : m_items) {
        ids.append(item ? item->sessionId() : kInvalidSessionImageId);
    }
    return ids;
}

QStringList ImageView::selectedPaths() const
{
    QStringList paths;
    if (isImageMode()) {
        if (ImageItem *item = primaryItem()) {
            paths.append(item->path());
        } else if (m_image.hasClassicPath()) {
            paths.append(m_image.classicPath());
        }
        return paths;
    }
    // Gallery / Workspace: preserve session/canvas order, not click order.
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            paths.append(item->path());
        }
    }
    return paths;
}

// --- View queries (was imageview_view.cpp) ---

QColor ImageView::slideshowPadColor() const
{
    if (m_slideshow.settings().isSolidLetterbox()
        && m_slideshow.settings().padColorRef().isValid()) {
        return m_slideshow.settings().padColorRef();
    }
    if (m_shell.canvasBg().primaryColor().isValid()) {
        return m_shell.canvasBg().primaryColor();
    }
    const QBrush b = backgroundBrush();
    if (b.style() != Qt::NoBrush && b.color().isValid()) {
        return b.color();
    }
    return m_slideshow.settings().padColorRef().isValid() ? m_slideshow.settings().padColorRef() : QColor(42, 42, 42);
}

void ImageView::setContentEditMarksVisible(bool on)
{
    ImageItem::setContentEditMarksVisible(on);
    if (m_scene) {
        for (ImageItem *it : m_items) {
            if (it) {
                it->update();
            }
        }
        m_scene->update();
    }
    if (viewport()) {
        viewport()->update();
    }
}

bool ImageView::contentEditMarksVisible() const
{
    return ImageItem::contentEditMarksVisible();
}

QString ImageView::currentPath() const
{
    if (ImageItem *item = targetItem()) {
        return item->path();
    }
    if (ImageItem *item = primaryItem()) {
        return item->path();
    }
    return {};
}

