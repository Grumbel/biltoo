// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "archivepath.h"

#include <QFileInfo>
#include <QUrl>

namespace ArchivePath {

bool isArchiveRef(const QString &path)
{
    return path.contains(QLatin1String(kMarker));
}

Ref parse(const QString &path)
{
    Ref r;
    const int idx = path.indexOf(QLatin1String(kMarker));
    if (idx < 0) {
        return r;
    }
    QString left = path.left(idx);
    QString right = path.mid(idx + 10); // "//archive:" is 10 chars

    if (left.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        const QUrl url(left);
        left = url.toLocalFile();
        if (left.isEmpty()) {
            left = QUrl::fromPercentEncoding(url.path().toUtf8());
        }
    }
    left = QFileInfo(left).absoluteFilePath();

    // Normalise member to forward slashes, strip leading ./ and /
    right.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (right.startsWith(QLatin1String("./"))) {
        right = right.mid(2);
    }
    while (right.startsWith(QLatin1Char('/'))) {
        right = right.mid(1);
    }

    if (left.isEmpty() || right.isEmpty()) {
        return r;
    }
    r.archivePath = left;
    r.memberPath = right;
    r.valid = true;
    return r;
}

QString makeRef(const QString &archivePath, const QString &memberPath)
{
    if (archivePath.isEmpty() || memberPath.isEmpty()) {
        return {};
    }
    QString member = memberPath;
    member.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (member.startsWith(QLatin1String("./"))) {
        member = member.mid(2);
    }
    while (member.startsWith(QLatin1Char('/'))) {
        member = member.mid(1);
    }
    const QString abs = QFileInfo(archivePath).absoluteFilePath();
    const QString left = QUrl::fromLocalFile(abs).toString(QUrl::FullyEncoded);
    return left + QLatin1String(kMarker) + member;
}

QString archiveFilePath(const QString &path)
{
    const Ref r = parse(path);
    return r.valid ? r.archivePath : QString();
}

QString memberPath(const QString &path)
{
    const Ref r = parse(path);
    return r.valid ? r.memberPath : QString();
}

QString displayName(const QString &path)
{
    if (!isArchiveRef(path)) {
        return QFileInfo(path).fileName();
    }
    const Ref r = parse(path);
    if (!r.valid) {
        return path;
    }
    const int slash = r.memberPath.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0 && slash + 1 < r.memberPath.size()) {
        return r.memberPath.mid(slash + 1);
    }
    return r.memberPath;
}

QStringList archiveSuffixes()
{
    static const QStringList list = {
        QStringLiteral("tar"),
        QStringLiteral("tar.gz"), QStringLiteral("tgz"),
        QStringLiteral("tar.bz2"), QStringLiteral("tbz2"), QStringLiteral("tbz"),
        QStringLiteral("tar.xz"), QStringLiteral("txz"),
        QStringLiteral("tar.zst"), QStringLiteral("tzst"),
        QStringLiteral("zip"),
        QStringLiteral("7z"),
        QStringLiteral("rar"),
        QStringLiteral("cpio"),
        QStringLiteral("iso"),
        QStringLiteral("cab"),
        QStringLiteral("ar"),
        QStringLiteral("a"),
    };
    return list;
}

bool isArchiveFile(const QString &path)
{
    if (isArchiveRef(path)) {
        return false;
    }
    const QString name = QFileInfo(path).fileName().toLower();
    for (const QString &suf : archiveSuffixes()) {
        if (name.endsWith(QLatin1Char('.') + suf)) {
            return true;
        }
    }
    // Compound suffixes already covered; also plain .gz of a tar often named .tar.gz
    return false;
}

} // namespace ArchivePath
