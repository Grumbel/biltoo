// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Thin ImageView accessors and pack/decode status queries.

#include "imageview.h"
#include "imageitem.h"
#include "display/imagecache.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"

#include <QSet>
#include <QGraphicsItem>

void ImageView::requestDebouncedGalleryPack(GalleryPackReason reason)
{
    const bool progressive = m_gallerySizeResolve.active();
    m_layoutDebounce.arm(reason, progressive);
    if (!m_layoutDebounceTimer) {
        if (progressive) {
            m_gallery.ensurePlaceholders();
        }
        m_gallery.applyLayout(reason);
        return;
    }
    // Continuous sizeReady restarts a quiet-period timer and can leave the
    // sized prefix unlaid-out for the whole probe stream. Force a pack when
    // the progressive arm has been pending past the max wait.
    if (progressive && m_layoutDebounce.progressiveMaxWaitExceeded()) {
        m_layoutDebounceTimer->stop();
        GalleryPackReason r = reason;
        if (m_layoutDebounce.take(&r)) {
            m_gallery.ensurePlaceholders();
            m_gallery.applyLayout(r);
        }
        return;
    }
    m_layoutDebounceTimer->setInterval(
        progressive ? LayoutDebounce::kProgressiveIntervalMs
                    : LayoutDebounce::kIntervalMs);
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
    if (m_canvasBg.primaryColor().isValid()) {
        return m_canvasBg.primaryColor();
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

