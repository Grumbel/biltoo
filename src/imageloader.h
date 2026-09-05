// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>

/**
 * Central image decode path: try QImageReader first (including system Qt
 * imageformat plugins such as KDE KImageFormats for XCF/KRA/ORA), then libvips
 * when built with BILTOO_HAVE_VIPS. Used by the main view, thumbnails and
 * file filters.
 */
namespace ImageLoader {

/** Call once at process start (initialises libvips when available). */
void init(const char *argv0);

/** True when this build linked against libvips. */
bool hasVips();

/**
 * Approximate attention / focus point in normalized image coordinates (0–1).
 * Uses libvips smartcrop (VIPS_INTERESTING_ATTENTION) on a downscaled copy.
 * Returns false when VIPS is unavailable, the image is empty, or the probe fails.
 */
bool attentionPoint(const QImage &image, QPointF *normalizedOut);

/**
 * Fast size probe without a full pixel decode when the backend allows it.
 * Tries QImageReader::size() (with auto-transform), then a libvips header open
 * when built with VIPS. Returns an invalid QSize on failure.
 */
QSize probeSize(const QString &path);

/**
 * Load a full image as 8-bit RGB(A) suitable for display.
 * Returns a null QImage on failure.
 */
QImage load(const QString &path);

/**
 * Load a downscaled preview (longest edge ≈ maxEdge). Falls back to a full
 * load + smooth scale when the backend cannot shrink during decode.
 * Aspect ratio is preserved (not square-cropped).
 */
QImage loadThumbnail(const QString &path, int maxEdge);

/**
 * Thin wrappers around ImageCache (shared process-wide path → image store).
 * Prefer ImageCache::* in new code.
 */
QImage cachedThumbnail(const QString &path);
void putCachedThumbnail(const QString &path, const QImage &image);
/** Ensure ≥ maxEdge in ImageCache (async on miss); return best available. */
QImage loadThumbnailCached(const QString &path, int maxEdge);

/** True if the path's suffix is a known image extension (Qt and/or vips). */
bool isImageFile(const QString &path);

/** Lowercase suffixes accepted by isImageFile(). */
QStringList imageSuffixes();

} // namespace ImageLoader

#endif // IMAGELOADER_H
