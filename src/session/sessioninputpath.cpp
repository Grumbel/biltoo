// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/sessioninputpath.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

namespace SessionInputPath {

namespace {

bool hasHttpScheme(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

bool hasFileScheme(const QUrl &url)
{
    return url.scheme().compare(QLatin1String("file"), Qt::CaseInsensitive) == 0;
}

/**
 * True if the trimmed string looks like a URL with a scheme, using QUrl only.
 * Rejects scheme-less strings so ordinary paths are not forced through URL rules.
 */
bool looksLikeUrlWithScheme(const QString &trimmed)
{
    // QUrl::TolerantMode accepts many forms; require an explicit scheme + ":"
    // and a non-empty scheme that is not a Windows drive letter (single letter).
    const QUrl url(trimmed, QUrl::TolerantMode);
    if (!url.isValid() || url.scheme().isEmpty()) {
        return false;
    }
    // "C:/foo" on Windows can parse with scheme "C" — treat as local path.
    if (url.scheme().size() == 1) {
        return false;
    }
    // Must have "scheme:" prefix in the original text (QUrl can invent schemes).
    const int colon = trimmed.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
        return false;
    }
    const QString prefix = trimmed.left(colon);
    if (prefix.compare(url.scheme(), Qt::CaseInsensitive) != 0) {
        return false;
    }
    return true;
}

} // namespace

Classified classify(const QString &input)
{
    Classified out;
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        out.kind = Kind::Empty;
        return out;
    }

    if (!looksLikeUrlWithScheme(trimmed)) {
        out.kind = Kind::LocalPath;
        out.localPath = trimmed;
        return out;
    }

    const QUrl url(trimmed, QUrl::TolerantMode);
    out.url = url;

    if (hasFileScheme(url)) {
        out.kind = Kind::FileUrl;
        // QUrl::toLocalFile understands file://; do not run QFileInfo on the URL text.
        out.localPath = url.toLocalFile();
        if (out.localPath.isEmpty()) {
            // Opaque or non-local file URL — still not an OS path string.
            out.kind = Kind::OtherUrl;
        }
        return out;
    }

    if (hasHttpScheme(url) && url.isValid()) {
        // Require authority or path so bare "http:" is not treated as downloadable.
        if (!url.host().isEmpty() || !url.path().isEmpty()) {
            out.kind = Kind::HttpUrl;
            return out;
        }
    }

    out.kind = Kind::OtherUrl;
    return out;
}

bool isHttpUrl(const QString &input)
{
    return classify(input).kind == Kind::HttpUrl;
}

QString urlPathLeafName(const QUrl &url)
{
    // QUrl::fileName is defined on the URL path, not the OS filesystem.
    QString leaf = url.fileName();
    if (leaf.isEmpty()) {
        const QString path = url.path();
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0 && slash + 1 < path.size()) {
            leaf = path.mid(slash + 1);
        }
    }
    if (leaf.isEmpty()) {
        return QStringLiteral("download");
    }
    // Sanitize only characters that are illegal in a single path segment on
    // common hosts — still not parsing the URL as a filesystem path.
    leaf.replace(QLatin1Char('/'), QLatin1Char('_'));
    leaf.replace(QLatin1Char('\\'), QLatin1Char('_'));
    leaf.replace(QLatin1Char('\0'), QLatin1Char('_'));
    return leaf;
}

QString remoteCachePath(const QUrl &httpUrl, const QString &cacheRoot)
{
    QDir dir(cacheRoot);
    dir.mkpath(QStringLiteral("remote"));
    const QByteArray digest = QCryptographicHash::hash(
        httpUrl.toEncoded(QUrl::FullyEncoded), QCryptographicHash::Sha256);
    const QString hex = QString::fromLatin1(digest.toHex().left(16));
    const QString leaf = urlPathLeafName(httpUrl);
    return dir.filePath(QStringLiteral("remote/%1_%2").arg(hex, leaf));
}

QString canonicalForSession(const QString &input)
{
    const Classified c = classify(input);
    switch (c.kind) {
    case Kind::Empty:
        return {};
    case Kind::HttpUrl:
    case Kind::OtherUrl:
        // Preserve the URL string; never absoluteFilePath a non-file URL.
        return input.trimmed();
    case Kind::FileUrl:
    case Kind::LocalPath: {
        const QString path = c.localPath;
        if (path.isEmpty()) {
            return input.trimmed();
        }
        // Absolute form for session identity without requiring existence.
        // QFileInfo is only applied to a known filesystem path string.
        const QFileInfo info(path);
        const QString abs = info.absoluteFilePath();
        return abs.isEmpty() ? path : abs;
    }
    }
    return input.trimmed();
}

} // namespace SessionInputPath
