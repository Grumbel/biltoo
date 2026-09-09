// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pagepath.h"

#include "archivepath.h"

#include <QFileInfo>
#include <QUrl>

namespace PagePath {

bool isPageRef(const QString &path)
{
    return path.contains(QLatin1String(kPageMarker));
}

Ref parse(const QString &path)
{
    Ref out;
    const int pageIdx = path.indexOf(QLatin1String(kPageMarker));
    if (pageIdx < 0) {
        return out;
    }
    QString left = path.left(pageIdx);
    QString right = path.mid(pageIdx + QLatin1String(kPageMarker).size());

    QString layoutParams;
    const int epubIdx = left.indexOf(QLatin1String(kEpubMarker));
    if (epubIdx >= 0) {
        layoutParams = left.mid(epubIdx + QLatin1String(kEpubMarker).size());
        left = left.left(epubIdx);
    }

    if (left.startsWith(QLatin1String("file:"))) {
        const QUrl url(left);
        left = url.isLocalFile() ? url.toLocalFile() : left;
    }
    bool ok = false;
    const int page = right.toInt(&ok);
    if (!ok || page < 1) {
        return out;
    }
    out.pdfPath = left;
    out.page = page;
    out.epubLayoutParams = layoutParams;
    out.valid = !out.pdfPath.isEmpty();
    return out;
}

QString makeRef(const QString &pdfPath, int page_1based)
{
    if (pdfPath.isEmpty() || page_1based < 1) {
        return {};
    }
    QString abs = pdfPath;
    const QFileInfo info(pdfPath);
    if (info.exists()) {
        abs = info.absoluteFilePath();
    }
    return abs + QLatin1String(kPageMarker) + QString::number(page_1based);
}

QString makeEpubRef(const QString &epubPath, int page_1based, const QString &layoutParams)
{
    if (epubPath.isEmpty() || page_1based < 1 || layoutParams.isEmpty()) {
        return {};
    }
    QString abs = epubPath;
    const QFileInfo info(epubPath);
    if (info.exists()) {
        abs = info.absoluteFilePath();
    }
    return abs + QLatin1String(kEpubMarker) + layoutParams + QLatin1String(kPageMarker)
           + QString::number(page_1based);
}

QString pdfFilePath(const QString &path)
{
    const Ref r = parse(path);
    return r.valid ? r.pdfPath : QString();
}

int pageNumber(const QString &path)
{
    const Ref r = parse(path);
    return r.valid ? r.page : 0;
}

QString displayName(const QString &path)
{
    if (isPageRef(path)) {
        const Ref r = parse(path);
        if (r.valid) {
            const QString base = QFileInfo(r.pdfPath).fileName();
            return QStringLiteral("%1 — p.%2").arg(base).arg(r.page);
        }
    }
    if (ArchivePath::isArchiveRef(path)) {
        return ArchivePath::displayName(path);
    }
    return QFileInfo(path).fileName();
}

QString canonicalSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    if (isPageRef(path)) {
        const Ref r = parse(path);
        if (!r.valid) {
            return path;
        }
        if (r.isEpub()) {
            return makeEpubRef(r.pdfPath, r.page, r.epubLayoutParams);
        }
        return makeRef(r.pdfPath, r.page);
    }
    return ArchivePath::canonicalSessionPath(path);
}

bool isPdfFile(const QString &path)
{
    if (isPageRef(path) || ArchivePath::isArchiveRef(path)) {
        return false;
    }
    const QString name = QFileInfo(path).fileName().toLower();
    return name.endsWith(QLatin1String(".pdf"));
}

bool isEpubFile(const QString &path)
{
    if (isPageRef(path) || ArchivePath::isArchiveRef(path)) {
        return false;
    }
    const QString name = QFileInfo(path).fileName().toLower();
    return name.endsWith(QLatin1String(".epub"));
}

QStringList pdfSuffixes()
{
    return {QStringLiteral("pdf")};
}

QStringList epubSuffixes()
{
    return {QStringLiteral("epub")};
}

bool isDjvuFile(const QString &path)
{
    if (isPageRef(path) || ArchivePath::isArchiveRef(path)) {
        return false;
    }
    const QString name = QFileInfo(path).fileName().toLower();
    return name.endsWith(QLatin1String(".djvu"))
        || name.endsWith(QLatin1String(".djv"));
}

QStringList djvuSuffixes()
{
    return {QStringLiteral("djvu"), QStringLiteral("djv")};
}

} // namespace PagePath
