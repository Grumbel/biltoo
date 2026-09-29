#include "session/sessionexpand.h"

#include "host/archivepath.h"
#include "host/imageloader.h"
#include "host/pagepath.h"
#include "host/thumtoocache.h"
#include "session/sessioninputpath.h"

#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace SessionExpand {

void expandReport(const ReportFn &report, const QString &message,
                  int current = -1, int total = -1);

namespace {

/**
 * Download a classified http(s) URL to the biltoo cache (or reuse cache).
 * Returns a local filesystem path, or empty on failure / cancel.
 * Safe on a QThreadPool worker (local QEventLoop).
 */
QString materializeHttpUrl(const QUrl &url, const ReportFn &report,
                           const CancelFn &cancel)
{
    if (!url.isValid()) {
        return {};
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        return {};
    }

    const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString dest = SessionInputPath::remoteCachePath(url, cacheRoot);
    {
        const QFileInfo existing(dest);
        if (existing.isFile() && existing.size() > 0) {
            expandReport(report,
                         QObject::tr("Using cached “%1”…")
                             .arg(SessionInputPath::urlPathLeafName(url)));
            return dest;
        }
    }

    expandReport(report,
                 QObject::tr("Downloading “%1”…")
                     .arg(SessionInputPath::urlPathLeafName(url)));

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral(
                      "biltoo/0.2 (session open; +https://github.com/Grumbel/biltoo)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    QNetworkReply *reply = nam.get(req);
    if (!reply) {
        return {};
    }

    const QString partPath = dest + QStringLiteral(".part");
    QFile out(partPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reply->abort();
        reply->deleteLater();
        return {};
    }

    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, &out]() {
        out.write(reply->readAll());
    });

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    QTimer cancelTimer;
    if (cancel) {
        cancelTimer.setInterval(200);
        QObject::connect(&cancelTimer, &QTimer::timeout, reply, [reply, &cancel, &loop]() {
            if (cancel && cancel()) {
                reply->abort();
                loop.quit();
            }
        });
        cancelTimer.start();
    }

    loop.exec();
    cancelTimer.stop();

    if (reply->bytesAvailable() > 0) {
        out.write(reply->readAll());
    }
    out.close();

    const qint64 written = QFileInfo(partPath).size();
    const bool aborted = reply->error() == QNetworkReply::OperationCanceledError
        || (cancel && cancel());
    const bool ok = !aborted && reply->error() == QNetworkReply::NoError && written > 0;
    reply->deleteLater();

    if (!ok) {
        QFile::remove(partPath);
        return {};
    }

    QFile::remove(dest);
    if (!QFile::rename(partPath, dest)) {
        QFile::remove(partPath);
        return {};
    }
    return dest;
}

} // namespace

QString canonicalImagePath(const QString &path)
{
    return SessionInputPath::canonicalForSession(path);
}

void expandReport(const ReportFn &report, const QString &message,
                  int current, int total)
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
        } else {
            expandReport(report, ThumtooCache::formatLoadErrorMessage(path));
        }
        images.append(pages);
        return;
    }
    if (PagePath::isMarkdownFile(path) && ThumtooCache::isAvailable()) {
        const QString name = QFileInfo(path).fileName();
        expandReport(report, QObject::tr("Indexing Markdown “%1”…").arg(name));
        const QStringList pages = ThumtooCache::expandMarkdownToPageRefs(path);
        if (!pages.isEmpty()) {
            expandReport(report,
                         QObject::tr("Markdown “%1”: %n page(s)", "", pages.size()).arg(name));
        } else {
            expandReport(report, ThumtooCache::formatLoadErrorMessage(path));
        }
        images.append(pages);
        return;
    }
    if (PagePath::isPlainTextFile(path) && ThumtooCache::isAvailable()) {
        const QString name = QFileInfo(path).fileName();
        expandReport(report, QObject::tr("Indexing text “%1”…").arg(name));
        const QStringList pages = ThumtooCache::expandPlainTextToPageRefs(path);
        if (!pages.isEmpty()) {
            expandReport(report,
                         QObject::tr("Text “%1”: %n page(s)", "", pages.size()).arg(name));
        } else {
            expandReport(report, ThumtooCache::formatLoadErrorMessage(path));
        }
        images.append(pages);
        return;
    }
    if (PagePath::isTextForceRef(path) && ThumtooCache::isAvailable()) {
        const QString name = QFileInfo(PagePath::documentFilePath(path)).fileName();
        expandReport(report, QObject::tr("Indexing as text “%1”…").arg(name));
        const QStringList pages = ThumtooCache::expandTextForceToPageRefs(path);
        if (!pages.isEmpty()) {
            expandReport(report,
                         QObject::tr("Text “%1”: %n page(s)", "", pages.size()).arg(name));
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
bool expandOneInputPath(QStringList &images, const QString &pathIn, bool recursive,
                        const ReportFn &report, const CancelFn &cancel)
{
    if (cancel && cancel()) {
        return false;
    }

    const SessionInputPath::Classified classified = SessionInputPath::classify(pathIn);
    QString path;
    switch (classified.kind) {
    case SessionInputPath::Kind::Empty:
        return true;
    case SessionInputPath::Kind::HttpUrl: {
        const QString local = materializeHttpUrl(classified.url, report, cancel);
        if (local.isEmpty()) {
            expandReport(report,
                         QObject::tr("Failed to download “%1”")
                             .arg(pathIn.trimmed()));
            return true; // continue other inputs; empty overall handled by caller
        }
        path = local;
        break;
    }
    case SessionInputPath::Kind::FileUrl:
    case SessionInputPath::Kind::LocalPath:
        path = classified.localPath;
        break;
    case SessionInputPath::Kind::OtherUrl:
        expandReport(report,
                     QObject::tr("Unsupported URL scheme in “%1”")
                         .arg(pathIn.trimmed()));
        return true;
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
    if (PagePath::isPdfFile(path) || PagePath::isMarkdownFile(path)
        || PagePath::isPlainTextFile(path) || PagePath::isTextForceRef(path)
        || PagePath::isEpubFile(path) || PagePath::isDjvuFile(path)
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
    bool anyDoc = false;
    bool anyEpub = false;
    bool anyRemote = false;
    bool anyArchive = false;
    QString firstDoc;
    for (const QString &p : paths) {
        if (SessionInputPath::isHttpUrl(p)) {
            anyRemote = true;
            continue;
        }
        if (PagePath::isPdfFile(p) || PagePath::isMarkdownFile(p)
            || PagePath::isPlainTextFile(p) || PagePath::isDjvuFile(p)) {
            anyDoc = true;
            if (firstDoc.isEmpty()) {
                firstDoc = p;
            }
        }
        if (PagePath::isEpubFile(p)) {
            anyEpub = true;
            if (firstDoc.isEmpty()) {
                firstDoc = p;
            }
        }
        if (ArchivePath::isArchiveFile(p)) {
            anyArchive = true;
            if (firstDoc.isEmpty()) {
                firstDoc = p;
            }
        }
    }
    if (anyRemote) {
        return QObject::tr(
            "Could not download remote URL (network error, redirect, or empty body). "
            "Try: curl -L -o file.pdf '<url>' and open the local file.");
    }
    if ((anyDoc || anyEpub) && !ThumtooCache::isAvailable()) {
        return QObject::tr("Cannot open document: thumtoo is not available.");
    }
    // Concrete open error after expand (MuPDF last error only — no filesystem stat).
    if (!firstDoc.isEmpty()) {
        return ThumtooCache::formatLoadErrorMessage(firstDoc);
    }
    if (anyArchive) {
        return QObject::tr(
            "Could not open archive (missing file, or no image members).");
    }
    return append ? QObject::tr("No readable images to add.")
                  : QObject::tr("No readable images found.");
}


} // namespace SessionExpand
