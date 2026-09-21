// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"
#include "slideshowclocks.h"
#include "viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "thumtoocache.h"
#include "sessionopen.h"
#include "sessionsort.h"
#include "sessionexpand.h"
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

} // namespace

bool MainWindow::isImageFile(const QString &path)
{
    return ImageLoader::isImageFile(path);
}

QStringList MainWindow::expandPaths(const QStringList &paths) const
{
    return SessionExpand::expandPathList(paths, m_recursive);
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
                SessionExpand::emptyResultMessage(sourcePaths, append), 8000);
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
        const SessionExpand::ReportFn report = [guard, gen, &reportClock, &lastReportMs](
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
        const SessionExpand::CancelFn cancel = [guard, gen]() {
            MainWindow *const window = guard.data();
            return !window || gen != window->m_expandGeneration;
        };

        const QStringList images = SessionExpand::expandPathList(paths, recursive, report, cancel);
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
    return SessionSort::modeNeedsImageProbe(m_sortMode);
}

bool MainWindow::sessionLooksLikePagedDocument(const QStringList &paths)
{
    return SessionSort::looksLikePagedDocument(paths);
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

    const QStringList paths = m_session.paths();
    SortMode mode = m_sortMode;
    QHash<QString, qint64> mtimes;
    QHash<QString, qint64> fsizes;

    if (mode == SortMode::MTime || mode == SortMode::FileSize) {
        // Prefer background path (sortModeNeedsImageProbe). If still here,
        // snapshot metadata once — never QFileInfo inside a comparator loop.
        for (const QString &p : paths) {
            const QFileInfo fi(p);
            mtimes.insert(p, fi.lastModified().toMSecsSinceEpoch());
            fsizes.insert(p, fi.size());
        }
    } else if (mode == SortMode::Width || mode == SortMode::Height
               || mode == SortMode::PixelCount || mode == SortMode::AspectRatio) {
        // Probe modes must use sortFileListWithProbesInBackground; basename
        // fallback if we are still on the sync path.
        mode = SortMode::Name;
    }

    const QVector<int> order = SessionSort::orderIndices(mode, paths, {}, mtimes, fsizes);
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
    return SessionSort::orderIndices(mode, paths, sizes, mtimes, fsizes);
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
            newIndex = indexOfPathPreferId(current);
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
            // Path fallback maps successive path occurrences to successive session
            // rows (same idea as removeSessionPaths).
            QList<int> indices;
            QSet<int> seen;
            QHash<QString, int> pathOccurrence;
            const int n = qMax(workspaceIds.size(), workspacePaths.size());
            for (int i = 0; i < n; ++i) {
                int idx = -1;
                if (i < workspaceIds.size()
                    && workspaceIds.at(i) != kInvalidSessionImageId) {
                    idx = indexOfSessionId(workspaceIds.at(i));
                }
                if (idx < 0 && i < workspacePaths.size()
                    && !workspacePaths.at(i).isEmpty()) {
                    const QString &path = workspacePaths.at(i);
                    const int wantOcc = pathOccurrence.value(path, 0);
                    pathOccurrence[path] = wantOcc + 1;
                    idx = m_session.indexOfPathOccurrence(path, wantOcc);
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
        newIndex = indexOfPathPreferId(currentPath);
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

int MainWindow::indexOfPathPreferId(const QString &path) const
{
    return m_session.indexOfPathPreferId(path);
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
        newIndex = indexOfPathPreferId(currentPath);
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
        if (e.id != kInvalidSessionImageId && m_session.hasId(e.id)) {
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
    removeSessionIndices(m_session.indicesForIds(ids));
}

void MainWindow::removeSessionPaths(const QStringList &paths)
{
    // Path-only fallback when list index is unknown. Prefer removeSessionIds /
    // removeSessionIndices. Successive path occurrences map via SessionDocument.
    if (paths.isEmpty() || m_session.isEmpty()) {
        return;
    }
    removeSessionIndices(m_session.indicesForPathsByOccurrence(paths));
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

