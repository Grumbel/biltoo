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

