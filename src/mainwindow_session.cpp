// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"
#include "slideshowclocks.h"
#include "viewtransform.h"
#include <QtMath>
#include <random>
#include <algorithm>
#include "thumtoocache.h"
#include "sessionopen.h"
#include "ttfp_trace.h"
#include "projectfile.h"
#include "archivepath.h"
#include "pagepath.h"
#include "epublayoutdialog.h"
#include "workspacebackgrounddialog.h"
#include "imageitem.h"
#include "imagecache.h"
#include <QFileInfo>
#include <QUrl>
#include <QPointer>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QDebug>

#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

#include <functional>

namespace {

QString imageFileDialogFilter()
{
    QStringList imagePatterns;
    for (const QString &suffix : ImageLoader::imageSuffixes()) {
        imagePatterns.append(QStringLiteral("*.%1").arg(suffix));
    }
    QStringList archivePatterns;
    for (const QString &suffix : ArchivePath::archiveSuffixes()) {
        // Compound suffixes like tar.gz → *.tar.gz
        archivePatterns.append(QStringLiteral("*.%1").arg(suffix));
    }
    const QString images = imagePatterns.join(QLatin1Char(' '));
    const QString archives = archivePatterns.join(QLatin1Char(' '));
    // First filter is the dialog default — include archives/PDFs/EPUBs so containers are visible.
    return QObject::tr(
               "Images, archives, PDF, EPUB and DjVu (%1 %2 *.pdf *.epub *.djvu *.djv);;"
               "Images only (%1);;Archives only (%2);;PDF documents (*.pdf);;"
               "EPUB books (*.epub);;DjVu documents (*.djvu *.djv);;All Files (*)")
        .arg(images, archives);
}

/**
 * Absolute path for session identity. AUDIT M17 / GUI_THREAD_AUDIT:
 * do **not** call exists(), isFile(), or canonicalFilePath() — those stat the
 * filesystem and can block for seconds on a cold USB/NFS drive before any
 * "Indexing…" UI is shown. Missing files fail later at decode.
 */
QString canonicalImagePath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    const QString abs = info.absoluteFilePath();
    return abs.isEmpty() ? path : abs;
}

using ExpandReportFn = std::function<void(const QString &message, int current, int total)>;
using ExpandCancelFn = std::function<bool()>; // true → abort

void expandReport(const ExpandReportFn &report, const QString &message,
                  int current = -1, int total = -1)
{
    if (report) {
        report(message, current, total);
    }
}

/** Expand a concrete filesystem file (PDF/EPUB/DjVu/archive/plain image). */
void appendFileContainerOrImage(QStringList &images, const QString &path,
                                const ExpandReportFn &report)
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
                        const ExpandReportFn &report, const ExpandCancelFn &cancel)
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
                           const ExpandReportFn &report, const ExpandCancelFn &cancel)
{
    QStringList images;
    for (const QString &path : paths) {
        if (!expandOneInputPath(images, path, recursive, report, cancel)) {
            break;
        }
    }
    return images;
}

QString expandEmptyResultMessage(const QStringList &paths, bool append)
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

/** Undoable session duplicate (Ctrl+D). Identity is SessionImageId, not path. */
class SessionDuplicateCommand : public QUndoCommand {
public:
    SessionDuplicateCommand(MainWindow *mw, const QList<SessionImageId> &sourceIds,
                            const QStringList &fallbackPaths)
        : QUndoCommand(QObject::tr("Duplicate"))
        , m_mw(mw)
        , m_sourceIds(sourceIds)
        , m_fallbackPaths(fallbackPaths)
    {
    }

    void undo() override
    {
        if (!m_mw || m_newIds.isEmpty()) {
            return;
        }
        QList<int> indices;
        for (SessionImageId id : m_newIds) {
            const int idx = m_mw->sessionIndexOfId(id);
            if (idx >= 0) {
                indices.append(idx);
            }
        }
        if (!indices.isEmpty()) {
            m_mw->applySessionRemoveIndices(indices);
        }
    }

    void redo() override
    {
        if (!m_mw) {
            return;
        }
        m_newIds = m_mw->applyDuplicate(m_sourceIds, m_fallbackPaths);
    }

private:
    MainWindow *m_mw = nullptr;
    QList<SessionImageId> m_sourceIds;
    QStringList m_fallbackPaths;
    QVector<SessionImageId> m_newIds;
};

/** Undoable session removal (Gallery delete / thumb remove). */
class SessionRemoveCommand : public QUndoCommand {
public:
    SessionRemoveCommand(MainWindow *mw, const QList<SessionEntrySnapshot> &entries)
        : QUndoCommand(QObject::tr("Remove from session"))
        , m_mw(mw)
        , m_entries(entries)
    {
    }

    void undo() override
    {
        if (m_mw) {
            m_mw->restoreSessionEntries(m_entries);
        }
    }

    void redo() override
    {
        if (!m_mw) {
            return;
        }
        // Prefer SessionImageId so redo stays correct after undo + insert/reorder.
        QList<int> indices;
        for (const auto &e : m_entries) {
            int idx = -1;
            if (e.id != kInvalidSessionImageId) {
                idx = m_mw->sessionIndexOfId(e.id);
            }
            if (idx < 0) {
                idx = e.index;
            }
            if (idx >= 0) {
                indices.append(idx);
            }
        }
        if (!indices.isEmpty()) {
            m_mw->applySessionRemoveIndices(indices);
        }
    }

private:
    MainWindow *m_mw = nullptr;
    QList<SessionEntrySnapshot> m_entries;
};

/** Undoable Workspace paste (new session rows + canvas tiles). */
class WorkspacePasteCommand : public QUndoCommand {
public:
    WorkspacePasteCommand(MainWindow *mw, const QList<WorkspaceItemState> &items)
        : QUndoCommand(QObject::tr("Paste Workspace tiles"))
        , m_mw(mw)
        , m_items(items)
    {
    }

    void undo() override
    {
        if (!m_mw || m_newIds.isEmpty()) {
            return;
        }
        QList<int> indices;
        for (SessionImageId id : m_newIds) {
            const int idx = m_mw->sessionIndexOfId(id);
            if (idx >= 0) {
                indices.append(idx);
            }
        }
        if (!indices.isEmpty()) {
            m_mw->applySessionRemoveIndices(indices);
        }
        m_mw->markWorkspaceDirty();
    }

    void redo() override
    {
        if (!m_mw) {
            return;
        }
        m_newIds = m_mw->applyWorkspacePaste(m_items);
    }

private:
    MainWindow *m_mw = nullptr;
    QList<WorkspaceItemState> m_items;
    QVector<SessionImageId> m_newIds;
};

/** Undoable Workspace background change. */
class WorkspaceBackgroundCommand : public QUndoCommand {
public:
    WorkspaceBackgroundCommand(MainWindow *mw,
                               const WorkspaceBackground &before,
                               const WorkspaceBackground &after)
        : QUndoCommand(QObject::tr("Workspace background"))
        , m_mw(mw)
        , m_before(before)
        , m_after(after)
    {
    }

    void undo() override
    {
        if (m_mw) {
            m_mw->applyWorkspaceBackground(m_before);
        }
    }

    void redo() override
    {
        if (m_mw) {
            m_mw->applyWorkspaceBackground(m_after);
        }
    }

private:
    MainWindow *m_mw = nullptr;
    WorkspaceBackground m_before;
    WorkspaceBackground m_after;
};

/** Undoable Workspace cut (canvas only; session rows kept). */
class WorkspaceCutCommand : public QUndoCommand {
public:
    WorkspaceCutCommand(MainWindow *mw, const QList<WorkspaceItemState> &items)
        : QUndoCommand(QObject::tr("Cut Workspace tiles"))
        , m_mw(mw)
        , m_items(items)
    {
    }

    void undo() override
    {
        if (m_mw) {
            m_mw->applyWorkspaceUncut(m_items);
        }
    }

    void redo() override
    {
        if (m_mw) {
            m_mw->applyWorkspaceCut(m_items);
        }
    }

private:
    MainWindow *m_mw = nullptr;
    QList<WorkspaceItemState> m_items;
};

} // namespace

bool MainWindow::isImageFile(const QString &path)
{
    return ImageLoader::isImageFile(path);
}

QStringList MainWindow::expandPaths(const QStringList &paths) const
{
    return expandPathList(paths, m_recursive, /*report=*/{}, /*cancel=*/{});
}

bool MainWindow::pathsNeedBackgroundExpand(const QStringList &paths) const
{
    // Heuristic only — never QFileInfo::isFile/isDir/exists or
    // ThumtooCache::isAvailable() here. Those hit the disk (or open the
    // thumtoo client) on the GUI thread and can stall for a long time on a
    // spinning-up USB drive *before* any "Indexing…" / "Opening…" status.
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        // Already session leaves — expand is pure string work, safe on GUI.
        if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
            || PagePath::isPdfImageRef(path)) {
            continue;
        }
        // Containers, directories (no image suffix), plain images, or unknown:
        // always expand on a worker (suffix checks do not stat).
        return true;
    }
    return false;
}

void MainWindow::setExpandProgressMessage(const QString &message)
{
    if (statusBar()) {
        statusBar()->showMessage(message, 0);
    }
    if (m_imageView) {
        // Centre progress suppresses the empty-session invite during expand.
        m_imageView->setCentreProgress(
            message.isEmpty() ? tr("Working…") : message);
    }
}

void MainWindow::setExpandProgressBusy(bool busy)
{
    if (m_statusProgress) {
        if (busy) {
            m_statusProgress->setRange(0, 0);
            m_statusProgress->show();
        } else {
            m_statusProgress->hide();
            m_statusProgress->setRange(0, 1);
            m_statusProgress->setValue(0);
        }
    }
    if (!busy && m_imageView) {
        // finishApplyExpandedLoad clears busy *after* enterGalleryMode, which may
        // already own the centre HUD for size-resolve — do not wipe that.
        if (!m_imageView->hostGallerySizeResolve().active()) {
            m_imageView->clearCentreProgress();
        }
    }
}

void MainWindow::setExpandProgress(int current, int total, const QString &message)
{
    if (statusBar()) {
        statusBar()->showMessage(message, 0);
    }
    if (m_statusProgress) {
        if (total > 0) {
            m_statusProgress->setRange(0, total);
            m_statusProgress->setValue(ViewTransform::clampedProgress(current, total));
            m_statusProgress->show();
        } else {
            m_statusProgress->setRange(0, 0);
            m_statusProgress->show();
        }
    }
    if (m_imageView) {
        const QString title = message.isEmpty() ? tr("Working…") : message;
        const QString detail = (total > 0)
            ? tr("%1 / %2").arg(current).arg(total)
            : QString();
        // When message already contains N/M, avoid duplicating the detail line.
        if (!detail.isEmpty() && message.contains(QLatin1Char('/'))) {
            m_imageView->setCentreProgress(title);
        } else {
            m_imageView->setCentreProgress(title, detail);
        }
    }
}

void MainWindow::applyExpandedPathsResult(const QStringList &images, bool append, int startAt,
                                          const QStringList &sourcePaths)
{
    if (images.isEmpty()) {
        setExpandProgressBusy(false);
        if (statusBar()) {
            statusBar()->showMessage(
                expandEmptyResultMessage(sourcePaths, append), 8000);
        }
        return;
    }
    // Expand worker done — drop Opening/Indexing HUD before apply/layout.
    setExpandProgressBusy(false);
    if (statusBar() && statusBar()->currentMessage().startsWith(tr("Opening"))) {
        statusBar()->clearMessage();
    }
    // Do not set "Opening N images…" here — finishApplyExpandedLoad shows it
    // only when durable sizes are still missing (warm index stays silent).
    if (append) {
        applyExpandedAppend(images);
    } else {
        applyExpandedLoad(images, startAt);
    }
}

void MainWindow::expandPathsInBackground(const QStringList &paths, bool append, int startAt)
{
    const quint64 gen = ++m_expandGeneration;
    const bool recursive = m_recursive;
    // Replace loads already clear the filmstrip in loadFiles; append keeps it.
    // Direct callers with append=false still need an immediate empty strip.
    if (!append && m_thumbnailBar && m_thumbnailBar->count() > 0) {
        m_thumbnailBar->setSession(QStringList(), QVector<SessionImageId>());
    }
    setExpandProgressBusy(true);
    // Shown immediately on the GUI thread — no disk I/O before this.
    setExpandProgressMessage(tr("Opening…"));

    const QPointer<MainWindow> guard(this);
    QThreadPool::globalInstance()->start([guard, paths, append, startAt, gen, recursive]() {
        QElapsedTimer reportClock;
        reportClock.start();
        qint64 lastReportMs = -1000;
        // Rate-limit GUI posts (~8 Hz) so listing huge archives does not flood
        // the event queue and freeze the UI that way.
        const ExpandReportFn report = [guard, gen, &reportClock, &lastReportMs](
                                          const QString &msg, int current, int total) {
            if (!guard) {
                return;
            }
            const qint64 now = reportClock.elapsed();
            if (now - lastReportMs < 120 && current >= 0 && total > 0 && current < total) {
                return;
            }
            lastReportMs = now;
            QMetaObject::invokeMethod(guard.data(), [guard, gen, msg, current, total]() {
                MainWindow *const window = guard.data();
                if (!window || gen != window->m_expandGeneration) {
                    return;
                }
                if (total > 0) {
                    window->setExpandProgress(current, total, msg);
                } else {
                    window->setExpandProgressBusy(true);
                    window->setExpandProgressMessage(msg);
                }
            }, Qt::QueuedConnection);
        };
        const ExpandCancelFn cancel = [guard, gen]() {
            MainWindow *const window = guard.data();
            return !window || gen != window->m_expandGeneration;
        };

        const QStringList images = expandPathList(paths, recursive, report, cancel);
        if (cancel()) {
            return;
        }

        QMetaObject::invokeMethod(guard.data(), [guard, gen, images, append, startAt, paths]() {
            MainWindow *const window = guard.data();
            if (!window || gen != window->m_expandGeneration) {
                return;
            }
            window->applyExpandedPathsResult(images, append, startAt, paths);
        }, Qt::QueuedConnection);
    });
}



void MainWindow::sortFileList()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    if (sortModeNeedsImageProbe()) {
        // MTime/FileSize/size probes must not run on the GUI thread.
        sortFileListWithProbesInBackground();
        return;
    }
    sortFileListSync();
}

bool MainWindow::sortModeNeedsImageProbe() const
{
    // Any sort that needs per-path disk or decode work must not run on the GUI
    // (GUI_THREAD_AUDIT G1). Name-only sort stays sync.
    return m_sortMode == SortMode::Width
        || m_sortMode == SortMode::Height
        || m_sortMode == SortMode::PixelCount
        || m_sortMode == SortMode::AspectRatio
        || m_sortMode == SortMode::MTime
        || m_sortMode == SortMode::FileSize;
}

bool MainWindow::sessionLooksLikePagedDocument(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return false;
    }
    int pages = 0;
    for (const QString &path : paths) {
        if (PagePath::isPageRef(path) || PagePath::isPdfImageRef(path)) {
            ++pages;
        }
    }
    // Majority: pure books + mixed dumps that are mostly pages.
    return pages * 2 >= paths.size();
}

LayoutMode MainWindow::initialGalleryLayoutForOpen() const
{
    if (sessionLooksLikePagedDocument(m_session.paths())) {
        return LayoutMode::Flow;
    }
    return m_galleryReturnLayout;
}


void MainWindow::sortFileListSync()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    m_session.ensureIdsAligned();

    auto nameLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(PagePath::displayName(a), PagePath::displayName(b)) < 0;
    };
    auto pathLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(a, b) < 0;
    };

    auto pathAt = [&](int i) -> const QString & { return m_session.paths().at(i); };

    QVector<int> order(m_session.paths().size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }


    switch (m_sortMode) {
    case SortMode::MTime:
    case SortMode::FileSize: {
        // Prefer background path (sortModeNeedsImageProbe). If still here,
        // snapshot metadata once — never QFileInfo inside the comparator.
        const int n = order.size();
        QVector<qint64> mtimes(n);
        QVector<qint64> fsizes(n);
        for (int i = 0; i < n; ++i) {
            const QFileInfo fi(pathAt(i));
            mtimes[i] = fi.lastModified().toMSecsSinceEpoch();
            fsizes[i] = fi.size();
        }
        if (m_sortMode == SortMode::MTime) {
            std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
                if (mtimes.at(ia) != mtimes.at(ib)) {
                    return mtimes.at(ia) < mtimes.at(ib);
                }
                return nameLess(pathAt(ia), pathAt(ib));
            });
        } else {
            std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
                if (fsizes.at(ia) != fsizes.at(ib)) {
                    return fsizes.at(ia) < fsizes.at(ib);
                }
                return nameLess(pathAt(ia), pathAt(ib));
            });
        }
        break;
    }
    case SortMode::Width:
    case SortMode::Height:
    case SortMode::PixelCount:
        // Must use sortFileListWithProbesInBackground — no probe on GUI.
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            return nameLess(pathAt(ia), pathAt(ib));
        });
        break;
    case SortMode::Path:
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            return pathLess(pathAt(ia), pathAt(ib));
        });
        break;
    case SortMode::Shuffle: {
        // Non-deterministic: new order each time this mode is applied.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(order.begin(), order.end(), gen);
        break;
    }
    case SortMode::AspectRatio:
        // Needs probes — should use background path; basename fallback here.
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            return nameLess(pathAt(ia), pathAt(ib));
        });
        break;
    case SortMode::Name:
    default:
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            return nameLess(pathAt(ia), pathAt(ib));
        });
        break;
    }

    QStringList newFiles;
    QVector<SessionImageId> newIds;
    newFiles.reserve(order.size());
    newIds.reserve(order.size());
    for (int i : order) {
        newFiles.append(m_session.paths().at(i));
        newIds.append(m_session.ids().at(i));
    }
    m_session.replaceAll(newFiles, newIds);
}

void MainWindow::applySortedSessionOrder(const QStringList &newFiles,
                                         const QVector<SessionImageId> &newIds,
                                         const std::function<void()> &onDone)
{
    m_session.replaceAll(newFiles, newIds);
    setExpandProgressBusy(false);
    if (statusBar()) {
        statusBar()->clearMessage();
    }
    if (onDone) {
        onDone();
    }
}

QVector<int> MainWindow::computeSortOrderIndices(
    SortMode mode,
    const QStringList &paths,
    const QHash<QString, QSize> &sizes,
    const QHash<QString, qint64> &mtimes,
    const QHash<QString, qint64> &fsizes)
{
    auto nameLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(PagePath::displayName(a), PagePath::displayName(b)) < 0;
    };
    auto pathLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(a, b) < 0;
    };

    QVector<int> order(paths.size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }

    if (mode == SortMode::Path) {
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            return pathLess(paths.at(ia), paths.at(ib));
        });
        return order;
    }
    if (mode == SortMode::Shuffle) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(order.begin(), order.end(), gen);
        return order;
    }

    if (mode == SortMode::MTime) {
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const qint64 ta = mtimes.value(paths.at(ia));
            const qint64 tb = mtimes.value(paths.at(ib));
            if (ta != tb) {
                return ta < tb;
            }
            return nameLess(paths.at(ia), paths.at(ib));
        });
    } else if (mode == SortMode::FileSize) {
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const qint64 sa = fsizes.value(paths.at(ia));
            const qint64 sb = fsizes.value(paths.at(ib));
            if (sa != sb) {
                return sa < sb;
            }
            return nameLess(paths.at(ia), paths.at(ib));
        });
    } else {
        auto sizeOf = [&](int i) -> QSize {
            return sizes.value(paths.at(i));
        };
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const QSize sa = sizeOf(ia);
            const QSize sb = sizeOf(ib);
            if (mode == SortMode::Width) {
                if (sa.width() != sb.width()) {
                    return sa.width() < sb.width();
                }
            } else if (mode == SortMode::Height) {
                if (sa.height() != sb.height()) {
                    return sa.height() < sb.height();
                }
            } else if (mode == SortMode::AspectRatio) {
                const qreal ra = (sa.height() > 0)
                    ? qreal(sa.width()) / qreal(sa.height()) : 0.0;
                const qreal rb = (sb.height() > 0)
                    ? qreal(sb.width()) / qreal(sb.height()) : 0.0;
                if (!qFuzzyCompare(ra, rb)) {
                    return ra < rb;
                }
            } else { // PixelCount
                const qint64 pa = qint64(sa.width()) * sa.height();
                const qint64 pb = qint64(sb.width()) * sb.height();
                if (pa != pb) {
                    return pa < pb;
                }
            }
            return nameLess(paths.at(ia), paths.at(ib));
        });
    }
    return order;
}

void MainWindow::sortFileListWithProbesInBackground(const std::function<void()> &onDone)
{
    if (m_session.paths().size() <= 1) {
        if (onDone) {
            onDone();
        }
        return;
    }
    const quint64 gen = ++m_sortGeneration;
    const SortMode mode = m_sortMode;
    const QStringList paths = m_session.paths();
    const QVector<SessionImageId> ids = m_session.ids();

    const bool diskMeta = (mode == SortMode::MTime || mode == SortMode::FileSize);
    // Progress HUD is started only if the worker finds cache misses (warm index
    // completes with no "Reading file info…" flash).

    const QPointer<MainWindow> guard(this);
    QThreadPool::globalInstance()->start([guard, gen, mode, paths, ids, onDone, diskMeta]() {
        QHash<QString, QSize> sizes;
        QHash<QString, qint64> mtimes;
        QHash<QString, qint64> fsizes;
        if (diskMeta) {
            mtimes.reserve(paths.size());
            fsizes.reserve(paths.size());
        } else {
            sizes.reserve(paths.size());
        }

        // Pass 1 — Store / cache only (no source I/O, no revalidate).
        QVector<int> missIdx;
        missIdx.reserve(paths.size());
        for (int i = 0; i < paths.size(); ++i) {
            MainWindow *const window = guard.data();
            if (!window || gen != window->m_sortGeneration) {
                return;
            }
            const QString &path = paths.at(i);
            if (diskMeta) {
                qint64 stSize = -1;
                qint64 stMtimeNs = -1;
                if (ThumtooCache::cachedFileStat(path, &stSize, &stMtimeNs)
                    && (stSize >= 0 || stMtimeNs >= 0)) {
                    mtimes.insert(path, stMtimeNs >= 0 ? stMtimeNs / 1000000 : 0);
                    fsizes.insert(path, stSize >= 0 ? stSize : 0);
                } else {
                    missIdx.append(i);
                }
            } else {
                const QSize sz = ImageLoader::probeSize(path);
                if (sz.isValid()) {
                    sizes.insert(path, sz);
                } else {
                    missIdx.append(i);
                }
            }
        }

        if (missIdx.isEmpty()) {
            const QVector<int> order = MainWindow::computeSortOrderIndices(
                mode, paths, sizes, mtimes, fsizes);
            QStringList newFiles;
            QVector<SessionImageId> newIds;
            newFiles.reserve(order.size());
            newIds.reserve(order.size());
            for (int i : order) {
                newFiles.append(paths.at(i));
                newIds.append(ids.at(i));
            }
            QMetaObject::invokeMethod(guard.data(), [guard, gen, newFiles, newIds, onDone]() {
                MainWindow *const window = guard.data();
                if (!window || gen != window->m_sortGeneration) {
                    return;
                }
                window->setExpandProgressBusy(false);
                window->applySortedSessionOrder(newFiles, newIds, onDone);
            }, Qt::QueuedConnection);
            return;
        }

        QElapsedTimer clock;
        clock.start();
        qint64 lastUi = -1000;
        const int total = paths.size();
        const int cached = total - missIdx.size();
        {
            const bool meta = diskMeta;
            QMetaObject::invokeMethod(guard.data(), [guard, gen, total, meta, cached]() {
                MainWindow *const host = guard.data();
                if (!host || gen != host->m_sortGeneration) {
                    return;
                }
                host->setExpandProgress(
                    cached, total,
                    meta ? MainWindow::tr("Reading file info… %1/%2").arg(cached).arg(total)
                         : MainWindow::tr("Measuring images… %1/%2").arg(cached).arg(total));
            }, Qt::QueuedConnection);
        }

        for (int mi = 0; mi < missIdx.size(); ++mi) {
            MainWindow *const window = guard.data();
            if (!window || gen != window->m_sortGeneration) {
                return;
            }
            const int i = missIdx.at(mi);
            const QString &path = paths.at(i);
            if (diskMeta) {
                const QFileInfo fi(path);
                mtimes.insert(path, fi.lastModified().toMSecsSinceEpoch());
                fsizes.insert(path, fi.size());
            } else if (!sizes.contains(path)) {
                sizes.insert(path, ImageLoader::probeSize(path));
            }
            const qint64 now = clock.elapsed();
            if (now - lastUi >= 250 || mi + 1 == missIdx.size()) {
                lastUi = now;
                const int done = cached + mi + 1;
                const bool meta = diskMeta;
                QMetaObject::invokeMethod(guard.data(), [guard, gen, done, total, meta]() {
                    MainWindow *const host = guard.data();
                    if (!host || gen != host->m_sortGeneration) {
                        return;
                    }
                    host->setExpandProgress(
                        done, total,
                        meta ? MainWindow::tr("Reading file info… %1/%2").arg(done).arg(total)
                             : MainWindow::tr("Measuring images… %1/%2").arg(done).arg(total));
                }, Qt::QueuedConnection);
            }
        }

        const QVector<int> order = MainWindow::computeSortOrderIndices(
            mode, paths, sizes, mtimes, fsizes);

        QStringList newFiles;
        QVector<SessionImageId> newIds;
        newFiles.reserve(order.size());
        newIds.reserve(order.size());
        for (int i : order) {
            newFiles.append(paths.at(i));
            newIds.append(ids.at(i));
        }

        QMetaObject::invokeMethod(guard.data(), [guard, gen, newFiles, newIds, onDone]() {
            MainWindow *const window = guard.data();
            if (!window || gen != window->m_sortGeneration) {
                return;
            }
            window->applySortedSessionOrder(newFiles, newIds, onDone);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::setSortMode(SortMode mode)
{
    m_sortMode = mode;
    if (m_sortNameAct) {
        m_sortNameAct->setChecked(mode == SortMode::Name);
    }
    if (m_sortPathAct) {
        m_sortPathAct->setChecked(mode == SortMode::Path);
    }
    if (m_sortAspectAct) {
        m_sortAspectAct->setChecked(mode == SortMode::AspectRatio);
    }
    if (m_sortShuffleAct) {
        m_sortShuffleAct->setChecked(mode == SortMode::Shuffle);
    }
    if (m_sortMTimeAct) {
        m_sortMTimeAct->setChecked(mode == SortMode::MTime);
    }
    if (m_sortFileSizeAct) {
        m_sortFileSizeAct->setChecked(mode == SortMode::FileSize);
    }
    if (m_sortWidthAct) {
        m_sortWidthAct->setChecked(mode == SortMode::Width);
    }
    if (m_sortHeightAct) {
        m_sortHeightAct->setChecked(mode == SortMode::Height);
    }
    if (m_sortPixelCountAct) {
        m_sortPixelCountAct->setChecked(mode == SortMode::PixelCount);
    }

    if (m_session.paths().isEmpty()) {
        return;
    }

    const SessionImageId currentId = currentSessionId();
    const QString current = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                                ? m_session.paths().at(m_currentIndex)
                                : QString();

    auto applyUi = [this, currentId, current]() {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        if (isWorkspaceMode()) {
            m_thumbnailBar->setMultiSelectEnabled(true);
            syncThumbnailWorkspaceSelection();
        }

        // Prefer SessionImageId so duplicate paths keep the focused row after sort.
        int newIndex = 0;
        if (currentId != kInvalidSessionImageId) {
            newIndex = indexOfSessionId(currentId);
        }
        if (newIndex < 0 && !current.isEmpty()) {
            newIndex = m_session.paths().indexOf(current);
        }
        if (newIndex < 0) {
            newIndex = 0;
        }
        m_currentIndex = -1; // force reload of Image mode cursor
        setCurrentIndex(newIndex);

        if (isGalleryMode()) {
            const LayoutMode layout = m_imageView
                ? m_imageView->hostLayout().currentMode()
                : LayoutMode::Masonry;
            populateGalleryCanvas();
            if (m_imageView) {
                m_imageView->enterGallery(layout);
                if (currentId != kInvalidSessionImageId
                    && m_imageView->findItemBySessionId(currentId)) {
                    m_imageView->focusSessionId(currentId);
                } else if (!current.isEmpty()) {
                    const SessionImageId sid = sessionIdAt(m_currentIndex);
                    if (sid != kInvalidSessionImageId
                        && m_imageView->findItemBySessionId(sid)) {
                        m_imageView->focusSessionId(sid);
                    } else {
                        m_imageView->focusSessionPath(current);
                    }
                }
            }
        } else if (isWorkspaceMode() && m_imageView) {
            m_imageView->reorderItemsByPaths(m_session.paths(), m_session.ids());
        }

        applyThumbnailVisibility();
    };

    if (sortModeNeedsImageProbe()) {
        sortFileListWithProbesInBackground(applyUi);
    } else {
        sortFileListSync();
        applyUi();
    }
}

void MainWindow::sortByName()
{
    setSortMode(SortMode::Name);
}

void MainWindow::sortByPath()
{
    setSortMode(SortMode::Path);
}

void MainWindow::sortByMTime()
{
    setSortMode(SortMode::MTime);
}

void MainWindow::sortByFileSize()
{
    setSortMode(SortMode::FileSize);
}

void MainWindow::sortByWidth()
{
    setSortMode(SortMode::Width);
}

void MainWindow::sortByHeight()
{
    setSortMode(SortMode::Height);
}

void MainWindow::sortByPixelCount()
{
    setSortMode(SortMode::PixelCount);
}

void MainWindow::sortByAspectRatio()
{
    setSortMode(SortMode::AspectRatio);
}

void MainWindow::sortByShuffle()
{
    // Always re-apply: Shuffle is intentional non-deterministic.
    setSortMode(SortMode::Shuffle);
}

void MainWindow::applyThumbnailVisibility()
{
    // Gallery and Workspace use independent preferred flags (updateThumbnailBarForMode).
    // Image mode keeps the multi-file auto rule plus CLI force overrides.
    if (m_imageView && (m_imageView->isGalleryMode() || m_imageView->isWorkspaceMode())) {
        updateThumbnailBarForMode();
        return;
    }
    bool show = m_session.paths().size() > 1;
    if (m_forceNoThumbnails) {
        show = false;
    } else if (m_forceThumbnails) {
        show = !m_session.paths().isEmpty();
    }
    const bool vis = show && !isFullScreen();
    if (m_thumbnailDock) {
        m_thumbnailDock->setVisible(vis);
    } else if (m_thumbnailBar) {
        m_thumbnailBar->setVisible(vis);
    }
    m_toggleThumbnailBarAct->setChecked(show);
    if (!isFullScreen()) {
        m_thumbnailBarVisibleBeforeFullscreen = show;
    }
}

void MainWindow::loadFiles(const QStringList &paths, int startAt)
{
    stopSlideshow();
    SessionOpen::beginReplace(m_imageView, m_thumbnailBar);

    if (pathsNeedBackgroundExpand(paths)) {
        expandPathsInBackground(paths, /*append=*/false, startAt);
        return;
    }
    // Sync path: invalidate any in-flight archive expand from a previous Open.
    ++m_expandGeneration;
    setExpandProgressBusy(false);

    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
        // AUDIT M26: explicit feedback when Open finds nothing usable
        // Restore filmstrip for the still-current session (we cleared above).
        if (m_thumbnailBar) {
            m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        }
        if (statusBar()) {
            statusBar()->showMessage(tr("No readable images found."), 5000);
        }
        return;
    }
    applyExpandedLoad(images, startAt);
}

void MainWindow::applyExpandedLoad(const QStringList &images, int startAt)
{
    m_session.setPaths(images); // also clears fat appearance (new ids)
    // Sparse ItemWorld tables are not owned by SessionDocument — clear them so
    // hasDurableAppearance cannot see prior-session crops after Open/Replace.
    if (m_imageView) {
        m_imageView->itemWorld().clearAppearance();
    }
    m_session.validateUniqueIds("loadFiles");
    if (sortModeNeedsImageProbe()) {
        sortFileListWithProbesInBackground([this, startAt]() {
            finishApplyExpandedLoad(startAt);
        });
        return;
    }
    sortFileListSync();
    finishApplyExpandedLoad(startAt);
}

void MainWindow::finishApplyExpandedLoad(int startAt)
{
    TtfpTrace::begin("finishApplyExpandedLoad");
    m_currentIndex = -1;

    const bool sizesWarm = SessionOpen::prepareExpandedSession(
        m_imageView, m_session.paths(), m_session.ids(),
        /*clearLiveWorkspace=*/isWorkspaceMode());

    m_workspaceReturnActive = false;
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }

    int idx = startAt;
    if (idx < 0 || idx >= m_session.paths().size()) {
        idx = 0;
    }

    // Filmstrip rebuild is O(n) list-widget work; on a warm multi-image open,
    // paint Gallery first and install the strip on the next event-loop turn.
    const auto installFilmstrip = [this]() {
        if (!m_thumbnailBar) {
            return;
        }
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        applyThumbnailVisibility();
    };

    TtfpTrace::mark(sizesWarm ? "sizes_warm" : "sizes_cold");
    if (m_session.paths().size() > 1) {
        if (!sizesWarm) {
            setExpandProgress(
                0, m_session.paths().size(),
                tr("Opening %n image(s)…", "", m_session.paths().size()));
        }
        // Always pack Gallery before filmstrip. Size probes are sequential and
        // must finish before filmstrip soft / tile pyramids compete on workers.
        enterGalleryMode(initialGalleryLayoutForOpen());
        TtfpTrace::mark("after_enterGalleryMode");
        setCurrentIndex(idx, /*ensureGalleryVisible=*/true);
        TtfpTrace::mark("after_setCurrentIndex");
        if (sizesWarm) {
            QTimer::singleShot(0, this, installFilmstrip);
            ThumtooCache::preparePaths(m_session.paths());
            ThumtooCache::warmUris(m_session.paths());
            TtfpTrace::mark("after_preparePaths");
        } else {
            // Cold: serial scheduleProbe (FIFO). preparePaths would flood parallel
            // ProbeSize and race EnsureTiles (thumtoo prefers tiles over sizes).
            const bool resolving =
                m_imageView && m_imageView->hostGallerySizeResolve().active();
            if (resolving && m_thumbnailBar) {
                m_thumbnailBar->setVisibleLoadsSuspended(true);
            }
            if (resolving && m_imageView) {
                connect(m_imageView, &ImageView::gallerySizeResolveFinished, this,
                        [this, installFilmstrip]() {
                            if (m_thumbnailBar) {
                                m_thumbnailBar->setVisibleLoadsSuspended(false);
                            }
                            installFilmstrip();
                            ThumtooCache::preparePaths(m_session.paths());
                            ThumtooCache::warmUris(m_session.paths());
                        },
                        static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));
            } else {
                // FreeForm / no size-gate: still serial probes, but allow strip.
                QTimer::singleShot(0, this, installFilmstrip);
                ThumtooCache::preparePaths(m_session.paths());
                ThumtooCache::warmUris(m_session.paths());
            }
            TtfpTrace::mark("sizes_cold_serial_probes");
        }
    } else {
        installFilmstrip();
        ThumtooCache::preparePaths(m_session.paths());
        ThumtooCache::warmUris(m_session.paths());
        if (m_imageView && !isImageMode()) {
            m_imageView->setViewMode(ImageView::ViewMode::Image);
        }
        if (m_thumbnailBar) {
            m_thumbnailBar->setMultiSelectEnabled(false);
        }
        setCurrentIndex(idx);
    }
    updateNavigationActions();
    updateWorkspaceActionVisibility();
    // Keep centre HUD while Gallery size probes still run.
    setExpandProgressBusy(false);
    if (statusBar() && statusBar()->currentMessage().startsWith(tr("Opening "))
        && !(m_imageView && m_imageView->hostGallerySizeResolve().active())) {
        statusBar()->clearMessage();
    }
    rememberSessionHistory(m_session.paths());
    TtfpTrace::mark("finishApplyExpandedLoad_return");
    // First pixels often arrive async; if still none, leave session active for
    // installDisplayPixels to close the report.
}

void MainWindow::newSession()
{
    stopSlideshow();
    m_session.clear();
    m_currentIndex = -1;
    m_galleryReturnActive = false;
    m_workspaceReturnActive = false;
    if (m_imageView) {
        // Drop all canvas objects and classic path so Image mode does not
        // reload the previous file after the mode switch.
        m_imageView->clearWorkspace();
        m_imageView->clearWorkspaceBackground();
        if (!m_imageView->isImageMode()) {
            m_imageView->setViewMode(ImageView::ViewMode::Image);
        }
        m_imageView->prepareImageModeCanvas();
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
        m_thumbnailBar->setFiles({});
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    applyThumbnailVisibility();
    updateNavigationActions();
    updateWorkspaceActionVisibility();
    if (m_metadataPanel) {
        m_metadataPath.clear();
        m_metadataPanel->clear();
    }
    updateStatus();
    if (statusBar()) {
        statusBar()->showMessage(tr("New session."), 2000);
    }
}

void MainWindow::appendFiles(const QStringList &paths)
{
    if (pathsNeedBackgroundExpand(paths)) {
        expandPathsInBackground(paths, /*append=*/true);
        return;
    }
    ++m_expandGeneration;
    setExpandProgressBusy(false);

    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("No readable images to add."), 5000);
        }
        return;
    }
    applyExpandedAppend(images);
}

void MainWindow::finishExpandedAppendChrome(SessionImageId currentId, const QString &currentPath,
                                            const QStringList &workspacePaths,
                                            const QVector<SessionImageId> &workspaceIds)
{
    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
        if (m_thumbnailBar->selectedIndices().isEmpty()
            && (!workspaceIds.isEmpty() || !workspacePaths.isEmpty())) {
            // Prefer SessionImageId so duplicate paths restore the correct rows.
            QList<int> indices;
            QSet<int> seen;
            const int n = qMax(workspaceIds.size(), workspacePaths.size());
            for (int i = 0; i < n; ++i) {
                int idx = -1;
                if (i < workspaceIds.size()
                    && workspaceIds.at(i) != kInvalidSessionImageId) {
                    idx = indexOfSessionId(workspaceIds.at(i));
                }
                if (idx < 0 && i < workspacePaths.size()
                    && !workspacePaths.at(i).isEmpty()) {
                    idx = m_session.paths().indexOf(workspacePaths.at(i));
                }
                if (idx >= 0 && !seen.contains(idx)) {
                    indices.append(idx);
                    seen.insert(idx);
                }
            }
            m_thumbnailBar->setSelectedIndices(indices);
        }
    }
    applyThumbnailVisibility();
    updateWorkspaceActionVisibility();

    // Prefer SessionImageId so duplicate paths keep the focused row after append/sort.
    int newIndex = 0;
    if (currentId != kInvalidSessionImageId) {
        newIndex = indexOfSessionId(currentId);
    }
    if (newIndex < 0 && !currentPath.isEmpty()) {
        newIndex = m_session.paths().indexOf(currentPath);
    }
    if (newIndex < 0) {
        newIndex = 0;
    }

    if (isWorkspaceMode()) {
        m_currentIndex = newIndex;
        if (m_metadataPanel) {
            m_metadataPath.clear();
        }
        updateStatus();
        updateNavigationActions();
        ThumtooCache::preparePaths(m_session.paths());
        ThumtooCache::warmUris(m_session.paths());
    } else if (isGalleryMode()) {
        SessionOpen::warmProcessMemos(m_session.paths());
        // Size-resolve HUD first; preparePaths after so cache fill does not skip it.
        populateGalleryCanvas();
        m_currentIndex = -1;
        setCurrentIndex(newIndex, /*ensureGalleryVisible=*/true);
        updateNavigationActions();
        ThumtooCache::preparePaths(m_session.paths());
        ThumtooCache::warmUris(m_session.paths());
    } else if (m_session.paths().size() > 1) {
        // Multi-image after append in Image mode — Gallery + size-first like Open.
        SessionOpen::warmProcessMemos(m_session.paths());
        const bool sizesWarm = SessionOpen::allSizesInProcessMemo(m_session.paths());
        if (!sizesWarm) {
            setExpandProgress(
                0, m_session.paths().size(),
                tr("Opening %n image(s)…", "", m_session.paths().size()));
        }
        enterGalleryMode(initialGalleryLayoutForOpen());
        setCurrentIndex(newIndex, /*ensureGalleryVisible=*/true);
        updateNavigationActions();
        ThumtooCache::preparePaths(m_session.paths());
        ThumtooCache::warmUris(m_session.paths());
    } else {
        ThumtooCache::preparePaths(m_session.paths());
        ThumtooCache::warmUris(m_session.paths());
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
        updateNavigationActions();
    }
    setExpandProgressBusy(false);
}

void MainWindow::applyExpandedAppend(const QStringList &images)
{
    const SessionImageId currentId = currentSessionId();
    const QString currentPath = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                                    ? m_session.paths().at(m_currentIndex)
                                    : QString();

    // Deduplicate while preserving order of existing entries
    QSet<QString> seen(m_session.paths().begin(), m_session.paths().end());
    int added = 0;
    for (const QString &path : images) {
        if (!seen.contains(path)) {
            m_session.append(path);
            seen.insert(path);
            ++added;
        }
    }
    if (added == 0) {
        return;
    }

    // Workspace selection restore after setFiles: prefer SessionImageId (duplicates).
    const QStringList workspacePaths =
        isWorkspaceMode() && m_imageView ? m_imageView->itemPaths() : QStringList();
    const QVector<SessionImageId> workspaceIds =
        isWorkspaceMode() && m_imageView ? m_imageView->itemSessionIds()
                                         : QVector<SessionImageId>();

    auto finish = [this, currentId, currentPath, workspacePaths, workspaceIds]() {
        finishExpandedAppendChrome(currentId, currentPath, workspacePaths, workspaceIds);
    };

    if (sortModeNeedsImageProbe()) {
        sortFileListWithProbesInBackground(finish);
    } else {
        sortFileListSync();
        finish();
    }
}




SessionImageId MainWindow::allocSessionId()
{
    return m_session.allocId();
}

SessionImageId MainWindow::sessionIdAt(int index) const
{
    return m_session.idAt(index);
}

int MainWindow::indexOfSessionId(SessionImageId id) const
{
    return m_session.indexOfId(id);
}

SessionImageId MainWindow::currentSessionId() const
{
    return sessionIdAt(m_currentIndex);
}

bool MainWindow::refreshSameCurrentIndex(bool ensureGalleryVisible)
{
    // Still refresh Image canvas after a mode switch that cleared the live
    // item, or if classicPath was left pointing at a different file.
    if (isImageMode() && m_imageView) {
        const QString path = m_session.paths().at(m_currentIndex);
        if (m_imageView->itemCount() == 0
            || m_imageView->hostImage().classicPath() != path) {
            m_imageView->hostDisplayPipeline().loadImage(path);
        }
        return true;
    }
    if (isGalleryMode() && m_imageView && ensureGalleryVisible) {
        // Filmstrip re-click of the current row: still select the gallery tile.
        const SessionImageId sid = sessionIdAt(m_currentIndex);
        if (sid != kInvalidSessionImageId
            && m_imageView->findItemBySessionId(sid)) {
            m_imageView->focusSessionId(sid);
        } else {
            m_imageView->focusSessionPath(m_session.paths().at(m_currentIndex));
        }
        return true;
    }
    return true;
}

void MainWindow::publishSessionCursorForIndex(int index)
{
    if (!m_imageView) {
        return;
    }
    // HUD identity pulse only in Image mode (Gallery pulse forced a full
    // viewport repaint of every tile on each click).
    const bool pulse = !m_slideshowAdvancing && isImageMode();
    m_imageView->hostSlideshow().setSessionPosition(index, m_session.paths().size(), pulse);
    m_imageView->setCurrentSessionId(currentSessionId());
}

void MainWindow::applyCurrentIndexCanvasChange(const QString &path, bool ensureGalleryVisible)
{
    // DOMAIN: only Image mode replaces the single-image canvas.
    // Gallery/Workspace keep multi-object canvas; update session cursor only.
    // Gallery: do not exclusive-select (Ctrl+click multi-select is owned by the view).
    if (isImageMode()) {
        // Slideshow pure-phase owns the viewport (m_ssFrom/To + atlas). loadImage
        // / PreferCache here raced the 16ms clock and caused soft→HQ frame drops
        // (log: preferCacheClimb after phase-from already at 2048).
        if (isSlideshowSession()) {
            if (m_imageView) {
                // User ←/→: nav-hot until quiet. Auto-advance is not key-repeat.
                m_imageView->hostSlideshow().setSlideshowNavHot(!m_slideshowAdvancing);
            }
            // User ←/→: onSlideshowUserNavigated already setSlideshowPhase (soft).
            // Settle: clear nav-hot and re-arm full phase quality once.
            if (!m_slideshowAdvancing) {
                if (!m_slideshowNavLoadTimer) {
                    m_slideshowNavLoadTimer = new QTimer(this);
                    m_slideshowNavLoadTimer->setSingleShot(true);
                    m_slideshowNavLoadTimer->setInterval(80);
                    connect(m_slideshowNavLoadTimer, &QTimer::timeout, this, [this]() {
                        if (!m_imageView || !isSlideshowSession()) {
                            return;
                        }
                        m_imageView->hostSlideshow().setSlideshowNavHot(false);
                        if (m_currentIndex < 0
                            || m_currentIndex >= m_session.paths().size()) {
                            return;
                        }
                        // Re-arm dwell quality (atlas / phase upgrade / blur)
                        // for the settled path only — not every keystroke.
                        m_imageView->hostSlideshow().setSlideshowPhase(
                            m_session.paths().at(m_currentIndex), QString(), -1.0);
                    });
                }
                m_slideshowNavLoadTimer->start();
            }
            return;
        }
        // Key-repeat: soft install every step; PreferCache only after quiet settle.
        m_imageView->hostSlideshow().setSlideshowNavHot(true);
        m_imageView->hostDisplayPipeline().loadImage(path);
        if (!m_slideshowNavLoadTimer) {
            m_slideshowNavLoadTimer = new QTimer(this);
            m_slideshowNavLoadTimer->setSingleShot(true);
            // 80ms: single taps still climb quickly; hold settles after burst.
            m_slideshowNavLoadTimer->setInterval(80);
            connect(m_slideshowNavLoadTimer, &QTimer::timeout, this, [this]() {
                if (!m_imageView || !isImageMode() || isSlideshowSession()) {
                    return;
                }
                if (m_currentIndex < 0
                    || m_currentIndex >= m_session.paths().size()) {
                    return;
                }
                m_imageView->hostSlideshow().setSlideshowNavHot(false);
                // Full load + PreferCache climb for the settled index only.
                m_imageView->hostDisplayPipeline().loadImage(m_session.paths().at(m_currentIndex));
                // ±1 neighbors: overview tiles into global path RAM (1212 retain).
                QStringList nbr;
                if (m_currentIndex > 0) {
                    nbr << m_session.paths().at(m_currentIndex - 1);
                }
                if (m_currentIndex + 1 < m_session.paths().size()) {
                    nbr << m_session.paths().at(m_currentIndex + 1);
                }
                m_imageView->hostTileNeighborPrefetch().prefetchPaths(nbr, 4);
            });
        }
        m_slideshowNavLoadTimer->start();
    } else if (isGalleryMode() && m_imageView) {
        // Filmstrip / keyboard nav (ensureGalleryVisible): select the tile and
        // scroll it into view. Gallery view clicks pass false — selection was
        // already applied there; do not clear Ctrl/Shift multi-select.
        if (ensureGalleryVisible) {
            const SessionImageId sid = sessionIdAt(m_currentIndex);
            if (sid != kInvalidSessionImageId
                && m_imageView->findItemBySessionId(sid)) {
                m_imageView->focusSessionId(sid);
            } else {
                m_imageView->focusSessionPath(path);
            }
        }
    } else if (m_imageView) {
        const SessionImageId sid = sessionIdAt(m_currentIndex);
        if (sid != kInvalidSessionImageId
            && m_imageView->findItemBySessionId(sid)) {
            m_imageView->focusSessionId(sid);
        } else {
            m_imageView->focusSessionPath(path);
        }
    }
}

void MainWindow::finishCurrentIndexChromeUpdate()
{
    m_thumbnailBar->setCurrentIndex(m_currentIndex);
    if (m_metadataPanel) {
        m_metadataPath.clear();
    }
    // During rapid Image-mode ←/→ (nav hot), skip title/location bar churn —
    // settle timer will load again and chrome catches up then.
    const bool navHot = m_imageView && m_imageView->hostSlideshow().hud().isNavHot()
                        && isImageMode() && !m_slideshowAdvancing;
    if (!navHot) {
        updateWindowTitle();
        syncLocationBarText();
    }
    // Image mode ←/→: do NOT call full updateStatus (metadata/adjustments/
    // pending-count/statusText rebuild). That was re-entered via statusChanged
    // and dominated the GUI on every key. Light path only.
    if (isImageMode()) {
        updateNavigationActions();
        if (m_imageView && m_statusLabel) {
            m_statusLabel->setText(m_imageView->statusText());
        }
        if (m_imageView) {
            m_imageView->hostSlideshow().setSessionPosition(m_currentIndex, m_session.paths().size(),
                                            !m_slideshowAdvancing && !navHot);
            m_imageView->setCurrentSessionId(currentSessionId());
        }
        return;
    }
    if (isGalleryMode()) {
        updateNavigationActions();
        m_statusLabel->setText(m_imageView ? m_imageView->statusText() : QString());
    } else {
        updateStatus();
        updateNavigationActions();
    }
}

void MainWindow::setCurrentIndex(int index, bool ensureGalleryVisible)
{
    if (m_session.paths().isEmpty() || index < 0 || index >= m_session.paths().size()) {
        return;
    }
    if (index == m_currentIndex) {
        refreshSameCurrentIndex(ensureGalleryVisible);
        return;
    }
    if (m_imageView && m_imageView->hostCrop().active()) {
        m_imageView->hostCrop().cancelCrop();
    }
    // Paused slideshow: clear transition overlay so the newly loaded image is
    // visible (hold/live layers otherwise mask LoadReplace).
    if (m_slideshowPaused && m_imageView) {
        m_imageView->hostSlideshow().cancelSlideshowTransition();
        m_slideshowPendingToIndex = -1;
        m_slideshowPreloadToIdx = -1;
    }

    m_currentIndex = index;
    const QString path = m_session.paths().at(m_currentIndex);

    // Publish session cursor before decode so Image-mode items bind the correct
    // sessionIndex (crop/flip sync to the matching Workspace slot).
    // Slideshow auto-advance must not pulse filename/index (only user nav or pinned HUD).
    publishSessionCursorForIndex(m_currentIndex);
    applyCurrentIndexCanvasChange(path, ensureGalleryVisible);
    finishCurrentIndexChromeUpdate();
}



void MainWindow::removeSessionIndices(const QList<int> &indices)
{
    if (indices.isEmpty() || m_session.paths().isEmpty()) {
        return;
    }

    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    QList<SessionEntrySnapshot> entries;
    for (int idx : sorted) {
        if (idx < 0 || idx >= m_session.paths().size()) {
            continue;
        }
        SessionEntrySnapshot snap;
        snap.index = idx;
        snap.path = m_session.pathAt(idx);
        snap.id = m_session.idAt(idx);
        // Capture appearance before redo removes canvas object / store entry.
        if (m_imageView && snap.id != kInvalidSessionImageId
            && m_imageView->hasSessionAppearance(snap.id)) {
            snap.appearance = m_imageView->sessionAppearanceValue(snap.id);
            snap.hasAppearance = true;
            snap.appearance.sessionId = snap.id;
            if (snap.appearance.path.isEmpty()) {
                snap.appearance.path = snap.path;
            }
        }
        entries.append(snap);
    }
    if (entries.isEmpty()) {
        return;
    }

    if (m_imageView && m_imageView->hostUndoStack() && !m_sessionUndoGuard) {
        // push() calls redo() → applySessionRemoveIndices
        m_imageView->hostUndoStack()->push(new SessionRemoveCommand(this, entries));
        return;
    }
    applySessionRemoveIndices(sorted);
}

void MainWindow::removeSessionIndicesFromModel(const QList<int> &sorted)
{
    // Remove highest indices first so remaining indices stay valid
    if (m_imageView) {
        m_imageView->setPreserveUndoOnDestroy(true);
        // Thumb-strip setFiles can resize the splitter → scrollbar range rebuild.
        // Snapshot once; each remove pins sceneRect; reassert after the churn.
        if (isGalleryMode()) {
            m_imageView->hostGallery().snapshotViewport();
            m_imageView->hostGallery().setRelayoutSuppressed(true);
        }
    }
    for (int i = sorted.size() - 1; i >= 0; --i) {
        const int idx = sorted.at(i);
        if (idx < 0 || idx >= m_session.paths().size()) {
            continue;
        }
        const SessionImageId sid = m_session.idAt(idx);
        m_session.removeAt(idx);
        // Always drop id-keyed appearance (fat + sparse). Canvas tiles only exist
        // in Workspace/Gallery; Image mode still must not leave orphaned rows.
        if (m_imageView && sid != kInvalidSessionImageId) {
            if (isWorkspaceMode() || isGalleryMode()) {
                m_imageView->removeWorkspaceSessionId(sid);
            } else {
                m_imageView->itemWorld().removeAppearance(sid);
            }
        }
    }
    if (m_imageView) {
        m_imageView->setPreserveUndoOnDestroy(false);
    }
}

void MainWindow::refreshSessionUiAfterRemove()
{
    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
    }
    applyThumbnailVisibility();
}

void MainWindow::selectIndexAfterSessionRemove(SessionImageId currentId, const QString &currentPath,
                                               const QList<int> &sorted)
{
    if (m_session.paths().isEmpty()) {
        m_currentIndex = -1;
        m_imageView->clearWorkspace();
        if (m_metadataPanel) {
            m_metadataPath.clear();
            m_metadataPanel->clear();
        }
        updateWindowTitle();
        updateStatus();
        updateNavigationActions();
        if (m_imageView) {
            m_imageView->hostGallery().setRelayoutSuppressed(false);
        }
        return;
    }

    // Prefer SessionImageId when the focused row was not removed.
    int newIndex = -1;
    if (currentId != kInvalidSessionImageId) {
        newIndex = indexOfSessionId(currentId);
    }
    if (newIndex < 0 && !currentPath.isEmpty()) {
        newIndex = m_session.paths().indexOf(currentPath);
    }
    if (newIndex < 0) {
        newIndex = ViewTransform::clampIndex(sorted.first(), m_session.paths().size());
    }

    if (isWorkspaceMode()) {
        m_currentIndex = newIndex;
        if (m_metadataPanel) {
            m_metadataPath.clear();
        }
        m_thumbnailBar->setCurrentIndex(newIndex);
        updateStatus();
        updateNavigationActions();
    } else if (isGalleryMode()) {
        m_currentIndex = newIndex;
        m_thumbnailBar->setCurrentIndex(newIndex);
        if (m_metadataPanel) {
            m_metadataPath.clear();
        }
        updateWindowTitle();
        updateStatus();
        updateNavigationActions();
        // Canvas already updated via removeWorkspaceSessionId; no full repack.
    } else {
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
    }

    if (m_imageView && isGalleryMode()) {
        // Immediate reassert after thumb setFiles / index updates.
        m_imageView->hostGallery().reassertViewport();
        // Release suppress and reassert again after splitter/layout events.
        QTimer::singleShot(0, m_imageView, [v = m_imageView]() {
            if (!v) {
                return;
            }
            v->hostGallery().reassertViewport();
            v->hostGallery().setRelayoutSuppressed(false);
        });
    }
}

void MainWindow::applySessionRemoveIndices(const QList<int> &indices)
{
    if (indices.isEmpty() || m_session.paths().isEmpty()) {
        return;
    }

    stopSlideshow();

    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    const SessionImageId currentId = currentSessionId();
    const QString currentPath = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                                    ? m_session.paths().at(m_currentIndex)
                                    : QString();

    // Gallery suppress stays on until the end of selectIndexAfterSessionRemove
    // (and one more event-loop tick) so setCurrentIndex / status updates cannot repack.
    removeSessionIndicesFromModel(sorted);
    refreshSessionUiAfterRemove();
    selectIndexAfterSessionRemove(currentId, currentPath, sorted);
}


void MainWindow::restoreSessionEntries(const QList<SessionEntrySnapshot> &entries)
{
    if (entries.isEmpty()) {
        return;
    }
    // Insert lowest index first so positions match the pre-remove session order.
    QList<SessionEntrySnapshot> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const SessionEntrySnapshot &a, const SessionEntrySnapshot &b) {
                  return a.index < b.index;
              });

    m_sessionUndoGuard = true;
    for (const auto &e : sorted) {
        // Identity is SessionImageId — skip if already present (duplicate-safe).
        if (e.id != kInvalidSessionImageId && m_session.indexOfId(e.id) >= 0) {
            continue;
        }
        const int idx = ViewTransform::clampInsertIndex(e.index, m_session.size());
        m_session.insert(idx, e.path, e.id);
        if (m_imageView && e.hasAppearance && e.id != kInvalidSessionImageId) {
            m_imageView->setSessionAppearance(e.id, e.appearance);
        }
    }
    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
    }
    applyThumbnailVisibility();

    if (isGalleryMode() && m_imageView) {
        m_imageView->setWorkspacePaths(m_session.paths(), m_session.ids());
    } else if (isWorkspaceMode() && m_imageView) {
        // Re-add only the restored session rows; do not replace the whole canvas.
        for (const auto &e : sorted) {
            const int idx = m_session.indexOfId(e.id);
            m_imageView->addImageForSession(e.path, e.id, idx >= 0 ? idx : e.index);
        }
        markWorkspaceDirty();
    } else if (!m_session.paths().isEmpty()) {
        m_currentIndex = -1;
        setCurrentIndex(qMin(m_currentIndex < 0 ? 0 : m_currentIndex, m_session.paths().size() - 1));
    }
    updateWindowTitle();
    updateStatus();
    updateNavigationActions();
    m_sessionUndoGuard = false;
}

void MainWindow::removeSessionIds(const QVector<SessionImageId> &ids)
{
    if (ids.isEmpty() || m_session.isEmpty()) {
        return;
    }
    QList<int> indices;
    for (SessionImageId id : ids) {
        const int idx = indexOfSessionId(id);
        if (idx >= 0) {
            indices.append(idx);
        }
    }
    removeSessionIndices(indices);
}

void MainWindow::removeSessionPaths(const QStringList &paths)
{
    // Path-only fallback when list index is unknown. Prefer removeSessionIds /
    // removeSessionIndices. When the same path appears more than once in
    // @p paths, map to successive session occurrences (not always the first).
    if (paths.isEmpty() || m_session.isEmpty()) {
        return;
    }
    QList<int> indices;
    QHash<QString, int> pathOccurrence;
    const QStringList &sessionPaths = m_session.paths();
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        const int wantOcc = pathOccurrence.value(path, 0);
        pathOccurrence[path] = wantOcc + 1;
        int seen = 0;
        int found = -1;
        for (int i = 0; i < sessionPaths.size(); ++i) {
            if (sessionPaths.at(i) != path) {
                continue;
            }
            if (seen == wantOcc) {
                found = i;
                break;
            }
            ++seen;
        }
        if (found >= 0 && !indices.contains(found)) {
            indices.append(found);
        }
    }
    removeSessionIndices(indices);
}

void MainWindow::updateNavPrevNextSlideshowActions(bool hasFiles, bool hasMany)
{
    // Prev/Next are Image mode only. Slideshow may start from Gallery (enters
    // Image mode on start); still unavailable in Workspace.
    const bool imageNav = hasMany && m_imageView && m_imageView->isImageMode();
    // Single-image sessions may still run a (short) slideshow dwell / motion.
    const bool canSlideshow = hasFiles && m_imageView && !m_imageView->isWorkspaceMode();
    m_previousAct->setEnabled(imageNav);
    m_nextAct->setEnabled(imageNav);
    const QString imageNavReason = tr("Available in Image mode when the session has more than one image.");
    if (m_previousAct) {
        m_previousAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    if (m_nextAct) {
        m_nextAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    if (m_epubLayoutAct) {
        bool epub = false;
        if (hasFiles) {
            const int i = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                              ? m_currentIndex
                              : 0;
            const QString pth = m_session.paths().at(i);
            if (PagePath::isPageRef(pth)) {
                const PagePath::Ref r = PagePath::parse(pth);
                epub = r.valid && r.isEpub();
            } else {
                epub = PagePath::isEpubFile(pth) || PagePath::isEpubLayoutOnly(pth);
            }
        }
        m_epubLayoutAct->setEnabled(epub);
        m_epubLayoutAct->setProperty(
            "biltooDisabledHelp",
            tr("EPUB Layout is available when the current item is an EPUB book or page."));
    }
    if (m_pdfEmbeddedImagesAct) {
        bool pdf = false;
        if (hasFiles && ThumtooCache::isAvailable()) {
            const int i = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                              ? m_currentIndex
                              : 0;
            const QString pth = m_session.paths().at(i);
            if (PagePath::isPageRef(pth)) {
                const PagePath::Ref r = PagePath::parse(pth);
                pdf = r.valid && !r.isEpub() && PagePath::isPdfFile(r.pdfPath);
            } else if (PagePath::isPdfImageRef(pth) || PagePath::isPdfImagesCollection(pth)) {
                pdf = true;
            } else {
                pdf = PagePath::isPdfFile(pth);
            }
        }
        m_pdfEmbeddedImagesAct->setEnabled(pdf);
        m_pdfEmbeddedImagesAct->setProperty(
            "biltooDisabledHelp",
            tr("Available when the current item is a PDF (page or file) and thumtoo is present."));
    }
    if (m_firstAct) {
        m_firstAct->setEnabled(imageNav);
        m_firstAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    if (m_lastAct) {
        m_lastAct->setEnabled(imageNav);
        m_lastAct->setProperty("biltooDisabledHelp", imageNavReason);
    }
    m_slideshowAct->setEnabled(canSlideshow);
    if (m_slideshowAct) {
        if (canSlideshow) {
            m_slideshowAct->setStatusTip(tr("Space: pause/resume · Esc: leave slideshow and fullscreen"));
            m_slideshowAct->setProperty("biltooDisabledHelp", QString());
        } else if (m_imageView && m_imageView->isWorkspaceMode()) {
            const QString r = tr("Slideshow is not available in Workspace mode.");
            m_slideshowAct->setStatusTip(r);
            m_slideshowAct->setProperty("biltooDisabledHelp", r);
        } else {
            const QString r = tr("Open at least one image to use the slideshow.");
            m_slideshowAct->setStatusTip(r);
            m_slideshowAct->setProperty("biltooDisabledHelp", r);
        }
    }
    if (m_imageView) {
        m_imageView->setImageModeNavigationEnabled(imageNav);
    }
    // Stop only when a running slideshow becomes invalid (empty session or
    // Workspace). Idle stopSlideshow is a no-op for the timer, but still
    // emitted statusChanged via restoreImageFramingAfterSlideshow → re-entered
    // here forever (stack overflow / SEGV in QToolButton). Single-image
    // sessions may keep a short dwell/motion loop.
    // Do not use transient "no canvas items" mid LoadReplace as a stop signal.
    if (isSlideshowSession()
        && (m_session.paths().isEmpty()
            || (m_imageView && m_imageView->isWorkspaceMode()))) {
        stopSlideshow();
    }
}

void MainWindow::updateNavTransformCropActions(bool canTransform)
{
    // Rotate/flip: Image (current), Workspace (selection), Gallery (selection)
    const QString transformReason = tr(
        "Rotate and flip need a current Image, or a selection in Gallery or Workspace.");
    for (QAction *act : {m_rotateLeftAct, m_rotateRightAct, m_flipHAct, m_flipVAct}) {
        if (act) {
            act->setEnabled(canTransform);
            act->setProperty("biltooDisabledHelp", transformReason);
        }
    }
    if (m_resetContentAppearanceAct) {
        m_resetContentAppearanceAct->setEnabled(
            canTransform && m_imageView && m_imageView->targetHasContentAppearance());
    }
    // Crop: Image mode, or exactly one Gallery/Workspace selection.
    if (m_cropAct) {
        const bool canCrop = m_imageView && m_imageView->hasSingleCropTarget();
        m_cropAct->setEnabled(canCrop);
        m_cropAct->setProperty(
            "biltooDisabledHelp",
            tr("Crop needs Image mode, or exactly one selected tile in Gallery or Workspace."));
        if (!canCrop && m_imageView && m_imageView->hostCrop().active()) {
            m_imageView->hostCrop().cancelCrop();
        }
    }
    // Placement resets are Workspace-only (content transforms use Image menu).
    const bool canResetPlacement = canTransform && m_imageView
        && m_imageView->isWorkspaceMode();
    for (QAction *act : {m_resetScaleAct, m_resetRotationAct, m_resetShearAct}) {
        if (act) {
            act->setEnabled(canResetPlacement);
        }
    }
}

void MainWindow::updateNavZoomAndSelectionActions(bool hasFiles, bool hasItem)
{
    // Zoom: Image / Workspace with content; Image with files loading also OK
    const bool canZoom = hasItem
                         || (m_imageView && m_imageView->isImageMode() && hasFiles);
    const QString zoomReason = tr(
        "Zoom commands need content on the canvas (Image or Workspace), or an open session in Image mode.");
    for (QAction *act : {m_zoomInAct, m_zoomOutAct, m_zoom1to1Act, m_zoomFitAct, m_zoomFillAct,
                         m_zoomRegionAct}) {
        if (act) {
            act->setEnabled(canZoom);
            act->setProperty("biltooDisabledHelp", zoomReason);
        }
    }

    if (m_selectAllAct) {
        m_selectAllAct->setEnabled(hasFiles);
    }
    if (m_hideThumbLabelsAct) {
        m_hideThumbLabelsAct->setEnabled(m_thumbnailBar != nullptr);
    }
    if (m_toggleHudAct) {
        m_toggleHudAct->setEnabled(true);
    }
    if (m_openSelectionNewWindowAct) {
        const bool canOpenSel = !pathsFromUiSelection().isEmpty();
        m_openSelectionNewWindowAct->setEnabled(canOpenSel);
    }
}

void MainWindow::updateNavigationActions()
{
    const bool hasFiles = !m_session.paths().isEmpty();
    const bool hasMany = m_session.paths().size() > 1;
    const bool hasItem = m_imageView && m_imageView->itemCount() > 0;
    const bool canTransform = m_imageView && m_imageView->hasTransformTargets();

    updateNavPrevNextSlideshowActions(hasFiles, hasMany);
    updateNavTransformCropActions(canTransform);
    updateNavZoomAndSelectionActions(hasFiles, hasItem);
}



void MainWindow::onThumbnailActivated(int index)
{
    // Double-click / Enter on the filmstrip — same as Gallery tile open:
    // switch to Image mode on that session index (not Workspace membership).
    // setCurrentIndex alone only moves the session cursor and left the user
    // in Gallery/Workspace.
    if (index < 0 || index >= m_session.size()) {
        return;
    }
    openSessionIndexInImageMode(index);
    onSlideshowUserNavigated();
}

void MainWindow::syncLocationBarText()
{
    if (!m_locationEdit) {
        return;
    }
    QString text;
    if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        text = m_session.paths().at(m_currentIndex);
    } else if (!m_session.paths().isEmpty()) {
        text = m_session.paths().first();
    }
    // Avoid fighting the user while they are typing.
    if (m_locationEdit->hasFocus()) {
        return;
    }
    m_locationEdit->setText(text);
}

void MainWindow::setLocationBarPinned(bool pinned)
{
    m_locationBarPinned = pinned;
    if (m_showLocationBarAct && m_showLocationBarAct->isChecked() != pinned) {
        QSignalBlocker block(m_showLocationBarAct);
        m_showLocationBarAct->setChecked(pinned);
    }
    if (!m_locationBar) {
        return;
    }
    if (pinned) {
        syncLocationBarText();
        m_locationBar->setVisible(true);
    } else {
        // Explicit unpin via the menu: always hide and release focus.
        // (Previously we kept the bar if the line edit had focus, which made
        // the toggle appear broken and left Escape with nothing to cancel.)
        if (m_locationEdit) {
            m_locationEdit->clearFocus();
        }
        m_locationBar->setVisible(false);
        if (m_imageView) {
            m_imageView->setFocus(Qt::OtherFocusReason);
        }
    }
}

void MainWindow::openLocation()
{
    if (!m_locationBar || !m_locationEdit) {
        return;
    }
    syncLocationBarText();
    // If empty (no session), still show so the user can type a path.
    if (m_locationEdit->text().isEmpty()
        && m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        m_locationEdit->setText(m_session.paths().at(m_currentIndex));
    } else if (m_locationEdit->text().isEmpty() && !m_session.paths().isEmpty()) {
        m_locationEdit->setText(m_session.paths().first());
    }
    m_locationBar->setVisible(true);
    m_locationEdit->setFocus(Qt::ShortcutFocusReason);
    m_locationEdit->selectAll();
}

void MainWindow::cancelLocationBar()
{
    if (!m_locationBar || !m_locationEdit) {
        return;
    }
    m_locationEdit->clearFocus();
    if (!m_locationBarPinned) {
        m_locationBar->setVisible(false);
    } else {
        syncLocationBarText();
    }
    if (m_imageView) {
        m_imageView->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::commitLocationBar()
{
    if (!m_locationEdit) {
        return;
    }
    const QString trimmed = m_locationEdit->text().trimmed();
    if (trimmed.isEmpty()) {
        cancelLocationBar();
        return;
    }
    // Accept file:// URLs and plain paths; page/archive refs pass through.
    QString path = trimmed;
    if (path.startsWith(QLatin1String("file:"))) {
        const QUrl url(path);
        if (url.isLocalFile()) {
            const int pipe = path.indexOf(QLatin1String("//"), 7);
            const QString local = url.toLocalFile();
            if (pipe > 0) {
                path = local + path.mid(pipe);
            } else {
                path = local;
            }
        }
    }

    // If the user strips //page:N (or //epub:…//page:N) back to the bare
    // document path, open the full expanded session — not a single page.
    // Prefer starting at the page they were on when it belongs to the same file.
    int startAt = 0;
    if (!PagePath::isPageRef(path)) {
        const QFileInfo info(path);
        const QString abs = info.exists() ? info.absoluteFilePath() : path;
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            const PagePath::Ref cur = PagePath::parse(m_session.paths().at(m_currentIndex));
            if (cur.valid) {
                const QFileInfo curInfo(cur.pdfPath);
                const QString curAbs =
                    curInfo.exists() ? curInfo.absoluteFilePath() : cur.pdfPath;
                if (curAbs == abs || cur.pdfPath == path) {
                    startAt = cur.page - 1; // 0-based index into expanded pages
                }
            }
        }
    }

    loadFiles(QStringList{path}, startAt);
    if (!m_locationBarPinned && m_locationBar) {
        m_locationEdit->clearFocus();
        m_locationBar->setVisible(false);
    }
    if (m_imageView) {
        m_imageView->setFocus(Qt::OtherFocusReason);
    }
}


bool MainWindow::resolveEpubLayoutTarget(QString *epubFile, QString *layoutParams,
                                         int *keepPage) const
{
    if (m_session.paths().isEmpty()) {
        return false;
    }
    const int idx = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                        ? m_currentIndex
                        : 0;
    const QString curPath = m_session.paths().at(idx);

    if (PagePath::isPageRef(curPath)) {
        const PagePath::Ref ref = PagePath::parse(curPath);
        if (!ref.valid || !ref.isEpub()) {
            return false;
        }
        *epubFile = ref.pdfPath;
        *layoutParams = ref.epubLayoutParams;
        *keepPage = ref.page;
        return true;
    }
    if (PagePath::isEpubLayoutOnly(curPath)) {
        *epubFile = PagePath::documentFilePath(curPath);
        *layoutParams = PagePath::epubLayoutParamsOf(curPath);
        *keepPage = 1;
        return true;
    }
    if (PagePath::isEpubFile(curPath)) {
        *epubFile = curPath;
        layoutParams->clear();
        *keepPage = 1;
        return true;
    }
    return false;
}

void MainWindow::rewriteEpubSessionPaths(const QString &epubFile, const QString &newParams,
                                         int keepPage)
{
    QStringList paths = m_session.paths();
    const int idx = (m_currentIndex >= 0 && m_currentIndex < paths.size())
                        ? m_currentIndex
                        : 0;
    int newIndex = idx;
    const QFileInfo epubInfo(epubFile);
    const QString epubAbs =
        epubInfo.exists() ? epubInfo.absoluteFilePath() : epubFile;

    for (int i = 0; i < paths.size(); ++i) {
        if (!PagePath::isPageRef(paths.at(i))) {
            continue;
        }
        const PagePath::Ref r = PagePath::parse(paths.at(i));
        if (!r.valid || !r.isEpub()) {
            continue;
        }
        const QFileInfo fi(r.pdfPath);
        const QString abs = fi.exists() ? fi.absoluteFilePath() : r.pdfPath;
        if (abs != epubAbs && r.pdfPath != epubFile) {
            continue;
        }
        paths[i] = PagePath::makeEpubRef(r.pdfPath, r.page, newParams);
        if (r.page == keepPage) {
            newIndex = i;
        }
    }

    const int startAt = (newIndex >= 0 && newIndex < paths.size()) ? newIndex : 0;
    // Full reload so thumtoo picks up new layout URIs (reflow + cache keys).
    loadFiles(paths, startAt);
}

void MainWindow::showEpubLayoutDialog()
{
    QString epubFile;
    QString layoutParams;
    int keepPage = 1;
    if (!resolveEpubLayoutTarget(&epubFile, &layoutParams, &keepPage)) {
        if (statusBar()) {
            statusBar()->showMessage(tr("EPUB Layout applies to EPUB pages only."), 4000);
        }
        return;
    }

    EpubLayoutDialog dlg(this);
    dlg.setLayoutParams(layoutParams);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString newParams = dlg.layoutParams();
    if (newParams == layoutParams) {
        return;
    }

    // Rewrite every session page that belongs to this EPUB.
    rewriteEpubSessionPaths(epubFile, newParams, keepPage);
    if (statusBar()) {
        statusBar()->showMessage(tr("EPUB layout applied."), 3000);
    }
}

void MainWindow::openPdfAsEmbeddedImages()
{
    if (!ThumtooCache::isAvailable()) {
        if (statusBar()) {
            statusBar()->showMessage(
                tr("PDF embedded images require thumtoo."), 4000);
        }
        return;
    }
    if (m_session.paths().isEmpty()) {
        return;
    }
    const int idx = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                        ? m_currentIndex
                        : 0;
    const QString cur = m_session.paths().at(idx);
    QString doc;
    if (PagePath::isPageRef(cur)) {
        const PagePath::Ref r = PagePath::parse(cur);
        if (!r.valid || r.isEpub()) {
            if (statusBar()) {
                statusBar()->showMessage(tr("PDF Embedded Images applies to PDFs only."), 4000);
            }
            return;
        }
        doc = r.pdfPath;
    } else if (PagePath::isPdfImageRef(cur) || PagePath::isPdfImagesCollection(cur)) {
        doc = PagePath::documentFilePath(cur);
    } else if (PagePath::isPdfFile(cur)) {
        doc = cur;
    } else {
        if (statusBar()) {
            statusBar()->showMessage(tr("PDF Embedded Images applies to PDFs only."), 4000);
        }
        return;
    }
    if (doc.isEmpty()) {
        return;
    }
    const QString collection = PagePath::makePdfImagesCollection(doc);
    if (collection.isEmpty()) {
        return;
    }
    // Already a pure //pdfimages session for this file — re-expand to refresh.
    loadFiles(QStringList{collection});
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Opening embedded images from “%1”…").arg(QFileInfo(doc).fileName()),
            3000);
    }
}

void MainWindow::openFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Open Images"),
        QString(),
        imageFileDialogFilter());
    if (!files.isEmpty()) {
        loadFiles(files);
    }
}

void MainWindow::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Add Images"),
        QString(),
        imageFileDialogFilter());
    if (!files.isEmpty()) {
        appendFiles(files);
    }
}

void MainWindow::openDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"));
    if (!dir.isEmpty()) {
        loadFiles({dir});
    }
}


void MainWindow::reloadFromDisk()
{
    if (!m_imageView) {
        return;
    }
    // Gallery F5: re-decode + explicit pack (same as pressing a layout action).
    // Image mode: only the focused file. Workspace: tiles in place, no pack.
    const bool relayout = m_imageView->isGalleryMode();
    m_imageView->reloadFromDisk(relayout);
    updateStatus();
}

void MainWindow::hardReloadFromDisk()
{
    if (!m_imageView) {
        return;
    }
    const bool relayout = m_imageView->isGalleryMode();
    m_imageView->hardReloadFromDisk(relayout);
    updateStatus();
}

void MainWindow::goPrevious()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    int idx = m_currentIndex - 1;
    if (idx < 0) {
        idx = m_session.paths().size() - 1;
    }
    setCurrentIndex(idx);
    onSlideshowUserNavigated();
}

void MainWindow::goNext()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    int idx = m_currentIndex + 1;
    if (idx >= m_session.paths().size()) {
        idx = 0;
    }
    setCurrentIndex(idx);
    onSlideshowUserNavigated();
}

void MainWindow::goFirst()
{
    if (m_session.paths().isEmpty()) {
        return;
    }
    setCurrentIndex(0);
    onSlideshowUserNavigated();
}

void MainWindow::goLast()
{
    if (m_session.paths().isEmpty()) {
        return;
    }
    setCurrentIndex(m_session.paths().size() - 1);
    onSlideshowUserNavigated();
}

void MainWindow::clampSlideshowTransitionToInterval()
{
    if (!m_imageView) {
        return;
    }
    const int cap = m_slideshowIntervalMs;
    const int tr = m_imageView->hostSlideshow().settings().transitionDuration();
    if (cap >= 0 && tr > cap) {
        m_imageView->hostSlideshow().setSlideshowTransitionDurationMs(cap);
    }
}

void MainWindow::rearmSlideshowAfterIntervalChange(int oldInterval)
{
    if (!isSlideshowSession()) {
        return;
    }
    // Preserve normalized cycle progress under the new interval (running or
    // paused). Do not tear down phase buffers, atlases, or Ken Burns — speed
    // changes must be invisible except for timing.
    if (m_slideshowClockRunning && oldInterval != m_slideshowIntervalMs) {
        const int oldI = oldInterval > 0 ? oldInterval : 1;
        const int newI = m_slideshowIntervalMs > 0 ? m_slideshowIntervalMs : 1;
        remapSlideshowPhase(oldI, newI);
    }
    // Keep pending transition indices; only clear the cycle stamp so the next
    // tick can re-bind fade math under the new interval without a cut.
    m_slideshowTransitionCycle = -1;
    if (m_imageView) {
        // Interval-only when already active (no progress-clock restart).
        m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
        // Retarget motion duration; keep dwell atlas and phase images.
        m_imageView->hostSlideshow().reapplySlideshowFraming();
    }
    if (!m_slideshowPaused) {
        updateSlideshowFromClock();
    }
}

void MainWindow::setSlideshowIntervalMs(int ms)
{
    // 0 ms = as fast as the event loop allows; upper bound keeps UI usable.
    const int oldInterval = m_slideshowIntervalMs;
    m_slideshowIntervalMs = SlideshowClocks::clampStoredIntervalMs(ms); // 0…3600s
    // Transition duration is the full effect (out + in); may use the whole interval.
    clampSlideshowTransitionToInterval();
    rearmSlideshowAfterIntervalChange(oldInterval);
}



namespace {

/** Human-readable slideshow dwell for HUD / status line. */
QString formatSlideshowInterval(int ms)
{
    const auto L = SlideshowClocks::intervalLabelParts(ms);
    if (ms <= 0) {
        return QCoreApplication::translate("MainWindow", "0 ms (max speed)");
    }
    if (L.useMs) {
        return QCoreApplication::translate("MainWindow", "%1 ms").arg(L.wholeMs);
    }
    if (L.exactSec) {
        return QCoreApplication::translate("MainWindow", "%1 s").arg(L.wholeSec);
    }
    return QCoreApplication::translate("MainWindow", "%1 s").arg(L.sec, 0, 'f', 1);
}

} // namespace

void MainWindow::slideshowFaster()
{
    // mpv ]: higher playback speed → shorter dwell per slide
    const int next = SlideshowClocks::intervalFaster(m_slideshowIntervalMs);
    if (next == m_slideshowIntervalMs) {
        const QString msg = tr("Slideshow already at maximum speed (0 ms)");
        if (m_imageView) {
            m_imageView->flashHud(tr("Slideshow interval"), msg);
        }
        if (statusBar()) {
            statusBar()->showMessage(msg, 2000);
        }
        return;
    }
    setSlideshowIntervalMs(next);
    const QString detail = formatSlideshowInterval(m_slideshowIntervalMs);
    if (m_imageView) {
        m_imageView->flashHud(tr("Slideshow interval"), detail);
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Slideshow interval: %1").arg(detail), 2000);
    }
}

void MainWindow::slideshowSlower()
{
    // mpv [: lower playback speed → longer dwell per slide
    const int next = SlideshowClocks::intervalSlower(m_slideshowIntervalMs);
    if (next == m_slideshowIntervalMs) {
        const QString msg = tr("Slideshow already at maximum interval (60 s)");
        if (m_imageView) {
            m_imageView->flashHud(tr("Slideshow interval"), msg);
        }
        if (statusBar()) {
            statusBar()->showMessage(msg, 2000);
        }
        return;
    }
    setSlideshowIntervalMs(next);
    const QString detail = formatSlideshowInterval(m_slideshowIntervalMs);
    if (m_imageView) {
        m_imageView->flashHud(tr("Slideshow interval"), detail);
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Slideshow interval: %1").arg(detail), 2000);
    }
}

void MainWindow::showSlideshowCursor()
{
    if (m_slideshowCursorHidden) {
        QApplication::restoreOverrideCursor();
        m_slideshowCursorHidden = false;
    }
}

void MainWindow::hideSlideshowCursor()
{
    if (!isSlideshowSession()) {
        return;
    }
    if (!m_slideshowCursorHidden) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
        m_slideshowCursorHidden = true;
    }
}

void MainWindow::armSlideshowCursorHide()
{
    if (!m_cursorHideTimer || !isSlideshowSession()) {
        return;
    }
    // Show on activity, then hide after 1 s of inactivity (timer interval).
    showSlideshowCursor();
    m_cursorHideTimer->start();
}

bool MainWindow::isSlideshowSession() const
{
    return m_slideshowPaused || m_slideshowClockRunning;
}

void MainWindow::updateSlideshowActionUi()
{
    if (!m_slideshowAct) {
        return;
    }
    if (m_slideshowPaused) {
        m_slideshowAct->setChecked(true);
        m_slideshowAct->setText(tr("Resume &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"),
                                           QStyle::SP_MediaPlay));
        m_slideshowAct->setStatusTip(
            tr("Space: resume · Esc: leave slideshow and fullscreen"));
    } else if (m_slideshowClockRunning) {
        m_slideshowAct->setChecked(true);
        m_slideshowAct->setText(tr("Pause &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-pause"),
                                           QStyle::SP_MediaPause));
        m_slideshowAct->setStatusTip(
            tr("Space: pause · Esc: leave slideshow and fullscreen · ←/→ change slide"));
    } else {
        m_slideshowAct->setChecked(false);
        m_slideshowAct->setText(tr("Play &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"),
                                           QStyle::SP_MediaPlay));
        m_slideshowAct->setStatusTip(
            tr("Space: pause/resume · Esc: leave slideshow and fullscreen"));
    }
}

void MainWindow::onSlideshowUserNavigated()
{
    if (!isSlideshowSession() || m_slideshowAdvancing) {
        return;
    }
    if (!m_imageView) {
        return;
    }

    // Debounce neighbour preload — rapid ←/→ used to start a decode every
    // keystroke for next and prev (and each cancelled the previous job).
    const int nPaths = m_session.paths().size();
    if (nPaths > 1 && m_currentIndex >= 0) {
        const int idx = m_currentIndex;
        if (!m_slideshowPreloadTimer) {
            m_slideshowPreloadTimer = new QTimer(this);
            m_slideshowPreloadTimer->setSingleShot(true);
            m_slideshowPreloadTimer->setInterval(200);
            connect(m_slideshowPreloadTimer, &QTimer::timeout, this, [this]() {
                if (!m_imageView || m_session.paths().size() <= 1
                    || m_currentIndex < 0) {
                    return;
                }
                const int n = m_session.paths().size();
                const int i = m_currentIndex;
                m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at((i + 1) % n));
                m_imageView->hostSlideshow().preloadSlideshowImage(
                    m_session.paths().at((i - 1 + n) % n));
            });
        }
        Q_UNUSED(idx);
        m_slideshowPreloadTimer->start();
    }

    m_imageView->hostSlideshow().cancelSlideshowTransition();
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    m_slideshowTransitionCycle = -1;
    m_slideshowBaseIndex = ViewTransform::clampIndex(m_currentIndex, m_session.paths().size());
    m_slideshowPausedAccumMs = 0;

    // Always publish HUD position for this index. While playing, arm()/tick
    // also write the timeline; while paused the tick does not run, so without
    // this the clock stayed frozen on ←/→.
    const int n = m_session.paths().size();
    const int intervalMs = ViewTransform::atLeast1(m_slideshowIntervalMs);
    if (n > 0) {
        const qint64 totalMs = qint64(n) * qint64(intervalMs);
        const qint64 at = qint64(m_slideshowBaseIndex) * qint64(intervalMs);
        m_imageView->hostSlideshow().setSlideshowTimeline(at, totalMs);
    }

    if (!m_slideshowPaused) {
        armSlideshowAdvanceTimer();
        // Force pure phase to the navigated path so a mid-transition fade
        // cannot leave the previous slide on screen for a tick under ←/→.
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostSlideshow().setSlideshowPhase(m_session.paths().at(m_currentIndex),
                                           QString(), -1.0);
        }
    } else {
        // Clock is frozen while paused, so updateSlideshowFromClock will not
        // push a new pure phase. Drive the composite to the navigated slide
        // or the screen stays on the previous m_slideshow.phase().fromImage until unpause.
        m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostSlideshow().setSlideshowPhase(m_session.paths().at(m_currentIndex),
                                           QString(), -1.0);
        }
        m_imageView->hostSlideshow().setSlideshowMotionPaused(true);
        m_imageView->hostSlideshow().setSlideshowPausedHud(true);
    }
    updateSlideshowActionUi();
}

void MainWindow::startSlideshow()
{
    if (m_session.paths().isEmpty() || isWorkspaceMode()) {
        m_slideshowPaused = false;
        updateSlideshowActionUi();
        return;
    }
    // Shared preview cache: every session path gets a ≥512 frame scheduled now.
    ImageCache::warm(m_session.paths(), ImageCache::kPreviewEdge);
    if (m_thumbnailBar) {
        m_thumbnailBar->setVisibleLoadsSuspended(true);
    }
    // Gallery: open the focused session image in Image mode, then advance.
    if (isGalleryMode()) {
        const SessionImageId sid = currentSessionId();
        if (sid != kInvalidSessionImageId) {
            openSessionImageInImageMode(sid);
        } else {
            QString path;
            if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
                path = m_session.paths().at(m_currentIndex);
            } else if (!m_session.paths().isEmpty()) {
                path = m_session.paths().first();
            }
            if (path.isEmpty()) {
                m_slideshowPaused = false;
                updateSlideshowActionUi();
                return;
            }
            showPathInImageMode(path);
        }
    }
    m_slideshowOwnsFullscreen = false;
    if (m_slideshowFullscreen && !isFullScreen()) {
        showFullScreen();
        m_slideshowOwnsFullscreen = true;
    }
    m_slideshowPaused = false;
    // Mark the session active *before* chrome policy. updateScrollBarPolicyForMode
    // keys off isSlideshowSession(); calling it before the clock runs left Gallery
    // AsNeeded/AlwaysOn bars visible for the whole show (Gallery → Space).
    m_slideshowClockRunning = true;
    updateScrollBarPolicyForMode();
    updateSlideshowActionUi();
    qApp->installEventFilter(this);
    // Ensure free mouse moves reach the app filter during the show.
    setMouseTracking(true);
    if (m_imageView) {
        m_imageView->setMouseTracking(true);
        if (m_imageView->viewport()) {
            m_imageView->viewport()->setMouseTracking(true);
        }
    }
    armSlideshowCursorHide();
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowMotionPaused(false);
        m_imageView->hostSlideshow().setSlideshowPausedHud(false);
        m_imageView->hostSlideshow().setSlideshowProgress(true, m_slideshowIntervalMs);
        // Arm pure phase *before* framing/motion so the first paint is oriented
        // ContentXform sample — not an unoriented dwell underlay stand-in.
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostSlideshow().setSlideshowPhase(
                m_session.paths().at(m_currentIndex), QString(), -1.0);
        }
        // Frame + start dwell motion AFTER phase arm so prepareSlideshowMotionDwell
        // can reuse the oriented phase buffer.
        m_imageView->hostSlideshow().reapplySlideshowFraming();
        if (m_session.paths().size() > 1) {
            int n = (m_currentIndex + 1) % m_session.paths().size();
            if (n < 0) {
                n = 0;
            }
            m_imageView->hostSlideshow().preloadSlideshowImage(m_session.paths().at(n));
        }
        m_imageView->flashHud(tr("▶  Slideshow"),
                              formatSlideshowInterval(m_slideshowIntervalMs));
    }
    // Clock last — may immediately start a transition when pureMs==0.
    armSlideshowAdvanceTimer();
    // Re-assert after arm (and after any motion freeze) so Gallery→show never
    // leaves AsNeeded bars from the pre-session policy snapshot.
    updateScrollBarPolicyForMode();
}

void MainWindow::seekSlideshowFraction(qreal fraction)
{
    if (!isSlideshowSession() || m_session.paths().isEmpty()) {
        return;
    }
    fraction = ViewTransform::clamp01(fraction);
    const int n = m_session.paths().size();
    int intervalMs = m_slideshowIntervalMs;
    if (intervalMs <= 0) {
        intervalMs = 1;
    }
    // Unitless seek: position covers the whole session loop [0, n).
    m_slideshowBaseIndex = 0;
    m_slideshowPosition = fraction * qreal(n);
    m_slideshowPausedAccumMs = 0;
    m_slideshowTransitionCycle = -1;
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    if (!m_slideshowPaused) {
        m_slideshowClock.start();
    }
    const int idx = int(qFloor(m_slideshowPosition)) % n;
    const qreal phaseT = m_slideshowPosition - qFloor(m_slideshowPosition);
    const qint64 totalMs = qint64(n) * qint64(intervalMs);
    const qint64 elapsedMs = SlideshowClocks::timelineElapsedMs(
        m_slideshowPosition, intervalMs, totalMs);
    if (m_imageView) {
        m_imageView->hostSlideshow().cancelSlideshowTransition();
        m_imageView->hostSlideshow().setSlideshowTimeline(elapsedMs, totalMs);
        m_imageView->hostSlideshow().setSlideshowCycleProgress(phaseT);
    }
    if (idx != m_currentIndex && !m_slideshowAdvancing) {
        m_slideshowAdvancing = true;
        setCurrentIndex(idx);
        m_slideshowAdvancing = false;
    }
    if (!m_slideshowPaused) {
        updateSlideshowFromClock();
    } else if (m_imageView && idx >= 0 && idx < n) {
        m_imageView->hostSlideshow().setSlideshowPhase(m_session.paths().at(idx), QString(), -1.0);
        m_imageView->hostSlideshow().setSlideshowMotionPaused(true);
    }
}

void MainWindow::pauseSlideshow()
{
    if (!m_slideshowClockRunning || m_slideshowPaused) {
        return;
    }
    // Fold last wall segment into unitless position, then freeze.
    if (m_slideshowClock.isValid()) {
        int intervalMs = m_slideshowIntervalMs > 0 ? m_slideshowIntervalMs : 1;
        const qint64 wallDelta = m_slideshowClock.elapsed();
        if (wallDelta > 0) {
            m_slideshowPosition += qreal(wallDelta) / qreal(intervalMs);
        }
    }
    m_slideshowPausedAccumMs = 0; // position is authoritative
    m_slideshowPaused = true;
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    if (m_slideshowTimer) {
        m_slideshowTimer->stop();
    }
    if (m_imageView) {
        // Drop any in-flight live/snapshot overlay so ←/→ can show the new
        // image immediately (otherwise the hold layer masks LoadReplace).
        m_imageView->hostSlideshow().cancelSlideshowTransition();
        m_imageView->hostSlideshow().setSlideshowMotionPaused(true);
        m_imageView->hostSlideshow().setSlideshowProgressPaused(true);
        m_imageView->hostSlideshow().setSlideshowPausedHud(true);
    }
    updateScrollBarPolicyForMode();
    updateSlideshowActionUi();
}

void MainWindow::resumeSlideshow()
{
    if (!m_slideshowPaused) {
        return;
    }
    if (m_session.paths().isEmpty() || isWorkspaceMode()) {
        stopSlideshow();
        return;
    }
    m_slideshowPaused = false;
    // Continue the same timeline (do not reset base index / cycle).
    m_slideshowClock.start();
    m_slideshowClockRunning = true;
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowMotionPaused(false);
        m_imageView->hostSlideshow().setSlideshowPausedHud(false);
        m_imageView->hostSlideshow().setSlideshowProgressPaused(false);
        m_imageView->flashHud(tr("▶  Slideshow"),
                              formatSlideshowInterval(m_slideshowIntervalMs));
    }
    if (m_slideshowTimer && !m_slideshowTimer->isActive()) {
        m_slideshowTimer->start();
    }
    updateSlideshowFromClock();
    armSlideshowCursorHide();
    updateScrollBarPolicyForMode();
    updateSlideshowActionUi();
}

void MainWindow::stopSlideshow()
{
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowNavHot(false);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setVisibleLoadsSuspended(false);
    }
    // Session = playing or paused. Silent no-ops when fully idle.
    const bool wasSession = isSlideshowSession()
        || (m_slideshowAct && m_slideshowAct->isChecked());
    if (!wasSession) {
        return;
    }
    const bool announce = isSlideshowSession();

    m_slideshowPaused = false;
    m_slideshowClockRunning = false;
    m_slideshowPausedAccumMs = 0;
    m_slideshowPosition = 0.0;
    m_slideshowTransitionCycle = -1;
    m_slideshowPendingToIndex = -1;
    m_slideshowPreloadToIdx = -1;
    if (m_slideshowTimer) {
        m_slideshowTimer->stop();
    }
    if (m_cursorHideTimer) {
        m_cursorHideTimer->stop();
    }
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowMotionPaused(false);
        m_imageView->hostSlideshow().setSlideshowPausedHud(false);
        m_imageView->hostSlideshow().cancelSlideshowTransition();
        m_imageView->hostSlideshow().cancelSlideshowMotion();
    }
    showSlideshowCursor();
    qApp->removeEventFilter(this);
    updateSlideshowActionUi();
    if (m_imageView) {
        m_imageView->hostSlideshow().setSlideshowProgress(false, 0);
        // Slideshow advances the session index without loadImage (pure phase owns
        // the viewport). Leaving without a canvas load left Image mode on the
        // pre-show tile. Session flags are already cleared so LoadReplace runs.
        if (announce && isImageMode()
            && m_currentIndex >= 0
            && m_currentIndex < m_session.paths().size()) {
            m_imageView->hostDisplayPipeline().loadImage(m_session.paths().at(m_currentIndex));
            m_imageView->hostSlideshow().restoreImageFramingAfterSlideshow();
            m_imageView->flashHud(tr("■  Slideshow stopped"));
        } else if (announce) {
            m_imageView->hostSlideshow().restoreImageFramingAfterSlideshow();
            m_imageView->flashHud(tr("■  Slideshow stopped"));
        }
    }
    updateScrollBarPolicyForMode();
    // Slideshow is separate from user fullscreen: if we entered fullscreen for
    // this show, leave it with the show. Esc then returns to a normal window.
    if (m_slideshowOwnsFullscreen && isFullScreen()) {
        m_slideshowOwnsFullscreen = false;
        showNormal();
        updateFullscreenUi();
    } else {
        m_slideshowOwnsFullscreen = false;
    }
}

void MainWindow::updateHelpPanelFromWidget(QWidget *widget, const QPoint &localPos)
{
    if (!m_helpPanel || !widget) {
        return;
    }
    QAction *act = nullptr;
    if (auto *tb = qobject_cast<QToolBar *>(widget)) {
        act = tb->actionAt(localPos);
    } else if (auto *menu = qobject_cast<QMenu *>(widget)) {
        act = menu->actionAt(localPos);
    } else if (auto *btn = qobject_cast<QToolButton *>(widget)) {
        act = btn->defaultAction();
        if (!act) {
            // Some toolbar buttons use setDefaultAction; others only QAction via actions().
            const QList<QAction *> acts = btn->actions();
            if (!acts.isEmpty()) {
                act = acts.first();
            }
        }
        // If this is a child of a toolbar, prefer toolbar actionAt in parent coords.
        if (!act) {
            if (auto *parentTb = qobject_cast<QToolBar *>(btn->parentWidget())) {
                act = parentTb->actionAt(btn->mapTo(parentTb, localPos));
            }
        }
    } else if (auto *mb = qobject_cast<QMenuBar *>(widget)) {
        act = mb->actionAt(localPos);
    }
    if (act && !act->isSeparator() && !act->menu()) {
        m_helpPanel->showAction(act);
    } else if (act && act->menu() && !act->isSeparator()) {
        // Top-level menu title: still show a short line if it has statusTip/whatsThis.
        m_helpPanel->showAction(act);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Help panel: pick up disabled toolbar/menu items (QAction::hovered skips them).
    if (m_helpPanel
        && (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)) {
        if (auto *w = qobject_cast<QWidget *>(watched)) {
            QPoint pos;
            if (event->type() == QEvent::MouseMove) {
                pos = static_cast<QMouseEvent *>(event)->pos();
            } else {
                pos = static_cast<QHoverEvent *>(event)->position().toPoint();
            }
            if (qobject_cast<QToolBar *>(w) || qobject_cast<QMenu *>(w)
                || qobject_cast<QToolButton *>(w) || qobject_cast<QMenuBar *>(w)) {
                updateHelpPanelFromWidget(w, pos);
            }
        }
    }
    // Mode / filmstrip overviews when the pointer enters the canvas or strip.
    if (m_helpPanel && event->type() == QEvent::HoverEnter) {
        if (watched == m_thumbnailBar || watched == m_thumbnailDock) {
            showFilmstripHelp();
        } else if (watched == m_imageView) {
            showCurrentModeHelp();
        }
    }

    // Escape is also a WindowShortcut (fullscreen / leave Image). QLineEdit does
    // not accept ShortcutOverride for Esc, so the window shortcut wins unless we
    // claim it here first — KeyPress alone never runs.
    auto under = [](QWidget *root, QObject *obj) -> bool {
        if (!root || !obj) {
            return false;
        }
        if (obj == root) {
            return true;
        }
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            return root->isAncestorOf(w);
        }
        return false;
    };
    const bool locWatch = under(m_locationBar, watched) || watched == m_locationEdit;
    const bool searchWatch = under(m_searchBar, watched) || watched == m_searchEdit;
    if (locWatch
        && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
                return true;
            }
            cancelLocationBar();
            return true;
        }
    }
    if (searchWatch
        && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
                return true;
            }
            cancelSearchBar();
            return true;
        }
    }
    if (isSlideshowSession()) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::HoverMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::TabletMove:
            armSlideshowCursorHide();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleSlideshow()
{
    if (m_slideshowPaused) {
        resumeSlideshow();
    } else if (m_slideshowClockRunning) {
        pauseSlideshow();
    } else {
        startSlideshow();
    }
}

QString MainWindow::historyEntryLabel(const QStringList &paths) const
{
    if (paths.isEmpty()) {
        return tr("(empty)");
    }
    auto containerDir = [](const QString &p) -> QString {
        if (PagePath::isPageRef(p)) {
            const PagePath::Ref r = PagePath::parse(p);
            return r.valid ? QFileInfo(r.pdfPath).absolutePath() : QString();
        }
        if (ArchivePath::isArchiveRef(p)) {
            const ArchivePath::Ref r = ArchivePath::parse(p);
            return r.valid ? QFileInfo(r.archivePath).absolutePath() : QString();
        }
        return QFileInfo(p).absolutePath();
    };
    const QString dir = containerDir(paths.first());
    bool sameDir = !dir.isEmpty();
    for (const QString &p : paths) {
        if (containerDir(p) != dir) {
            sameDir = false;
            break;
        }
    }
    if (paths.size() == 1) {
        return PagePath::displayName(paths.first());
    }
    if (sameDir) {
        // Prefer archive filename when all members share one container.
        if (PagePath::isPageRef(paths.first())) {
            const PagePath::Ref r = PagePath::parse(paths.first());
            if (r.valid) {
                return tr("%1 — %n page(s)", "history entry", paths.size())
                    .arg(QFileInfo(r.pdfPath).fileName());
            }
        }
        if (ArchivePath::isArchiveRef(paths.first())) {
            const ArchivePath::Ref r = ArchivePath::parse(paths.first());
            if (r.valid) {
                return tr("%1 — %n image(s)", "history entry", paths.size())
                    .arg(QFileInfo(r.archivePath).fileName());
            }
        }
        const QString folder = QFileInfo(dir).fileName();
        return tr("%1 — %n image(s)", "history entry", paths.size()).arg(folder);
    }
    return tr("%1 (+%n more)", "history entry", paths.size() - 1)
        .arg(PagePath::displayName(paths.first()));
}


QString MainWindow::historyEntryHelpHtml(const QStringList &paths) const
{
    if (paths.isEmpty()) {
        return tr("<p>This history slot is empty.</p>");
    }

    QStringList items;
    items.reserve(paths.size());
    constexpr int kListCap = 40;
    const int shown = qMin(paths.size(), kListCap);
    for (int i = 0; i < shown; ++i) {
        const QString &p = paths.at(i);
        const QString name = PagePath::displayName(p);
        // Prefer a readable name; include the session path when it differs.
        if (name == p || p.endsWith(name)) {
            items.append(QStringLiteral("<li><code>%1</code></li>").arg(p.toHtmlEscaped()));
        } else {
            items.append(QStringLiteral("<li>%1<br/><code>%2</code></li>")
                             .arg(name.toHtmlEscaped(), p.toHtmlEscaped()));
        }
    }

    QString more;
    if (paths.size() > kListCap) {
        more = tr("<p><i>…and %n more path(s) not listed here.</i></p>",
                  nullptr, paths.size() - kListCap);
    }

    return tr(
        "<p>Reopen this <b>Recent Session</b> — the full ordered list of images "
        "from a previous open (not a .biltoo project).</p>"
        "<p><b>%n file(s) / page(s)</b> will replace the current session "
        "(same as Open with those paths).</p>"
        "<ul>%1</ul>%2"
        "<p>Paths keep archive members (<code>//archive:</code>) and document "
        "pages in session form. Choosing an entry does not restore Workspace poses "
        "or project appearance — only the image list.</p>",
        nullptr,
        paths.size())
        .arg(items.join(QString()), more);
}

void MainWindow::rememberSessionHistory(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    QStringList normalized;
    normalized.reserve(paths.size());
    for (const QString &p : paths) {
        // Never run archive refs through QFileInfo::absoluteFilePath — QDir::cleanPath
        // collapses "//archive:" into "/archive:" and breaks the member path.
        const QString abs = PagePath::canonicalSessionPath(p);
        if (!abs.isEmpty()) {
            normalized.append(abs);
        }
    }
    if (normalized.isEmpty()) {
        return;
    }

    for (int i = m_sessionHistory.size() - 1; i >= 0; --i) {
        if (m_sessionHistory.at(i) == normalized) {
            m_sessionHistory.removeAt(i);
        }
    }
    m_sessionHistory.prepend(normalized);
    while (m_sessionHistory.size() > kMaxSessionHistory) {
        m_sessionHistory.removeLast();
    }
    rebuildHistoryMenu();
}

void MainWindow::rebuildHistoryMenu()
{
    if (!m_historyMenu) {
        return;
    }
    m_historyMenu->clear();
    if (m_sessionHistory.isEmpty()) {
        auto *empty = m_historyMenu->addAction(tr("(No recent sessions)"));
        empty->setEnabled(false);
        empty->setWhatsThis(tr(
            "<p>No sessions have been remembered yet.</p>"
            "<p>After you open a set of images (Open, directory, archive, …), "
            "the ordered path list is stored under <b>Recent Sessions</b> "
            "(up to %1 entries). That is separate from <b>Recent Projects</b> "
            "(.biltoo files).</p>").arg(kMaxSessionHistory));
        empty->setProperty(
            "biltooDisabledHelp",
            tr("Open some images first; sessions appear here after a successful open."));
        if (m_helpPanel) {
            connect(empty, &QAction::hovered, this, [this, empty]() {
                m_helpPanel->showAction(empty);
            });
        }
        m_historyMenu->addSeparator();
        if (m_clearHistoryAct) {
            m_historyMenu->addAction(m_clearHistoryAct);
            m_clearHistoryAct->setEnabled(false);
        }
        return;
    }

    for (int i = 0; i < m_sessionHistory.size(); ++i) {
        const QStringList &entry = m_sessionHistory.at(i);
        QAction *act = m_historyMenu->addAction(historyEntryLabel(entry));
        act->setData(i);
        // Status bar: compact path summary; Help panel gets the full list via whatsThis.
        if (entry.size() <= 2) {
            QStringList names;
            for (const QString &p : entry) {
                names.append(PagePath::displayName(p));
            }
            act->setStatusTip(names.join(QStringLiteral(" · ")));
        } else {
            act->setStatusTip(
                tr("%1 — %n item(s); open Help panel or hover for the full list",
                   nullptr, entry.size())
                    .arg(PagePath::displayName(entry.first())));
        }
        act->setWhatsThis(historyEntryHelpHtml(entry));
        act->setToolTip(act->statusTip());
        connect(act, &QAction::triggered, this, &MainWindow::openHistoryEntry);
        // Dynamic menu entries are not covered by the one-shot installActionHelpTracking.
        if (m_helpPanel) {
            connect(act, &QAction::hovered, this, [this, act]() {
                m_helpPanel->showAction(act);
            });
            connect(act, &QAction::triggered, this, [this, act](bool) {
                m_helpPanel->showAction(act);
            });
        }
    }
    m_historyMenu->addSeparator();
    if (m_clearHistoryAct) {
        m_historyMenu->addAction(m_clearHistoryAct);
        m_clearHistoryAct->setEnabled(true);
    }
}

void MainWindow::openHistoryEntry()
{
    auto *act = qobject_cast<QAction *>(sender());
    if (!act) {
        return;
    }
    const int index = act->data().toInt();
    if (index < 0 || index >= m_sessionHistory.size()) {
        return;
    }
    // Copy paths — loadFiles will reshuffle history.
    const QStringList paths = m_sessionHistory.at(index);
    loadFiles(paths);
}

void MainWindow::clearSessionHistory()
{
    m_sessionHistory.clear();
    rebuildHistoryMenu();
}

void MainWindow::duplicateSelected()
{
    if (!m_imageView) {
        return;
    }
    if (!isWorkspaceMode() && !isGalleryMode()) {
        return;
    }
    // Prefer SessionImageId — path occurrence always hits the first tile and
    // broke selection + source identity on the 2nd+ Duplicate of the same path.
    const QList<SessionImageId> sourceIds = m_imageView->selectedSessionIds();
    const QStringList fallbackPaths = m_imageView->selectedPaths();
    if (sourceIds.isEmpty() && fallbackPaths.isEmpty()) {
        return;
    }
    if (m_imageView->hostUndoStack() && !m_sessionUndoGuard) {
        m_imageView->hostUndoStack()->push(
            new SessionDuplicateCommand(this, sourceIds, fallbackPaths));
        return;
    }
    applyDuplicate(sourceIds, fallbackPaths);
}

QVector<SessionImageId> MainWindow::applyDuplicate(const QList<SessionImageId> &sourceIds,
                                                   const QStringList &fallbackPaths)
{
    QVector<SessionImageId> newIds;
    if (!m_imageView) {
        return newIds;
    }
    if (!isWorkspaceMode() && !isGalleryMode()) {
        return newIds;
    }

    // Reselect the exact source tiles by id (redo-safe with path duplicates).
    if (!sourceIds.isEmpty()) {
        m_imageView->selectBySessionIds(sourceIds);
    } else if (!fallbackPaths.isEmpty()) {
        // Unbound tiles only — last resort.
        m_imageView->selectPathsByOccurrence(fallbackPaths);
    }
    if (m_imageView->selectedPaths().isEmpty()) {
        return newIds;
    }

    // Paths of the tiles we are about to copy (after id-based reselect).
    const QStringList sourcePaths = m_imageView->selectedPaths();
    if (sourcePaths.isEmpty()) {
        return newIds;
    }

    // Allocate session rows *before* canvas copies so tiles bind on create
    // (no unbound window between duplicateSelected and bindSelectedSessionIds).
    const int firstNew = m_session.paths().size();
    for (const QString &path : sourcePaths) {
        if (!path.isEmpty()) {
            const SessionImageId id = allocSessionId();
            m_session.append(path, id);
            newIds.append(id);
        }
    }
    if (m_session.paths().size() == firstNew || newIds.isEmpty()) {
        return {};
    }
    m_session.validateUniqueIds("applyDuplicate");
    m_imageView->duplicateSelected(newIds, firstNew);
    // Copies are already id-bound; rebind refreshes path-order / membership.
    m_imageView->rebindWorkspaceSession(m_session.paths(), m_session.ids());
    syncThumbnailCanvasMembership();

    // Select the new tiles by stable id — not path occurrence or stale index.
    QList<SessionImageId> newIdList = newIds.toList();
    m_imageView->selectBySessionIds(newIdList);

    QList<int> newIndices;
    for (int i = firstNew; i < m_session.paths().size(); ++i) {
        newIndices.append(i);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        if (isWorkspaceMode()) {
            m_thumbnailBar->setMultiSelectEnabled(true);
        }
        m_thumbnailBar->setSelectedIndices(newIndices);
        if (!newIndices.isEmpty()) {
            m_thumbnailBar->setCurrentIndex(newIndices.first());
        }
    }
    applyThumbnailVisibility();
    if (isGalleryMode()) {
        m_imageView->hostGallery().applyLayout(GalleryPackReason::SessionMutate);
    }
    updateWorkspaceActionVisibility();
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Duplicated %n image(s) into the session", "", sourcePaths.size()), 3000);
    }
    return newIds;
}

int MainWindow::sessionIndexOfId(SessionImageId id) const
{
    return m_session.indexOfId(id);
}

namespace {

const char kWorkspaceClipMime[] = "application/x-biltoo-workspace-items";

QByteArray encodeWorkspaceClipboard(const QList<WorkspaceItemState> &items)
{
    QJsonArray arr;
    for (const WorkspaceItemState &s : items) {
        QJsonObject o = ProjectFile::appearanceToJson(s, /*includePose=*/true);
        o.insert(QStringLiteral("path"), s.path);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("biltoo-workspace-clipboard"));
    // Stage 4b: same nested sparse appearance as project format ≥ 2.
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("items"), arr);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QList<WorkspaceItemState> decodeWorkspaceClipboard(const QByteArray &bytes)
{
    QList<WorkspaceItemState> out;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        return out;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString()
        != QLatin1String("biltoo-workspace-clipboard")) {
        return out;
    }
    if (root.value(QStringLiteral("version")).toInt(0) < 2) {
        return out;
    }
    const QJsonArray arr = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject o = v.toObject();
        WorkspaceItemState s = ProjectFile::appearanceFromJson(o);
        s.path = o.value(QStringLiteral("path")).toString();
        if (!s.path.isEmpty()) {
            out.append(s);
        }
    }
    return out;
}

} // namespace

static void writeWorkspaceMime(const QList<WorkspaceItemState> &items)
{
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(kWorkspaceClipMime), encodeWorkspaceClipboard(items));
    QList<QUrl> urls;
    for (const WorkspaceItemState &s : items) {
        const QUrl u = QUrl::fromLocalFile(s.path);
        if (u.isValid() && !urls.contains(u)) {
            urls.append(u);
        }
    }
    if (!urls.isEmpty()) {
        mime->setUrls(urls);
    }
    QApplication::clipboard()->setMimeData(mime);
}

bool MainWindow::clipboardHasWorkspaceItems() const
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    return mime && mime->hasFormat(QString::fromLatin1(kWorkspaceClipMime));
}

void MainWindow::updatePasteActionEnabled()
{
    if (!m_pasteWorkspaceAct) {
        return;
    }
    m_pasteWorkspaceAct->setEnabled(clipboardHasWorkspaceItems());
}

void MainWindow::copyWorkspaceItems()
{
    // Prefer page text selection in Image mode (Shift+drag regions).
    if (m_imageView && m_imageView->isImageMode()
        && m_imageView->hostTextLayer().selectionCount() > 0) {
        if (m_imageView->copySelectedText()) {
            statusBar()->showMessage(
                tr("Copied %n text region(s)", "", m_imageView->hostTextLayer().selectionCount()),
                3000);
            return;
        }
    }

    if (!m_imageView || !isWorkspaceMode()) {
        return;
    }
    const QList<WorkspaceItemState> items = m_imageView->captureSelectedWorkspaceClipboard();
    if (items.isEmpty()) {
        return;
    }
    writeWorkspaceMime(items);
    m_workspacePasteGeneration = 0;
    updatePasteActionEnabled();
    if (statusBar()) {
        statusBar()->showMessage(tr("Copied %n Workspace tile(s)", "", items.size()), 2500);
    }
}

void MainWindow::cutWorkspaceItems()
{
    if (!m_imageView || !isWorkspaceMode()) {
        return;
    }
    const QList<WorkspaceItemState> items = m_imageView->captureSelectedWorkspaceClipboard();
    if (items.isEmpty()) {
        return;
    }
    writeWorkspaceMime(items);
    m_workspacePasteGeneration = 0;
    updatePasteActionEnabled();
    if (m_imageView->hostUndoStack() && !m_sessionUndoGuard) {
        m_imageView->hostUndoStack()->push(new WorkspaceCutCommand(this, items));
        return;
    }
    applyWorkspaceCut(items);
}

void MainWindow::applyWorkspaceCut(const QList<WorkspaceItemState> &items)
{
    if (!m_imageView || items.isEmpty()) {
        return;
    }
    QList<SessionImageId> ids;
    ids.reserve(items.size());
    for (const WorkspaceItemState &s : items) {
        if (s.sessionId != kInvalidSessionImageId) {
            ids.append(s.sessionId);
        }
    }
    m_imageView->removeCanvasSessionIds(ids);
    syncThumbnailCanvasMembership();
    markWorkspaceDirty();
    updateWorkspaceActionVisibility();
    if (statusBar()) {
        statusBar()->showMessage(tr("Cut %n Workspace tile(s)", "", items.size()), 2500);
    }
}

void MainWindow::applyWorkspaceUncut(const QList<WorkspaceItemState> &items)
{
    if (!m_imageView || items.isEmpty()) {
        return;
    }
    if (!isWorkspaceMode()) {
        enterWorkspaceMode();
    }
    QList<SessionImageId> ids;
    QStringList paths;
    QList<int> indices;
    for (const WorkspaceItemState &s : items) {
        if (s.sessionId == kInvalidSessionImageId || s.path.isEmpty()) {
            continue;
        }
        // Ensure appearance (pose) is in the store for restore.
        m_imageView->setSessionAppearance(s.sessionId, s);
        ids.append(s.sessionId);
        paths.append(s.path);
        indices.append(m_session.indexOfId(s.sessionId));
    }
    m_imageView->placeSessionIdsOnCanvas(ids, paths, indices);
    syncThumbnailCanvasMembership();
    markWorkspaceDirty();
    updateWorkspaceActionVisibility();
}

void MainWindow::pasteWorkspaceItems()
{
    if (!m_imageView) {
        return;
    }
    if (!clipboardHasWorkspaceItems()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Clipboard has no Workspace tiles."), 2500);
        }
        return;
    }
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    QList<WorkspaceItemState> items =
        decodeWorkspaceClipboard(mime->data(QString::fromLatin1(kWorkspaceClipMime)));
    if (items.isEmpty()) {
        return;
    }
    if (m_imageView->hostUndoStack() && !m_sessionUndoGuard) {
        m_imageView->hostUndoStack()->push(new WorkspacePasteCommand(this, items));
        return;
    }
    applyWorkspacePaste(items);
}

QVector<SessionImageId> MainWindow::applyWorkspacePaste(const QList<WorkspaceItemState> &clipItems)
{
    QVector<SessionImageId> newIds;
    if (!m_imageView || clipItems.isEmpty()) {
        return newIds;
    }
    if (!isWorkspaceMode()) {
        enterWorkspaceMode();
    }

    ++m_workspacePasteGeneration;
    const qreal off = 40.0 * static_cast<qreal>(m_workspacePasteGeneration);
    QList<WorkspaceItemState> items = clipItems;
    QList<int> indices;
    QList<SessionImageId> selectIds;
    newIds.reserve(items.size());
    indices.reserve(items.size());
    selectIds.reserve(items.size());
    for (WorkspaceItemState &s : items) {
        s.pos += QPointF(off, off);
        const SessionImageId id = allocSessionId();
        m_session.append(s.path, id);
        s.sessionId = id;
        m_imageView->setSessionAppearance(id, s);
        newIds.append(id);
        selectIds.append(id);
        indices.append(m_session.size() - 1);
    }

    m_session.validateUniqueIds("pasteWorkspaceItems");
    if (m_thumbnailBar) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        m_thumbnailBar->setMultiSelectEnabled(true);
    }
    m_imageView->placeWorkspaceClipboardItems(items, newIds, indices);
    m_imageView->selectBySessionIds(selectIds);
    if (m_thumbnailBar && !indices.isEmpty()) {
        m_thumbnailBar->setSelectedIndices(indices);
        m_thumbnailBar->setCurrentIndex(indices.first());
    }
    syncThumbnailCanvasMembership();
    markWorkspaceDirty();
    updateWorkspaceActionVisibility();
    if (statusBar()) {
        statusBar()->showMessage(tr("Pasted %n Workspace tile(s)", "", items.size()), 2500);
    }
    return newIds;
}

void MainWindow::saveProject()
{
    if (m_projectPath.isEmpty()) {
        saveProjectAs();
        return;
    }
    QString err;
    if (!writeProjectToPath(m_projectPath, &err)) {
        if (statusBar()) {
            statusBar()->showMessage(err, 8000);
        }
    } else {
        rememberRecentProject(m_projectPath);
        m_workspaceDirty = false;
        if (statusBar()) {
            statusBar()->showMessage(tr("Project saved."), 3000);
        }
    }
}

void MainWindow::saveProjectAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project"),
        m_projectPath.isEmpty() ? QDir::homePath() : m_projectPath,
        tr("Biltoo Project (*.biltoo);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString out = path;
    if (!out.endsWith(QLatin1String(".biltoo"), Qt::CaseInsensitive)) {
        out += QStringLiteral(".biltoo");
    }
    QString err;
    if (!writeProjectToPath(out, &err)) {
        if (statusBar()) {
            statusBar()->showMessage(err, 8000);
        }
        return;
    }
    m_projectPath = out;
    rememberRecentProject(out);
    m_workspaceDirty = false;
    if (statusBar()) {
        statusBar()->showMessage(tr("Project saved."), 3000);
    }
}

bool MainWindow::openProjectFile(const QString &path, QString *error)
{
    if (path.isEmpty()) {
        if (error) {
            *error = tr("No project path given.");
        }
        return false;
    }
    if (!QFileInfo::exists(path)) {
        if (error) {
            *error = tr("Project file does not exist: %1").arg(path);
        }
        return false;
    }
    if (!QFileInfo(path).isFile()) {
        if (error) {
            *error = tr("Not a project file: %1").arg(path);
        }
        return false;
    }
    QString err;
    if (!loadProjectFromPath(path, &err)) {
        if (error) {
            *error = err.isEmpty() ? tr("Failed to load project: %1").arg(path) : err;
        }
        return false;
    }
    m_projectPath = QFileInfo(path).absoluteFilePath();
    rememberRecentProject(m_projectPath);
    m_workspaceDirty = false;
    if (statusBar()) {
        statusBar()->showMessage(tr("Project loaded."), 3000);
    }
    return true;
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"),
        m_projectPath.isEmpty() ? QDir::homePath() : m_projectPath,
        tr("Biltoo Project (*.biltoo);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString err;
    if (!openProjectFile(path, &err)) {
        if (statusBar()) {
            statusBar()->showMessage(err, 8000);
        }
        return;
    }
}

QString MainWindow::currentProjectModeString() const
{
    if (isWorkspaceMode()) {
        return QStringLiteral("workspace");
    }
    if (isGalleryMode()) {
        return QStringLiteral("gallery");
    }
    return QStringLiteral("image");
}

QString MainWindow::containerHashPathForSessionPath(const QString &sessionPath)
{
    if (sessionPath.isEmpty()) {
        return {};
    }
    // Archive/PDF pages: hash the container file, not the virtual page path.
    if (ArchivePath::isArchiveRef(sessionPath)) {
        return ArchivePath::archiveFilePath(sessionPath);
    }
    if (PagePath::isPageRef(sessionPath)) {
        return PagePath::pdfFilePath(sessionPath);
    }
    return sessionPath;
}

QString MainWindow::ensureProjectAsset(ProjectDocument *doc,
                                       QHash<QString, QString> *pathToSha,
                                       const QDir &projDir,
                                       const QString &sessionPath)
{
    if (!doc || !pathToSha) {
        return {};
    }
    const QString hashPath = containerHashPathForSessionPath(sessionPath);
    if (hashPath.isEmpty()) {
        return {};
    }
    if (pathToSha->contains(hashPath)) {
        return pathToSha->value(hashPath);
    }
    const QString sha = ProjectFile::fileSha256(hashPath);
    if (sha.isEmpty()) {
        return {};
    }
    pathToSha->insert(hashPath, sha);
    ProjectAsset a;
    a.sha256 = sha;
    a.path = hashPath;
    const QString rel = projDir.relativeFilePath(hashPath);
    if (!rel.startsWith(QLatin1String(".."))) {
        a.pathRelative = rel;
    }
    doc->assets.append(a);
    return sha;
}

QHash<SessionImageId, ItemComponents::Placement> MainWindow::captureLiveWorkspacePoses() const
{
    QHash<SessionImageId, ItemComponents::Placement> poses;
    if (!m_imageView) {
        return poses;
    }
    for (ImageItem *item : m_imageView->liveItems()) {
        if (!item || item->sessionId() == kInvalidSessionImageId) {
            continue;
        }
        poses.insert(item->sessionId(), item->placement());
    }
    return poses;
}

void MainWindow::mergePoseIntoProjectImage(ProjectImage *im, const ItemComponents::Placement &pose)
{
    if (!im) {
        return;
    }
    im->appearance.pos = pose.pos;
    im->appearance.scale = pose.scale;
    im->appearance.scaleY = pose.scaleY;
    im->appearance.shear = pose.shear;
    im->appearance.rotation = pose.rotation;
    im->appearance.opacity = pose.opacity;
    im->appearance.z = pose.z;
    im->appearance.hFlip = pose.hFlip;
    im->appearance.vFlip = pose.vFlip;
    im->hasWorkspacePose = true;
    im->hasAppearance = true;
}

void MainWindow::attachWorkspaceBackgroundToDocument(ProjectDocument *doc, const QDir &projDir)
{
    if (!doc || !m_imageView) {
        return;
    }
    WorkspaceBackground wb = m_imageView->hostCanvasBg().workspaceRef();
    if (wb.isAppDefault()) {
        return;
    }
    // JSON only: absolute path, optional path relative to the project
    // file, and SHA-256 checksum. Never copy tile bytes into a side folder.
    if (wb.mode == WorkspaceBackgroundMode::ImageTile && !wb.imagePath.isEmpty()) {
        const QFileInfo fi(wb.imagePath);
        const QString abs = fi.canonicalFilePath().isEmpty()
            ? fi.absoluteFilePath()
            : fi.canonicalFilePath();
        wb.imagePath = abs;
        const QString rel = projDir.relativeFilePath(abs);
        if (!rel.startsWith(QLatin1String("..")) && !QFileInfo(rel).isAbsolute()) {
            wb.imagePathRelative = rel;
        } else {
            wb.imagePathRelative.clear();
        }
        wb.imageSha256 = ProjectFile::fileSha256(abs);
    }
    doc->hasWorkspaceBackground = true;
    doc->workspaceBackground = wb;
}

bool MainWindow::writeProjectToPath(const QString &projectPath, QString *error)
{
    // Stage 4b persistence boundary: nested sparse appearance (format ≥ 2)
    // built from sessionAppearanceValue + live Workspace poses. Never treat
    // a raw fat WorkspaceItemState pointer as save authority.
    ProjectDocument doc;
    doc.version = 2;
    doc.mode = currentProjectModeString();

    QHash<QString, QString> pathToSha; // absolute path → sha256
    const QDir projDir = QFileInfo(projectPath).absoluteDir();
    const QHash<SessionImageId, ItemComponents::Placement> poses = captureLiveWorkspacePoses();

    for (int i = 0; i < m_session.size(); ++i) {
        const QString path = m_session.pathAt(i);
        const SessionImageId id = m_session.idAt(i);
        const QString sha = ensureProjectAsset(&doc, &pathToSha, projDir, path);
        if (sha.isEmpty()) {
            if (error) {
                *error = tr("Cannot hash image: %1").arg(path);
            }
            return false;
        }
        ProjectImage im;
        im.id = id;
        im.assetSha256 = sha;
        if (m_imageView && m_imageView->hasSessionAppearance(id)) {
            im.appearance = m_imageView->sessionAppearanceValue(id);
            im.hasAppearance = true;
        }
        // Archive/PDF pages need the full ref stored; asset is only the container.
        if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)) {
            im.hasAppearance = true;
        }
        if (poses.contains(id)) {
            mergePoseIntoProjectImage(&im, poses.value(id));
        }
        im.appearance.path = path;
        im.appearance.sessionId = id;
        doc.images.append(im);
    }

    attachWorkspaceBackgroundToDocument(&doc, projDir);
    return ProjectFile::save(projectPath, doc, error);
}

void MainWindow::installProjectAppearances(
    const QVector<SessionImageId> &ids,
    const QVector<WorkspaceItemState> &appearanceByRow,
    const QVector<bool> &rowHasAppearance)
{
    // Stage 4a load path: every row goes through setSessionAppearance so ItemWorld
    // dual-fills sparse Crop/ContentBake/Color/Attention/Placement tables.
    if (!m_imageView) {
        return;
    }
    m_imageView->itemWorld().clearAppearance();
    for (int i = 0; i < appearanceByRow.size(); ++i) {
        if (!rowHasAppearance.at(i)) {
            continue;
        }
        const SessionImageId sid = ids.at(i);
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        m_imageView->setSessionAppearance(sid, appearanceByRow.at(i));
    }
}

void MainWindow::installProjectBackground(const ProjectDocument &doc, const QString &projectPath)
{
    if (!m_imageView) {
        return;
    }
    if (doc.hasWorkspaceBackground) {
        WorkspaceBackground wb = doc.workspaceBackground;
        if (wb.mode == WorkspaceBackgroundMode::ImageTile) {
            const QString resolved = ProjectFile::resolveWorkspaceBackgroundImage(
                wb, projectPath);
            if (!resolved.isEmpty()) {
                wb.imagePath = resolved;
            }
        }
        m_imageView->setWorkspaceBackground(wb);
    } else {
        m_imageView->clearWorkspaceBackground();
    }
}

void MainWindow::enterProjectCanvasMode(
    const QStringList &paths,
    const QVector<SessionImageId> &ids,
    const QVector<WorkspaceItemState> &appearanceByRow,
    const QVector<bool> &rowHasPose,
    const ProjectDocument &doc,
    int poseCount)
{
    // Workspace canvas is a subset of the session: only images with a saved
    // pose (hasWorkspacePose) belong on the canvas. Gallery still shows all.
    const bool wantWorkspace =
        (doc.mode == QLatin1String("workspace")) || poseCount > 0;
    if (wantWorkspace) {
        if (!isWorkspaceMode()) {
            enterWorkspaceMode();
        }
        if (m_imageView) {
            // Pose (pos/scale/shear/rotation/opacity/z) is already in ItemWorld placement.
            // addImageForSession schedules LoadAdd; on decode, LoadAdd applies
            // placement via applyState from the store.
            //
            // Do not call loadImage/LoadReplace here: empty-workspace LoadReplace
            // would seed the classic/first path as an unbound tile (leftover path
            // meant for session navigation in an empty Workspace).
            for (int i = 0; i < rowHasPose.size(); ++i) {
                if (!rowHasPose.at(i)) {
                    continue;
                }
                const SessionImageId sid = ids.at(i);
                if (sid == kInvalidSessionImageId) {
                    continue;
                }
                m_imageView->setSessionAppearance(sid, appearanceByRow.at(i));
                m_imageView->addImageForSession(paths.at(i), sid, i);
            }
            m_imageView->updateWorkspaceSceneRect();
            syncThumbnailCanvasMembership();
        }
    } else if (doc.mode == QLatin1String("gallery") || isGalleryMode()) {
        if (!isGalleryMode()) {
            enterGalleryMode(initialGalleryLayoutForOpen());
        }
        if (m_imageView) {
            m_imageView->setWorkspacePaths(m_session.paths(), m_session.ids());
        }
    } else if (m_imageView && !m_session.paths().isEmpty()) {
        // Image-mode project: must be in Image mode before loadImage.
        // Calling loadImage while still in Workspace/Gallery with an empty canvas
        // seeds the first path via LoadReplace (unbound tile) — intermittent when
        // Preferences "Start in workspace mode" is on.
        if (!isImageMode()) {
            if (m_workspaceModeAct) {
                m_workspaceModeAct->setChecked(false);
            }
            if (m_thumbnailBar) {
                m_thumbnailBar->setMultiSelectEnabled(false);
            }
            m_imageView->setViewMode(ImageView::ViewMode::Image);
        }
        // setCurrentIndex loads the classic canvas and binds session cursor.
        m_currentIndex = -1;
        setCurrentIndex(0);
    }
}

void MainWindow::finishProjectInstall(const QStringList &missing)
{
    updateWindowTitle();
    updateWorkspaceActionVisibility();
    if (!missing.isEmpty() && statusBar()) {
        statusBar()->showMessage(
            tr("Project loaded with %n missing image(s).", "", missing.size()), 8000);
    }
    m_workspaceDirty = false;
}

void MainWindow::installProjectSession(
    const QStringList &paths,
    QVector<SessionImageId> ids,
    QVector<WorkspaceItemState> appearanceByRow,
    const QVector<bool> &rowHasAppearance,
    const QVector<bool> &rowHasPose,
    const ProjectDocument &doc,
    const QString &projectPath,
    const QStringList &missing)
{
    // Replace session with preserved ids where possible.
    for (int i = 0; i < ids.size(); ++i) {
        if (ids.at(i) == kInvalidSessionImageId) {
            ids[i] = m_session.allocId();
        }
        // Keep appearance rows keyed to the final session id.
        appearanceByRow[i].sessionId = ids.at(i);
    }
    m_session.clear();
    m_session.replaceAll(paths, ids);
    m_currentIndex = 0;

    int poseCount = 0;
    for (bool p : rowHasPose) {
        if (p) {
            ++poseCount;
        }
    }

    if (m_imageView) {
        // Drop prior session tiles, stashes, and durable Workspace snapshot so
        // enterWorkspaceMode does not restore the previous arrangement.
        m_imageView->clearWorkspace();
        installProjectAppearances(ids, appearanceByRow, rowHasAppearance);
        installProjectBackground(doc, projectPath);
    }

    if (m_thumbnailBar) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    }
    applyThumbnailVisibility();

    enterProjectCanvasMode(paths, ids, appearanceByRow, rowHasPose, doc, poseCount);
    finishProjectInstall(missing);
}



QString MainWindow::promptLocateMissingAsset(const ProjectAsset &asset,
                                             const ProjectImage &im,
                                             const QString &projectPath,
                                             bool *skipAllMissing)
{
    const QString hint = asset.path.isEmpty()
        ? (asset.pathRelative.isEmpty()
               ? im.assetSha256.left(16)
               : asset.pathRelative)
        : asset.path;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("Locate missing image"),
        tr("Could not find:\n%1\n\nSHA-256: %2\n\nLocate the file manually?")
            .arg(hint, im.assetSha256),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::NoToAll,
        QMessageBox::Yes);
    if (choice == QMessageBox::NoToAll) {
        *skipAllMissing = true;
        return {};
    }
    if (choice != QMessageBox::Yes) {
        return {};
    }
    const QString picked = QFileDialog::getOpenFileName(
        this,
        tr("Locate image"),
        QFileInfo(hint).absolutePath().isEmpty()
            ? QFileInfo(projectPath).absolutePath()
            : QFileInfo(hint).absolutePath(),
        tr("Images (*.png *.jpg *.jpeg *.webp *.tif *.tiff *.bmp *.gif);;All Files (*)"));
    if (picked.isEmpty()) {
        return {};
    }
    if (!asset.sha256.isEmpty()) {
        const QString got = ProjectFile::fileSha256(picked);
        if (!got.isEmpty()
            && got.compare(asset.sha256, Qt::CaseInsensitive) != 0) {
            const auto useAnyway = QMessageBox::warning(
                this,
                tr("Hash mismatch"),
                tr("The selected file does not match the stored SHA-256.\n"
                   "Expected %1\nGot %2\n\nUse it anyway?")
                    .arg(asset.sha256.left(16), got.left(16)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (useAnyway != QMessageBox::Yes) {
                return {};
            }
        }
    }
    return picked;
}

QString MainWindow::sessionPathFromResolvedAsset(const QString &resolved,
                                                 const ProjectImage &im) const
{
    // Asset resolves to a filesystem file (image, archive, or PDF container).
    QString sessionPath = resolved;
    if (im.hasAppearance && PagePath::isPageRef(im.appearance.path)) {
        const PagePath::Ref ref = PagePath::parse(im.appearance.path);
        if (ref.valid) {
            sessionPath = PagePath::makeRef(resolved, ref.page);
        }
    } else if (im.hasAppearance && ArchivePath::isArchiveRef(im.appearance.path)) {
        const ArchivePath::Ref ref = ArchivePath::parse(im.appearance.path);
        if (ref.valid) {
            sessionPath = ArchivePath::makeRef(resolved, ref.memberPath);
        }
    }
    return sessionPath;
}

bool MainWindow::resolveProjectSessionRows(
    const ProjectDocument &doc,
    const QString &projectPath,
    QStringList *paths,
    QVector<SessionImageId> *ids,
    QVector<WorkspaceItemState> *appearanceByRow,
    QVector<bool> *rowHasAppearance,
    QVector<bool> *rowHasPose,
    QStringList *missing)
{
    QHash<QString, ProjectAsset> assetsBySha;
    for (const ProjectAsset &a : doc.assets) {
        assetsBySha.insert(a.sha256.toLower(), a);
    }

    bool skipAllMissing = false;
    for (const ProjectImage &im : doc.images) {
        ProjectAsset asset = assetsBySha.value(im.assetSha256.toLower());
        if (asset.sha256.isEmpty()) {
            // Asset row missing — synthesize from image only (path unknown).
            asset.sha256 = im.assetSha256;
        }
        QString resolveErr;
        QString resolved = ProjectFile::resolveAssetPath(asset, projectPath, &resolveErr);
        if (resolved.isEmpty() && !skipAllMissing) {
            resolved = promptLocateMissingAsset(asset, im, projectPath, &skipAllMissing);
        }
        if (resolved.isEmpty()) {
            missing->append(resolveErr.isEmpty() ? im.assetSha256.left(12) : resolveErr);
            continue;
        }
        const QString sessionPath = sessionPathFromResolvedAsset(resolved, im);
        paths->append(sessionPath);
        const SessionImageId id =
            im.id != kInvalidSessionImageId ? im.id : kInvalidSessionImageId;
        ids->append(id);

        WorkspaceItemState st = im.appearance;
        st.path = sessionPath;
        st.sessionId = id;
        appearanceByRow->append(st);
        rowHasAppearance->append(im.hasAppearance || im.hasWorkspacePose);
        rowHasPose->append(im.hasWorkspacePose);
    }
    return !paths->isEmpty();
}

bool MainWindow::loadProjectFromPath(const QString &projectPath, QString *error)
{
    ProjectDocument doc;
    if (!ProjectFile::load(projectPath, &doc, error)) {
        return false;
    }
    if (doc.images.isEmpty()) {
        if (error) {
            *error = tr("Project has no images.");
        }
        return false;
    }

    QStringList paths;
    QVector<SessionImageId> ids;
    QVector<WorkspaceItemState> appearanceByRow;
    QVector<bool> rowHasAppearance;
    QVector<bool> rowHasPose;
    QStringList missing;

    if (!resolveProjectSessionRows(doc, projectPath, &paths, &ids, &appearanceByRow,
                                   &rowHasAppearance, &rowHasPose, &missing)) {
        if (error) {
            *error = tr("No images could be resolved from the project.");
        }
        return false;
    }

    stopSlideshow();
    installProjectSession(paths, ids, appearanceByRow, rowHasAppearance, rowHasPose,
                          doc, projectPath, missing);
    return true;
}



void MainWindow::rememberRecentProject(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (abs.isEmpty()) {
        return;
    }
    m_recentProjects.removeAll(abs);
    m_recentProjects.prepend(abs);
    while (m_recentProjects.size() > kMaxRecentProjects) {
        m_recentProjects.removeLast();
    }
    rebuildRecentProjectsMenu();
}

void MainWindow::rebuildRecentProjectsMenu()
{
    if (!m_recentProjectsMenu) {
        return;
    }
    m_recentProjectsMenu->clear();
    if (m_recentProjects.isEmpty()) {
        auto *empty = m_recentProjectsMenu->addAction(tr("(No recent projects)"));
        empty->setEnabled(false);
        empty->setWhatsThis(tr(
            "<p>No projects have been remembered yet.</p>"
            "<p>After you open or save a <code>.biltoo</code> project, it appears "
            "under <b>Recent Projects</b>. That is separate from "
            "<b>Recent Sessions</b> (image path lists only).</p>"));
        empty->setProperty(
            "biltooDisabledHelp",
            tr("Open or save a .biltoo project first."));
        if (m_helpPanel) {
            connect(empty, &QAction::hovered, this, [this, empty]() {
                m_helpPanel->showAction(empty);
            });
        }
        m_recentProjectsMenu->addSeparator();
        if (m_clearRecentProjectsAct) {
            m_recentProjectsMenu->addAction(m_clearRecentProjectsAct);
            m_clearRecentProjectsAct->setEnabled(false);
        }
        return;
    }

    for (int i = 0; i < m_recentProjects.size(); ++i) {
        const QString &p = m_recentProjects.at(i);
        const QFileInfo fi(p);
        // Show filename; full path in status tip / Help. Missing files stay listed.
        QAction *act = m_recentProjectsMenu->addAction(
            QStringLiteral("%1. %2").arg(i + 1).arg(fi.fileName()));
        act->setData(p);
        act->setStatusTip(p);
        act->setWhatsThis(tr(
            "<p>Reopen this <b>Recent Project</b> (<code>.biltoo</code>).</p>"
            "<p><b>Path:</b> <code>%1</code></p>"
            "<p>Loads session order, appearance, and Workspace poses from the "
            "project file. Not the same as Recent Sessions (paths only).</p>%2")
            .arg(p.toHtmlEscaped(),
                 fi.isFile()
                     ? QString()
                     : tr("<p><b>Currently unavailable:</b> the file is missing on disk.</p>")));
        if (!fi.isFile()) {
            act->setEnabled(false);
            act->setText(act->text() + tr(" (missing)"));
            act->setProperty(
                "biltooDisabledHelp",
                tr("This project file is missing on disk."));
        }
        connect(act, &QAction::triggered, this, &MainWindow::openRecentProject);
        if (m_helpPanel) {
            connect(act, &QAction::hovered, this, [this, act]() {
                m_helpPanel->showAction(act);
            });
            connect(act, &QAction::triggered, this, [this, act](bool) {
                m_helpPanel->showAction(act);
            });
        }
    }
    m_recentProjectsMenu->addSeparator();
    if (m_clearRecentProjectsAct) {
        m_recentProjectsMenu->addAction(m_clearRecentProjectsAct);
        m_clearRecentProjectsAct->setEnabled(true);
    }
}

void MainWindow::openRecentProject()
{
    auto *act = qobject_cast<QAction *>(sender());
    if (!act) {
        return;
    }
    const QString path = act->data().toString();
    if (path.isEmpty()) {
        return;
    }
    QString err;
    if (!openProjectFile(path, &err)) {
        if (!QFileInfo::exists(path)) {
            m_recentProjects.removeAll(path);
            rebuildRecentProjectsMenu();
        }
        if (statusBar()) {
            statusBar()->showMessage(err, 8000);
        }
        return;
    }
}

void MainWindow::clearRecentProjects()
{
    m_recentProjects.clear();
    rebuildRecentProjectsMenu();
}


void MainWindow::applyWorkspaceBackground(const WorkspaceBackground &bg)
{
    if (!m_imageView) {
        return;
    }
    m_imageView->setWorkspaceBackgroundShowDefault(false);
    m_imageView->setWorkspaceBackground(bg);
    syncWorkspaceBackgroundActions();
    markWorkspaceDirty();
}

void MainWindow::editWorkspaceBackground()
{
    if (!m_imageView) {
        return;
    }
    if (!isWorkspaceMode()) {
        enterWorkspaceMode();
    }
    const WorkspaceBackground before = m_imageView->hostCanvasBg().workspaceRef();
    WorkspaceBackgroundDialog dlg(this);
    dlg.setCanvasContext(/*forProject=*/true);
    dlg.setAppDefaultColors(
        m_imageView->hostCanvasBg().primaryColor(),
        m_imageView->hostCanvasBg().altColor(),
        m_imageView->hostCanvasBg().currentPattern() == BackgroundPattern::Checkerboard);
    dlg.setBackground(before);
    // Live canvas preview while the dialog is open; restore on cancel.
    connect(&dlg, &WorkspaceBackgroundDialog::backgroundChanged, this,
            [this](const WorkspaceBackground &bg) {
                if (m_imageView) {
                    m_imageView->setWorkspaceBackground(bg);
                }
            });
    if (dlg.exec() != QDialog::Accepted) {
        m_imageView->setWorkspaceBackground(before);
        syncWorkspaceBackgroundActions();
        return;
    }
    const WorkspaceBackground after = dlg.background();
    // Reset to before so redo applies the accepted state once.
    m_imageView->setWorkspaceBackground(before);
    if (m_imageView->hostUndoStack() && !m_sessionUndoGuard) {
        m_imageView->hostUndoStack()->push(
            new WorkspaceBackgroundCommand(this, before, after));
    } else {
        applyWorkspaceBackground(after);
    }
    if (statusBar()) {
        QString msg;
        switch (after.mode) {
        case WorkspaceBackgroundMode::Solid:
            msg = tr("Workspace background: solid");
            break;
        case WorkspaceBackgroundMode::Checkerboard:
            msg = tr("Workspace background: checkerboard");
            break;
        case WorkspaceBackgroundMode::ImageTile:
            msg = tr("Workspace background: image pattern");
            break;
        case WorkspaceBackgroundMode::AppDefault:
        default:
            msg = tr("Workspace background: application default");
            break;
        }
        statusBar()->showMessage(msg, 2500);
    }
}

void MainWindow::editBackgroundForCurrentMode()
{
    if (isWorkspaceMode()) {
        editWorkspaceBackground();
    } else {
        editViewBackground();
    }
}

void MainWindow::editViewBackground()
{
    if (!m_imageView) {
        return;
    }
    const WorkspaceBackground before = m_imageView->hostCanvasBg().viewRef();
    WorkspaceBackgroundDialog dlg(this);
    dlg.setCanvasContext(/*forProject=*/false);
    dlg.setAppDefaultColors(
        m_imageView->hostCanvasBg().primaryColor(),
        m_imageView->hostCanvasBg().altColor(),
        m_imageView->hostCanvasBg().currentPattern() == BackgroundPattern::Checkerboard);
    dlg.setBackground(before);
    connect(&dlg, &WorkspaceBackgroundDialog::backgroundChanged, this,
            [this](const WorkspaceBackground &bg) {
                if (m_imageView) {
                    m_imageView->setViewBackground(bg);
                }
            });
    if (dlg.exec() != QDialog::Accepted) {
        m_imageView->setViewBackground(before);
        return;
    }
    const WorkspaceBackground after = dlg.background();
    m_imageView->setViewBackground(after);
    if (statusBar()) {
        QString msg;
        switch (after.mode) {
        case WorkspaceBackgroundMode::Solid:
            msg = tr("View background: solid");
            break;
        case WorkspaceBackgroundMode::Checkerboard:
            msg = tr("View background: checkerboard");
            break;
        case WorkspaceBackgroundMode::ImageTile:
            msg = tr("View background: image pattern");
            break;
        case WorkspaceBackgroundMode::ContentBlur:
            msg = tr("View background: content blur");
            break;
        case WorkspaceBackgroundMode::AppDefault:
        default:
            msg = tr("View background: Preferences default");
            break;
        }
        statusBar()->showMessage(msg, 2500);
    }
}

void MainWindow::workspaceBackgroundDefault(bool checked)
{
    if (!m_imageView) {
        return;
    }
    // Permanent AppDefault: nothing to preview. Keep the control checked and
    // disabled via syncWorkspaceBackgroundActions (avoids a stuck toggle).
    if (m_imageView->hostCanvasBg().workspaceRef().isAppDefault()) {
        m_imageView->setWorkspaceBackgroundShowDefault(false);
        syncWorkspaceBackgroundActions();
        return;
    }
    // Temporary view of the Preferences background — does not change project
    // state, undo stack, or dirty flag.
    m_imageView->setWorkspaceBackgroundShowDefault(checked);
    syncWorkspaceBackgroundActions();
    if (statusBar()) {
        statusBar()->showMessage(
            checked ? tr("Showing application default background (temporary)")
                    : tr("Restored project Workspace background"),
            2000);
    }
}
