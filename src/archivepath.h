// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ARCHIVEPATH_H
#define ARCHIVEPATH_H

#include <QString>
#include <QStringList>

/**
 * One-level archive member references for session paths.
 *
 * Syntax (URL-like, future-compatible):
 *   file:///home/user/archive.tar//archive:dir/photo.jpg
 *   /home/user/archive.tar//archive:dir/photo.jpg
 *
 * The marker "//archive:" separates the container path from the member path.
 * Only one level is supported (no archive-in-archive).
 */
namespace ArchivePath {

/** Marker between container and member (includes the trailing colon). */
inline constexpr const char kMarker[] = "//archive:";

struct Ref {
    QString archivePath; /**< Local filesystem path of the archive file. */
    QString memberPath;  /**< Path inside the archive (forward slashes). */
    bool valid = false;
};

/** True if @p path contains the archive member marker. */
bool isArchiveRef(const QString &path);

/** Parse a session/path string into archive + member. Invalid if not a ref. */
Ref parse(const QString &path);

/**
 * Build a canonical reference string.
 * Uses a file:// URL for the archive when @p archivePath is absolute.
 */
QString makeRef(const QString &archivePath, const QString &memberPath);

/** Local filesystem path of the archive container (empty if not a ref). */
QString archiveFilePath(const QString &path);

/** Member path inside the archive (empty if not a ref). */
QString memberPath(const QString &path);

/** Display name for UI (member basename, or full path if not a ref). */
QString displayName(const QString &path);

/** True if the path looks like a supported archive container (by suffix). */
bool isArchiveFile(const QString &path);

/** Lowercase archive suffixes (tar, zip, …). */
QStringList archiveSuffixes();

} // namespace ArchivePath

#endif // ARCHIVEPATH_H
