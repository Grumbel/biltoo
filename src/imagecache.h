// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGECACHE_H
#define IMAGECACHE_H

#include <QImage>
#include <QString>
#include <QStringList>

/**
 * Process-wide decoded-image cache shared by filmstrip, gallery, Image mode,
 * and slideshow. Entries are path-keyed; put() keeps the larger long-edge
 * version. Full multi‑megapixel frames are accepted but the warm/ensure path
 * targets preview size (kPreviewEdge) so slideshow can open without disk.
 */
namespace ImageCache {

/** Default long-edge for slideshow / shared previews. */
constexpr int kPreviewEdge = 512;

/** Best cached image for path, or null. If minLongEdge > 0, require that size. */
QImage get(const QString &path, int minLongEdge = 0);

/** Insert; replaces only if the new image is larger on the long edge. */
void put(const QString &path, const QImage &image);

/** True if get(path, minLongEdge) would succeed. */
bool has(const QString &path, int minLongEdge = 0);

/**
 * Return a cached image with long edge >= maxEdge when possible.
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
