// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include <QUndoCommand>
#include <QSet>
#include "slideshow/slideshowclocks.h"
#include "view/viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "host/thumtoocache.h"
#include "session/sessionopen.h"
#include "session/sessionsort.h"
#include "session/sessionexpand.h"
#include "ttfp_trace.h"
#include "session/projectfile.h"
#include "host/archivepath.h"
#include "host/pagepath.h"
#include "shell/epublayoutdialog.h"
#include "workspace/workspacebackgrounddialog.h"
#include "imageitem.h"
#include "display/imagecache.h"
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

// Session list sort chrome (split from mainwindow_session).


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
    // Order applied in onDone via applySessionOrder when provided; otherwise here.
    if (!onDone) {
        applySessionOrder(newFiles, newIds, currentSessionId());
    } else {
        m_session.replaceAll(newFiles, newIds);
    }
    setExpandProgressBusy(false);
    if (statusBar()) {
        statusBar()->clearMessage();
    }
    if (onDone) {
        onDone();
    }
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
            const QVector<int> order = SessionSort::orderIndices(
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

        const QVector<int> order = SessionSort::orderIndices(
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

    auto applyUi = [this, currentId]() {
        applySessionOrder(m_session.paths(), m_session.ids(), currentId);
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



namespace {

/** Undoable session reorder (filmstrip drag). */
class SessionReorderCommand : public QUndoCommand {
public:
    SessionReorderCommand(MainWindow *mw,
                          const QStringList &beforePaths,
                          const QVector<SessionImageId> &beforeIds,
                          const QStringList &afterPaths,
                          const QVector<SessionImageId> &afterIds,
                          SessionImageId focusId,
                          const QVector<SessionImageId> &selectIds)
        : QUndoCommand(QObject::tr("Reorder session"))
        , m_mw(mw)
        , m_beforePaths(beforePaths)
        , m_beforeIds(beforeIds)
        , m_afterPaths(afterPaths)
        , m_afterIds(afterIds)
        , m_focusId(focusId)
        , m_selectIds(selectIds)
    {
    }

    void undo() override
    {
        if (m_mw) {
            m_mw->applySessionOrder(m_beforePaths, m_beforeIds, m_focusId);
            m_mw->selectSessionIdsOnFilmstrip(m_selectIds);
        }
    }

    void redo() override
    {
        if (m_mw) {
            m_mw->applySessionOrder(m_afterPaths, m_afterIds, m_focusId);
            m_mw->selectSessionIdsOnFilmstrip(m_selectIds);
        }
    }

private:
    MainWindow *m_mw = nullptr;
    QStringList m_beforePaths;
    QVector<SessionImageId> m_beforeIds;
    QStringList m_afterPaths;
    QVector<SessionImageId> m_afterIds;
    SessionImageId m_focusId = kInvalidSessionImageId;
    QVector<SessionImageId> m_selectIds;
};

} // namespace

void MainWindow::applySessionOrder(const QStringList &paths,
                                   const QVector<SessionImageId> &ids,
                                   SessionImageId focusId)
{
    if (paths.size() != ids.size()) {
        return;
    }
    m_session.replaceAll(paths, ids);

    if (m_thumbnailBar) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        if (isWorkspaceMode() || isGalleryMode()) {
            m_thumbnailBar->setMultiSelectEnabled(true);
        }
    }

    SessionImageId id = focusId;
    if (id == kInvalidSessionImageId) {
        id = currentSessionId();
    }
    int newIndex = 0;
    if (id != kInvalidSessionImageId) {
        newIndex = indexOfSessionId(id);
    }
    if (newIndex < 0) {
        newIndex = 0;
    }
    m_currentIndex = -1;
    setCurrentIndex(newIndex);

    if (isGalleryMode()) {
        const LayoutMode layout = m_imageView
            ? m_imageView->hostLayout().currentMode()
            : LayoutMode::Masonry;
        populateGalleryCanvas();
        if (m_imageView) {
            m_imageView->enterGallery(layout);
            if (id != kInvalidSessionImageId
                && m_imageView->findItemBySessionId(id)) {
                m_imageView->focusSessionId(id);
            }
        }
    } else if (isWorkspaceMode() && m_imageView) {
        m_imageView->reorderItemsByPaths(m_session.paths(), m_session.ids());
    }

    applyThumbnailVisibility();
    updateFileExportActions();
}

void MainWindow::selectSessionIdsOnFilmstrip(const QVector<SessionImageId> &ids)
{
    if (!m_thumbnailBar || ids.isEmpty()) {
        return;
    }
    QList<int> indices;
    indices.reserve(ids.size());
    for (SessionImageId id : ids) {
        const int idx = indexOfSessionId(id);
        if (idx >= 0) {
            indices.append(idx);
        }
    }
    if (!indices.isEmpty()) {
        m_thumbnailBar->setSelectedIndices(indices);
    }
}

void MainWindow::reorderSessionRows(const QList<int> &rows, int insertBefore)
{
    if (rows.isEmpty() || m_session.size() <= 1) {
        return;
    }
    const int n = m_session.size();
    QList<int> moving;
    QSet<int> seen;
    for (int r : rows) {
        if (r < 0 || r >= n || seen.contains(r)) {
            continue;
        }
        seen.insert(r);
        moving.append(r);
    }
    if (moving.isEmpty()) {
        return;
    }
    std::sort(moving.begin(), moving.end());

    // Build remaining + extracted in selection order (sorted by original index).
    QStringList remainPaths;
    QVector<SessionImageId> remainIds;
    QStringList movePaths;
    QVector<SessionImageId> moveIds;
    remainPaths.reserve(n);
    remainIds.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (seen.contains(i)) {
            movePaths.append(m_session.pathAt(i));
            moveIds.append(m_session.idAt(i));
        } else {
            remainPaths.append(m_session.pathAt(i));
            remainIds.append(m_session.idAt(i));
        }
    }

    // insertBefore is in the full list before removal. Adjust for removed rows
    // strictly before the insertion point.
    int dest = insertBefore;
    int removedBefore = 0;
    for (int r : moving) {
        if (r < insertBefore) {
            ++removedBefore;
        }
    }
    dest = insertBefore - removedBefore;
    dest = qBound(0, dest, remainPaths.size());

    QStringList afterPaths = remainPaths;
    QVector<SessionImageId> afterIds = remainIds;
    for (int k = 0; k < movePaths.size(); ++k) {
        afterPaths.insert(dest + k, movePaths.at(k));
        afterIds.insert(dest + k, moveIds.at(k));
    }
    if (afterPaths == m_session.paths() && afterIds == m_session.ids()) {
        return;
    }

    const QStringList beforePaths = m_session.paths();
    const QVector<SessionImageId> beforeIds = m_session.ids();
    SessionImageId focusId = kInvalidSessionImageId;
    if (!moveIds.isEmpty()) {
        focusId = moveIds.first();
    } else {
        focusId = currentSessionId();
    }

    if (m_imageView && m_imageView->hostUndoStack() && !m_sessionUndoGuard) {
        m_imageView->hostUndoStack()->push(
            new SessionReorderCommand(this, beforePaths, beforeIds,
                                      afterPaths, afterIds, focusId, moveIds));
        return;
    }
    applySessionOrder(afterPaths, afterIds, focusId);
    selectSessionIdsOnFilmstrip(moveIds);
}

void MainWindow::showSessionReorderDialog()
{
    if (m_session.size() < 2) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Need at least two images to reorder"), 3000);
        }
        return;
    }

    SessionReorderDialog dlg(this);
    dlg.setSession(m_session.paths(), m_session.ids());
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QStringList afterPaths = dlg.orderedPaths();
    const QVector<SessionImageId> afterIds = dlg.orderedIds();
    if (afterPaths.size() != afterIds.size()
        || afterPaths.size() != m_session.size()) {
        return;
    }
    if (afterPaths == m_session.paths() && afterIds == m_session.ids()) {
        return;
    }

    const QStringList beforePaths = m_session.paths();
    const QVector<SessionImageId> beforeIds = m_session.ids();
    SessionImageId focusId = currentSessionId();
    QVector<SessionImageId> selectIds;
    if (m_thumbnailBar) {
        for (int row : m_thumbnailBar->selectedIndices()) {
            if (row >= 0 && row < beforeIds.size()) {
                selectIds.append(beforeIds.at(row));
            }
        }
    }
    if (selectIds.isEmpty() && focusId != kInvalidSessionImageId) {
        selectIds.append(focusId);
    }

    if (m_imageView && m_imageView->hostUndoStack() && !m_sessionUndoGuard) {
        m_imageView->hostUndoStack()->push(
            new SessionReorderCommand(this, beforePaths, beforeIds,
                                      afterPaths, afterIds, focusId, selectIds));
        return;
    }
    applySessionOrder(afterPaths, afterIds, focusId);
    selectSessionIdsOnFilmstrip(selectIds);
}

