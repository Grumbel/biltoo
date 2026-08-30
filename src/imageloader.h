// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QImage>
#include <QString>
#include <QStringList>

/**
 * Central image decode path: try QImageReader first, then libvips when built
 * with QIMGVIEW_HAVE_VIPS. Used by the main view, thumbnails and file filters.
 */
namespace ImageLoader {

/** Call once at process start (initialises libvips when available). */
void init(const char *argv0);

/** True when this build linked against libvips. */
bool hasVips();

/**
 * Load a full image as 8-bit RGB(A) suitable for display.
 * Returns a null QImage on failure.
 */
QImage load(const QString &path);

/**
 * Load a downscaled preview (longest edge ≈ maxEdge). Falls back to a full
 * load + smooth scale when the backend cannot shrink during decode.
 */
QImage loadThumbnail(const QString &path, int maxEdge);

/** True if the path's suffix is a known image extension (Qt and/or vips). */
bool isImageFile(const QString &path);

/** Lowercase suffixes accepted by isImageFile(). */
QStringList imageSuffixes();

} // namespace ImageLoader

#endif // IMAGELOADER_H
