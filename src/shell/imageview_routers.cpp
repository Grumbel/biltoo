// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView public accessors co-located with shell/ (MainWindow API surface).

#include "imageview.h"
#include "imageitem.h"

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


















































int ImageView::itemCount() const
{
    return m_items.size();
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

