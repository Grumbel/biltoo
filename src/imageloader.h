// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

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
 * Primary peak from attentionPoints(); falls back to image centre.
 */
bool attentionPoint(const QImage &image, QPointF *normalizedOut);

/**
 * Up to @a maxPoints local saliency peaks (normalized 0–1), strongest first.
 * Uses a pure-Qt local-contrast map + non-max suppression (no OpenCV). When
 * VIPS is available, the smartcrop attention peak is seeded as a candidate.
 * Returns false when the image is empty or no peaks are found.
 */
bool attentionPoints(const QImage &image, QVector<QPointF> *normalizedOut,
                     int maxPoints = 5);

/**
 * Axis-aligned smart crop of @a cropW × @a cropH within @a image (pixels),
 * using libvips INTERESTING_ATTENTION (peak-centred window, edge-clamped).
 * Returns false without VIPS, on empty input, or if the size is invalid.
 */
bool smartCropRect(const QImage &image, int cropW, int cropH, QRect *pixelRectOut);

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
