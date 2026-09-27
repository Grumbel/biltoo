// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONINPUTPATH_H
#define SESSIONINPUTPATH_H

#include <QString>
#include <QUrl>

/**
 * Classify a user-supplied open/append string without treating it as a
 * filesystem path first.
 *
 * http(s) and other non-file schemes must not go through QFileInfo /
 * std::filesystem::path / absoluteFilePath — those APIs assume OS paths and
 * turn "https://host/a.pdf" into "$PWD/https:/host/a.pdf".
 */
namespace SessionInputPath {

enum class Kind {
    Empty,
    /** Ordinary local path string (relative or absolute), not a URL scheme. */
    LocalPath,
    /** file: URL; localPath() is the decoded filesystem path. */
    FileUrl,
    /** http: or https: URL suitable for download-then-open. */
    HttpUrl,
    /** Some other URL scheme (ftp, about, …) — not handled as a local file. */
    OtherUrl,
};

struct Classified {
    Kind kind = Kind::Empty;
    /** Parsed URL when kind is FileUrl, HttpUrl, or OtherUrl; else empty. */
    QUrl url;
    /**
     * Filesystem path only when kind is LocalPath or FileUrl.
     * Empty for HttpUrl / OtherUrl / Empty — callers must not invent one.
     */
    QString localPath;
};

/**
 * Classify @p input (CLI arg, drop string, paste).
 * Trims outer whitespace. Does not stat the disk.
 */
Classified classify(const QString &input);

/** True when classify would yield HttpUrl. */
bool isHttpUrl(const QString &input);

/**
 * Leaf name for cache files derived from a URL path only (QUrl::fileName).
 * Never uses QFileInfo or std::filesystem on the full URL string.
 * Falls back to "download" if the URL path has no segment.
 */
QString urlPathLeafName(const QUrl &url);

/**
 * Stable cache file path under @p cacheRoot/remote/ for an http(s) URL.
 * @p cacheRoot is typically QStandardPaths::CacheLocation (a real directory).
 * Hash is over the encoded URL; leaf name comes from urlPathLeafName only.
 */
QString remoteCachePath(const QUrl &httpUrl, const QString &cacheRoot);

/**
 * For session identity: local/file → absolute path when possible without
 * requiring the file to exist; http(s) → trimmed URL string unchanged;
 * other → trimmed input unchanged.
 */
QString canonicalForSession(const QString &input);

} // namespace SessionInputPath

#endif // SESSIONINPUTPATH_H
