// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/sessionexpand.h"

#include "archivepath.h"
#include "imageloader.h"
#include "pagepath.h"
#include "host/thumtoocache.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>

namespace SessionExpand {

QString canonicalImagePath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    const QString abs = info.absoluteFilePath();
    return abs.isEmpty() ? path : abs;
}

using ReportFn = std::function<void(const QString &message, int current, int total)>;
using CancelFn = std::function<bool()>; // true → abort

void expandReport(const ReportFn &report, const QString &message,
                  int current = -1, int total = -1)
{
    if (report) {
        report(message, current, total);
    }
}

/** Expand a concrete filesystem file (PDF/EPUB/DjVu/archive/plain image). */
void appendFileContainerOrImage(QStringList &images, const QString &path,
                                const ReportFn &report)
{
    if (PagePath::isPdfFile(path) && ThumtooCache::isAvailable()) {
        const QString name = QFileInfo(path).fileName();
        expandReport(report, QObject::tr("Indexing PDF “%1”…").arg(name));
        const QStringList pages = ThumtooCache::expandPdfToPageRefs(path);
        if (!pages.isEmpty()) {
            expandReport(report,
                         QObject::tr("PDF “%1”: %n page(s)", "", pages.size()).arg(name));
        }
        images.append(pages);
        return;
    }
    if (PagePath::isEpubFile(path) && ThumtooCache::isAvailable()) {
        const QString name = QFileInfo(path).fileName();
        expandReport(report, QObject::tr("Indexing EPUB “%1”…").arg(name));
        const QStringList pages = ThumtooCache::expandEpubToPageRefs(path);
        if (!pages.isEmpty()) {
            expandReport(report,
                         QObject::tr("EPUB “%1”: %n page(s)", "", pages.size()).arg(name));
        }
        images.append(pages);
        return;
    }
    if (PagePath::isDjvuFile(path) && ThumtooCache::isAvailable()) {
        const QString name = QFileInfo(path).fileName();
        expandReport(report, QObject::tr("Indexing DjVu “%1”…").arg(name));
        const QStringList pages = ThumtooCache::expandDjvuToPageRefs(path);
        if (!pages.isEmpty()) {
            expandReport(report,
                         QObject::tr("DjVu “%1”: %n page(s)", "", pages.size()).arg(name));
        }
        images.append(pages);
        return;
    }
    if (ArchivePath::isArchiveFile(path) && ThumtooCache::isAvailable()) {
        const QString name = QFileInfo(path).fileName();
        bool fromStore = false;
        const QStringList members =
            ThumtooCache::expandArchiveToImageRefs(path, &fromStore);
        if (!fromStore && !members.isEmpty()) {
            expandReport(report,
                         QObject::tr("Archive “%1”: %n image(s)", "", members.size())
                             .arg(name));
        } else if (!fromStore) {
            expandReport(report,
                         QObject::tr("No images in archive “%1”").arg(name));
        }
        images.append(members);
        return;
    }
    if (ImageLoader::isImageFile(path)) {
        const QString c = canonicalImagePath(path);
        if (!c.isEmpty()) {
            images.append(c);
        }
    }
}

/**
 * Expand one user-supplied path into session image URIs.
 * Returns false if @a cancel requested an abort (partial @a images may remain).
 */
bool expandOneInputPath(QStringList &images, const QString &path, bool recursive,
                        const ReportFn &report, const CancelFn &cancel)
{
    if (cancel && cancel()) {
        return false;
    }
    if (PagePath::isPageRef(path)) {
        const PagePath::Ref ref = PagePath::parse(path);
        if (ref.valid) {
            if (ref.isEpub()) {
                images.append(PagePath::makeEpubRef(ref.pdfPath, ref.page,
                                                    ref.epubLayoutParams));
            } else {
                images.append(PagePath::makeRef(ref.pdfPath, ref.page));
            }
        }
        return true;
    }
    if (PagePath::isPdfImageRef(path)) {
        const QString doc = PagePath::documentFilePath(path);
        const int n = PagePath::pdfImageNumber(path);
        if (!doc.isEmpty() && n >= 1) {
            images.append(PagePath::makePdfImageRef(doc, n));
        }
        return true;
    }
    if (PagePath::isPdfImagesCollection(path) && ThumtooCache::isAvailable()) {
        const QString doc = PagePath::documentFilePath(path);
        const QString name = QFileInfo(doc).fileName();
        expandReport(report, QObject::tr("Extracting images from “%1”…").arg(name));
        const QStringList imgs = ThumtooCache::expandPdfToImageRefs(doc);
        if (!imgs.isEmpty()) {
            expandReport(report,
                         QObject::tr("PDF “%1”: %n image(s)", "", imgs.size()).arg(name));
        }
        images.append(imgs);
        return true;
    }
    if (ArchivePath::isArchiveRef(path)) {
        if (ImageLoader::isImageFile(path)) {
            const ArchivePath::Ref ref = ArchivePath::parse(path);
            if (ref.valid) {
                images.append(ArchivePath::makeRef(ref.archivePath, ref.memberPath));
            }
        }
        return true;
    }
    // Location bar may leave //epub:w,h,fs after stripping //page:N.
    if (PagePath::isEpubLayoutOnly(path) && ThumtooCache::isAvailable()) {
        const QString doc = PagePath::documentFilePath(path);
        const QString layout = PagePath::epubLayoutParamsOf(path);
        const QString name = QFileInfo(doc).fileName();
        expandReport(report, QObject::tr("Indexing EPUB “%1”…").arg(name));
        QStringList pages = ThumtooCache::expandEpubToPageRefs(doc);
        if (!layout.isEmpty()) {
            QStringList relayout;
            for (const QString &pg : pages) {
                const PagePath::Ref r = PagePath::parse(pg);
                if (r.valid) {
                    relayout.append(PagePath::makeEpubRef(r.pdfPath, r.page, layout));
                }
            }
            pages = relayout;
        }
        if (!pages.isEmpty()) {
            expandReport(report,
                         QObject::tr("EPUB “%1”: %n page(s)", "", pages.size()).arg(name));
        }
        images.append(pages);
        return true;
    }

    // Suffix-known containers/images: expand without isFile()/isDir() first so
    // a single cold USB stat is not required before "Indexing…".
    if (PagePath::isPdfFile(path) || PagePath::isEpubFile(path) || PagePath::isDjvuFile(path)
        || ArchivePath::isArchiveFile(path) || ImageLoader::isImageFile(path)) {
        appendFileContainerOrImage(images, path, report);
        return true;
    }

    // Unknown path shape — may be a directory (must stat) or a suffix-less file.
    const QFileInfo info(path);
    if (info.isDir()) {
        expandReport(report, QObject::tr("Scanning folder “%1”…").arg(info.fileName()));
        const QDir::Filters filters = QDir::Files | QDir::Readable | QDir::NoDotAndDotDot;
        const QDirIterator::IteratorFlags flags = recursive
            ? QDirIterator::Subdirectories
            : QDirIterator::NoIteratorFlags;
        QDirIterator it(path, filters, flags);
        while (it.hasNext()) {
            if (cancel && cancel()) {
                return false;
            }
            appendFileContainerOrImage(images, it.next(), report);
        }
        return true;
    }
    if (info.isFile()) {
        appendFileContainerOrImage(images, path, report);
    }
    return true;
}

QStringList expandPathList(const QStringList &paths, bool recursive,
                           const ReportFn &report, const CancelFn &cancel)
{
    QStringList images;
    for (const QString &path : paths) {
        if (!expandOneInputPath(images, path, recursive, report, cancel)) {
            break;
        }
    }
    return images;
}

QString emptyResultMessage(const QStringList &paths, bool append)
{
    bool anyPdf = false;
    bool anyEpub = false;
    for (const QString &p : paths) {
        if (PagePath::isPdfFile(p)) {
            anyPdf = true;
        }
        if (PagePath::isEpubFile(p)) {
            anyEpub = true;
        }
    }
    if ((anyPdf || anyEpub) && !ThumtooCache::isAvailable()) {
        return QObject::tr("Cannot open PDF/EPUB: thumtoo is not available.");
    }
    if (anyEpub) {
        return QObject::tr(
            "Cannot open EPUB (no pages found). Rebuild thumtoo "
            "with MuPDF and update the biltoo flake input.");
    }
    if (anyPdf) {
        return QObject::tr(
            "Cannot open PDF (no pages found). Rebuild thumtoo "
            "with Poppler/MuPDF and update the biltoo flake input.");
    }
    return append ? QObject::tr("No readable images to add.")
                  : QObject::tr("No readable images found.");
}


} // namespace SessionExpand
