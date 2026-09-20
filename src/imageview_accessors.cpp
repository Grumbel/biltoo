// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Thin ImageView accessors and pack/decode status queries.

#include "imageview.h"
#include "imageitem.h"
#include "imagecache.h"
#include "gallerysoftsm.h"
#include "displayquality.h"

#include <QSet>
#include <QGraphicsItem>

void ImageView::requestDebouncedGalleryPack(GalleryPackReason reason)
{
    m_layoutDebounce.arm(reason);
    if (!m_layoutDebounceTimer) {
        applyLayout(reason);
        return;
    }
    m_layoutDebounceTimer->start();
}

int ImageView::pendingDecodeCount() const
{
    // Remaining work overview — not concurrent inflight. Counting only inflight
    // flickered 1↔0 as each soft job finished before the next was claimed.
    int n = m_displayPipeline.loadGate().pendingWorkspaceAddCount()
          + m_displayPipeline.loadGate().pendingRestoreCount();

    if (isGalleryMode()) {
        // Gallery: blanks still need LQIP. LQIP-only is intentional underlay
        // (tiles own sharpness) — do not count as remaining soft work.
        QSet<QString> blankPaths;
        for (ImageItem *item : m_items) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            if (!item->hasDisplayPixels()) {
                blankPaths.insert(item->path());
            }
        }
        n += blankPaths.size();
    } else if (isWorkspaceMode()) {
        // Workspace may still climb PreferCache for soft+ samples.
        QSet<QString> weakPaths;
        for (ImageItem *item : m_items) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            const int edge = item->displayPixelLongEdge();
            if (!item->hasDisplayPixels()
                || edge <= DisplayQuality::kLqipMaxEdge) {
                weakPaths.insert(item->path());
            }
        }
        n += weakPaths.size();
    }

    // Slideshow preload queue (inflight + pending neighbours).
    if (m_slideshow.hud().isProgressActive()) {
        n += m_slideshow.phase().rasterQueueCount();
        const int need = 0; // need edge checked via target below if needed
        Q_UNUSED(need);
        if (m_slideshow.phase().hasFromPath()
            && ImageCache::longEdge(m_slideshow.phase().fromImageRef()) > 0
            && ImageCache::longEdge(m_slideshow.phase().fromImageRef())
                   < (m_slideshow.slideshowTargetEdge() * 7) / 10) {
            // Current slide still soft — count as remaining quality work once.
            if (!m_slideshow.phase().rasterInflightContains(m_slideshow.phase().fromPathRef())
                && !m_slideshow.phase().rasterPendingContains(m_slideshow.phase().fromPathRef())) {
                ++n;
            }
        }
    }
    return n;
}















































Qt::AspectRatioMode ImageView::currentFitAspectMode() const
{
    return m_framing.aspectMode();
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

QStringList ImageView::selectedPaths() const
{
    QStringList paths;
    if (isImageMode()) {
        if (ImageItem *item = primaryItem()) {
            paths.append(item->path());
        } else if (hasClassicPath()) {
            paths.append(classicPath());
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
