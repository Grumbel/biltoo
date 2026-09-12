// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGECACHE_H
#define IMAGECACHE_H

#include <QImage>
#include <QString>
#include <QStringList>

/**
 * Process-wide **undecoded-appearance** raster map (path → best sample).
 *
 * Shared by filmstrip, gallery, Image mode, and slideshow. This is the single
 * host-side decoded cache. Thumtoo remains the durable compressed store.
 *
 * Contract:
 * - Keys are decode paths (file / archive / page URI), not SessionImageId.
 * - Values are raw samples (no content flip/rotate/crop/grade applied).
 * - put() is upward-only by long edge; larger replaces smaller.
 * - Frames larger than kDisplayMaxEdge are clamped on insert (RAM bound).
 * - Soft ladder (≤512) and display edges (≤2048) share one slot per path.
 * - Eviction is LRU by access order (get/put touch the entry).
 *
 * See docs/PIXEL_HOST_CACHE.md.
 */
namespace ImageCache {

/** Soft / filmstrip / warm default (matches thumtoo soft max). */
constexpr int kPreviewEdge = 512;

/**
 * Max long edge retained in process memory. Matches thumtoo image ladder
 * (viewport × DPR × motion headroom), not native multi‑MP frames.
 */
constexpr int kDisplayMaxEdge = 2048;

/** Long edge of image, or 0 if null. */
inline int longEdge(const QImage &img)
{
    return img.isNull() ? 0 : qMax(img.width(), img.height());
}

/** True when long edge ≥ need (need ≤ 0 ⇒ any non-null). */
inline bool adequate(const QImage &img, int needLongEdge)
{
    return !img.isNull() && (needLongEdge <= 0 || longEdge(img) >= needLongEdge);
}

/**
 * Scale down so long edge ≤ maxEdge (KeepAspectRatio). No-op if already small
 * or maxEdge ≤ 0. Pure — does not touch the cache.
 */
QImage clampToMaxEdge(const QImage &image, int maxEdge = kDisplayMaxEdge);

/** Best cached image for path, or null. If minLongEdge > 0, require that size. */
QImage get(const QString &path, int minLongEdge = 0);

/**
 * Insert raw sample; replaces only if the new image is larger on the long edge.
 * Clamps to kDisplayMaxEdge before store.
 */
void put(const QString &path, const QImage &image);

/** True if get(path, minLongEdge) would succeed. */
bool has(const QString &path, int minLongEdge = 0);

/**
 * Return a cached image with long edge ≥ maxEdge when possible.
 * On miss, schedule an async loadThumbnail(path, maxEdge) into the cache.
 * Returns whatever is already cached (possibly smaller or null).
 */
QImage ensure(const QString &path, int maxEdge = kPreviewEdge);

/** ensure() every path (deduped). Call when a slideshow session starts. */
void warm(const QStringList &paths, int maxEdge = kPreviewEdge);

/** Drop all entries and in-flight ensures. */
void clear();

} // namespace ImageCache

#endif // IMAGECACHE_H
