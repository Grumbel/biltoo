// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "util/biltoo_thread.h"
#include "slideshow/slideshowclocks.h"
#include "view/viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "host/thumtoocache.h"
#include "session/sessionopen.h"
#include "session/sessionsort.h"
#include "session/sessionexpand.h"
#include "util/ttfp_trace.h"
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
    GUI_BUDGET("MainWindow::finishApplyExpandedLoad");
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
                            // Queued: must not run inside onSizeResolveGateComplete
                            // (Direct AutoConnection made setFiles block the gate ~60s).
                            // Yield one turn so Gallery pack paints first; filmstrip
                            // fill is progressive for the full session (no row cap).
                            QTimer::singleShot(0, this, installFilmstrip);
                            // Do not preparePaths/warmUris the entire session — that
                            // floods workers after virtualized Gallery open.
                            if (m_imageView) {
                                QStringList warm;
                                for (ImageItem *it : m_imageView->liveItems()) {
                                    if (it && !it->path().isEmpty()) {
                                        warm.append(it->path());
                                    }
                                }
                                if (!warm.isEmpty()) {
                                    ThumtooCache::preparePaths(warm);
                                    ThumtooCache::warmUris(warm);
                                }
                            }
                        },
                        static_cast<Qt::ConnectionType>(
                            Qt::QueuedConnection | Qt::SingleShotConnection));
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
    updateFileExportActions();
    TtfpTrace::mark("finishApplyExpandedLoad_return");
    // First pixels often arrive async; if still none, leave session active for
    // installDisplayPixels to close the report.
}

void MainWindow::newSession()
{
    stopSlideshow();
    m_session.clear();
    updateFileExportActions();
    m_currentIndex = -1;
    m_galleryReturnActive = false;
    m_workspaceReturnActive = false;
    if (m_imageView) {
        // Drop all canvas objects and classic path so Image mode does not
        // reload the previous file after the mode switch.
        m_imageView->hostWorkspace().clearWorkspace();
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
            m_imageView->hostGallery().focusSessionId(sid);
        } else {
            m_imageView->hostGallery().focusSessionPath(m_session.paths().at(m_currentIndex));
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
    // Use the *argument* index — not m_currentIndex — so callers that publish
    // a target before mutating m_currentIndex cannot pin the wrong SessionImageId.
    m_imageView->setCurrentSessionId(sessionIdAt(index));
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
                m_imageView->hostGallery().focusSessionId(sid);
            } else {
                m_imageView->hostGallery().focusSessionPath(path);
            }
        }
    } else if (m_imageView) {
        const SessionImageId sid = sessionIdAt(m_currentIndex);
        if (sid != kInvalidSessionImageId
            && m_imageView->findItemBySessionId(sid)) {
            m_imageView->hostGallery().focusSessionId(sid);
        } else {
            m_imageView->hostGallery().focusSessionPath(path);
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



