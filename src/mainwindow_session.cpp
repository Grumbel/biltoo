// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"

namespace {

QString imageFileDialogFilter()
{
    QStringList patterns;
    for (const QString &suffix : ImageLoader::imageSuffixes()) {
        patterns.append(QStringLiteral("*.%1").arg(suffix));
    }
    return QObject::tr("Images (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' ')));
}

/** Undoable session removal (Gallery delete / thumb remove). */
class SessionRemoveCommand : public QUndoCommand {
public:
    SessionRemoveCommand(MainWindow *mw, const QList<QPair<int, QString>> &entries)
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
        if (m_mw) {
            QList<int> indices;
            for (const auto &e : m_entries) {
                indices.append(e.first);
            }
            m_mw->applySessionRemoveIndices(indices);
        }
    }

private:
    MainWindow *m_mw = nullptr;
    QList<QPair<int, QString>> m_entries;
};

} // namespace

bool MainWindow::isImageFile(const QString &path)
{
    return ImageLoader::isImageFile(path);
}

QStringList MainWindow::expandPaths(const QStringList &paths) const
{
    // AUDIT M17: canonical paths so relative/absolute/symlink spellings dedup.
    auto canonicalImage = [](const QString &path) -> QString {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            return {};
        }
        const QString resolved = info.canonicalFilePath();
        return resolved.isEmpty() ? info.absoluteFilePath() : resolved;
    };

    QStringList images;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDir::Filters filters = QDir::Files | QDir::Readable | QDir::NoDotAndDotDot;
            QDirIterator::IteratorFlags flags = m_recursive
                ? QDirIterator::Subdirectories
                : QDirIterator::NoIteratorFlags;
            QDirIterator it(path, filters, flags);
            while (it.hasNext()) {
                const QString full = it.next();
                if (isImageFile(full)) {
                    const QString c = canonicalImage(full);
                    if (!c.isEmpty()) {
                        images.append(c);
                    }
                }
            }
        } else if (info.isFile() && isImageFile(path)) {
            const QString c = canonicalImage(path);
            if (!c.isEmpty()) {
                images.append(c);
            }
        }
    }
    return images;
}

void MainWindow::sortFileList()
{
    if (m_files.size() <= 1) {
        return;
    }

    auto nameLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    };

    auto imageSize = [](const QString &path) {
        QImageReader reader(path);
        return reader.size(); // may be invalid if unknown
    };

    switch (m_sortMode) {
    case SortMode::MTime:
        std::stable_sort(m_files.begin(), m_files.end(), [&](const QString &a, const QString &b) {
            const QFileInfo fa(a), fb(b);
            if (fa.lastModified() != fb.lastModified()) {
                return fa.lastModified() < fb.lastModified();
            }
            return nameLess(a, b);
        });
        break;
    case SortMode::FileSize:
        std::stable_sort(m_files.begin(), m_files.end(), [&](const QString &a, const QString &b) {
            const qint64 sa = QFileInfo(a).size();
            const qint64 sb = QFileInfo(b).size();
            if (sa != sb) {
                return sa < sb;
            }
            return nameLess(a, b);
        });
        break;
    case SortMode::Width:
        std::stable_sort(m_files.begin(), m_files.end(), [&](const QString &a, const QString &b) {
            const int wa = imageSize(a).width();
            const int wb = imageSize(b).width();
            if (wa != wb) {
                return wa < wb;
            }
            return nameLess(a, b);
        });
        break;
    case SortMode::Height:
        std::stable_sort(m_files.begin(), m_files.end(), [&](const QString &a, const QString &b) {
            const int ha = imageSize(a).height();
            const int hb = imageSize(b).height();
            if (ha != hb) {
                return ha < hb;
            }
            return nameLess(a, b);
        });
        break;
    case SortMode::PixelCount:
        std::stable_sort(m_files.begin(), m_files.end(), [&](const QString &a, const QString &b) {
            const QSize sa = imageSize(a);
            const QSize sb = imageSize(b);
            const qint64 pa = qint64(sa.width()) * sa.height();
            const qint64 pb = qint64(sb.width()) * sb.height();
            if (pa != pb) {
                return pa < pb;
            }
            return nameLess(a, b);
        });
        break;
    case SortMode::Name:
    default:
        std::stable_sort(m_files.begin(), m_files.end(), nameLess);
        break;
    }
}

void MainWindow::setSortMode(SortMode mode)
{
    m_sortMode = mode;
    if (m_sortNameAct) {
        m_sortNameAct->setChecked(mode == SortMode::Name);
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

    if (m_files.isEmpty()) {
        return;
    }

    const QString current = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                ? m_files.at(m_currentIndex)
                                : QString();
    sortFileList();
    m_thumbnailBar->setFiles(m_files);
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
    }

    int newIndex = 0;
    if (!current.isEmpty()) {
        newIndex = m_files.indexOf(current);
        if (newIndex < 0) {
            newIndex = 0;
        }
    }
    m_currentIndex = -1; // force reload of Image mode cursor
    setCurrentIndex(newIndex);

    // Gallery pack follows session order — refresh canvas so tiles match sort.
    if (isGalleryMode()) {
        const ImageView::LayoutMode layout = m_imageView
            ? m_imageView->layoutMode()
            : ImageView::LayoutMode::Masonry;
        populateGalleryCanvas();
        if (m_imageView) {
            m_imageView->enterGallery(layout);
            if (!current.isEmpty()) {
                m_imageView->focusSessionPath(current);
            }
        }
    } else if (isWorkspaceMode() && m_imageView) {
        // Keep canvas membership; only path order for any future gallery use.
        m_imageView->reorderItemsByPaths(m_files);
    }

    applyThumbnailVisibility();
}

void MainWindow::sortByName()
{
    setSortMode(SortMode::Name);
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

void MainWindow::applyThumbnailVisibility()
{
    bool show = m_files.size() > 1;
    if (m_forceNoThumbnails) {
        show = false;
    } else if (m_forceThumbnails) {
        show = !m_files.isEmpty();
    }
    m_thumbnailBar->setVisible(show && !isFullScreen());
    m_toggleThumbnailBarAct->setChecked(show);
    if (!isFullScreen()) {
        m_thumbnailBarVisibleBeforeFullscreen = show;
    }
}

void MainWindow::loadFiles(const QStringList &paths, int startAt)
{
    stopSlideshow();

    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
        // AUDIT M26: explicit feedback when Open finds nothing usable
        if (statusBar()) {
            statusBar()->showMessage(tr("No readable images found."), 5000);
        }
        return;
    }

    m_files = images;
    sortFileList();
    m_currentIndex = -1;

    m_thumbnailBar->setFiles(m_files);
    applyThumbnailVisibility();

    int idx = startAt;
    if (idx < 0 || idx >= m_files.size()) {
        idx = 0;
    }
    setCurrentIndex(idx);
    updateNavigationActions();
    updateWorkspaceActionVisibility();
}

void MainWindow::newSession()
{
    stopSlideshow();
    m_files.clear();
    m_currentIndex = -1;
    m_galleryReturnActive = false;
    if (m_imageView) {
        // Drop all canvas objects and classic path so Image mode does not
        // reload the previous file after the mode switch.
        m_imageView->clearWorkspace();
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
        m_metadataPanel->setImagePath(QString());
    }
    updateStatus();
    if (statusBar()) {
        statusBar()->showMessage(tr("New session."), 2000);
    }
}

void MainWindow::appendFiles(const QStringList &paths)
{
    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("No readable images to add."), 5000);
        }
        return;
    }

    const QString current = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                ? m_files.at(m_currentIndex)
                                : QString();

    // Deduplicate while preserving order of existing entries
    QSet<QString> seen(m_files.begin(), m_files.end());
    int added = 0;
    for (const QString &path : images) {
        if (!seen.contains(path)) {
            m_files.append(path);
            seen.insert(path);
            ++added;
        }
    }
    if (added == 0) {
        return;
    }

    // Paths currently on the workspace (selection must be restored after setFiles)
    const QStringList workspacePaths = isWorkspaceMode() ? m_imageView->itemPaths() : QStringList();

    sortFileList();
    m_thumbnailBar->setFiles(m_files);
    if (isWorkspaceMode()) {
        // setFiles rebuilds items; re-apply multi-select mode and canvas selection
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
        // If the canvas was empty, selection may still be empty — restore paths we had
        if (m_thumbnailBar->selectedIndices().isEmpty() && !workspacePaths.isEmpty()) {
            QList<int> indices;
            for (const QString &path : workspacePaths) {
                const int idx = m_files.indexOf(path);
                if (idx >= 0) {
                    indices.append(idx);
                }
            }
            m_thumbnailBar->setSelectedIndices(indices);
        }
    }
    applyThumbnailVisibility();
    updateWorkspaceActionVisibility();

    int newIndex = 0;
    if (!current.isEmpty()) {
        newIndex = m_files.indexOf(current);
        if (newIndex < 0) {
            newIndex = 0;
        }
    } else {
        // Jump to the first newly added image after sort
        newIndex = 0;
    }

    if (isWorkspaceMode()) {
        m_currentIndex = newIndex;
        if (m_metadataPanel && newIndex >= 0 && newIndex < m_files.size()) {
            m_metadataPanel->setImagePath(m_files.at(newIndex));
        }
        updateStatus();
        updateNavigationActions();
    } else {
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
        updateNavigationActions();
    }
}

void MainWindow::setCurrentIndex(int index)
{
    if (m_files.isEmpty() || index < 0 || index >= m_files.size()) {
        return;
    }
    if (index == m_currentIndex) {
        // Still refresh Image canvas if empty (e.g. after mode switch)
        if (isImageMode() && m_imageView && m_imageView->itemCount() == 0) {
            m_imageView->loadImage(m_files.at(m_currentIndex));
        }
        return;
    }

    m_currentIndex = index;
    const QString path = m_files.at(m_currentIndex);

    // DOMAIN: only Image mode replaces the single-image canvas.
    // Gallery/Workspace keep multi-object canvas; update session cursor only.
    // Gallery: do not exclusive-select (Ctrl+click multi-select is owned by the view).
    if (isImageMode()) {
        m_imageView->loadImage(path);
    } else if (isGalleryMode() && m_imageView) {
        m_imageView->revealGalleryPath(path);
    } else if (m_imageView) {
        m_imageView->focusSessionPath(path);
    }

    m_thumbnailBar->setCurrentIndex(m_currentIndex);
    if (m_metadataPanel) {
        m_metadataPanel->setImagePath(path);
    }
    updateWindowTitle();
    updateStatus();
    updateNavigationActions();
}

void MainWindow::removeSessionIndices(const QList<int> &indices)
{
    if (indices.isEmpty() || m_files.isEmpty()) {
        return;
    }

    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    QList<QPair<int, QString>> entries;
    for (int idx : sorted) {
        if (idx >= 0 && idx < m_files.size()) {
            entries.append(qMakePair(idx, m_files.at(idx)));
        }
    }
    if (entries.isEmpty()) {
        return;
    }

    if (m_imageView && m_imageView->undoStack() && !m_sessionUndoGuard) {
        // push() calls redo() → applySessionRemoveIndices
        m_imageView->undoStack()->push(new SessionRemoveCommand(this, entries));
        return;
    }
    applySessionRemoveIndices(sorted);
}

void MainWindow::applySessionRemoveIndices(const QList<int> &indices)
{
    if (indices.isEmpty() || m_files.isEmpty()) {
        return;
    }

    stopSlideshow();

    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    const QString currentPath = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                    ? m_files.at(m_currentIndex)
                                    : QString();

    // Remove highest indices first so remaining indices stay valid
    for (int i = sorted.size() - 1; i >= 0; --i) {
        const int idx = sorted.at(i);
        if (idx < 0 || idx >= m_files.size()) {
            continue;
        }
        const QString path = m_files.at(idx);
        m_files.removeAt(idx);
        // Drop canvas objects in Workspace and Gallery so layout switches do
        // not resurrect session-removed images from leftover items.
        if (m_imageView && (isWorkspaceMode() || isGalleryMode())) {
            m_imageView->removeWorkspacePath(path);
        }
    }

    m_thumbnailBar->setFiles(m_files);
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
    }
    applyThumbnailVisibility();

    if (m_files.isEmpty()) {
        m_currentIndex = -1;
        m_imageView->clearWorkspace();
        if (m_metadataPanel) {
            m_metadataPanel->clear();
        }
        updateWindowTitle();
        updateStatus();
        updateNavigationActions();
        return;
    }

    int newIndex = 0;
    if (!currentPath.isEmpty()) {
        newIndex = m_files.indexOf(currentPath);
    }
    if (newIndex < 0) {
        newIndex = qBound(0, sorted.first(), m_files.size() - 1);
    }

    if (isWorkspaceMode()) {
        m_currentIndex = newIndex;
        if (m_metadataPanel) {
            m_metadataPanel->setImagePath(m_files.at(newIndex));
        }
        m_thumbnailBar->setCurrentIndex(newIndex);
        updateStatus();
        updateNavigationActions();
    } else if (isGalleryMode()) {
        m_currentIndex = newIndex;
        m_thumbnailBar->setCurrentIndex(newIndex);
        if (m_metadataPanel) {
            m_metadataPanel->setImagePath(m_files.at(newIndex));
        }
        updateWindowTitle();
        updateStatus();
        updateNavigationActions();
        // Canvas already updated via removeWorkspacePath; no full repack needed.
    } else {
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
    }
}

void MainWindow::restoreSessionEntries(const QList<QPair<int, QString>> &entries)
{
    if (entries.isEmpty()) {
        return;
    }
    // Insert lowest index first so positions match the pre-remove session order.
    QList<QPair<int, QString>> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.first < b.first;
              });

    m_sessionUndoGuard = true;
    for (const auto &e : sorted) {
        const int idx = qBound(0, e.first, m_files.size());
        if (m_files.contains(e.second)) {
            continue;
        }
        m_files.insert(idx, e.second);
    }
    m_thumbnailBar->setFiles(m_files);
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
    }
    applyThumbnailVisibility();

    if (isGalleryMode() && m_imageView) {
        m_imageView->setWorkspacePaths(m_files);
    } else if (isWorkspaceMode() && m_imageView) {
        m_imageView->setWorkspacePaths(m_files);
    } else if (!m_files.isEmpty()) {
        m_currentIndex = -1;
        setCurrentIndex(qMin(m_currentIndex < 0 ? 0 : m_currentIndex, m_files.size() - 1));
    }
    updateWindowTitle();
    updateStatus();
    updateNavigationActions();
    m_sessionUndoGuard = false;
}

void MainWindow::removeSessionPaths(const QStringList &paths)
{
    if (paths.isEmpty() || m_files.isEmpty()) {
        return;
    }
    QList<int> indices;
    for (const QString &path : paths) {
        const int idx = m_files.indexOf(path);
        if (idx >= 0) {
            indices.append(idx);
        }
    }
    removeSessionIndices(indices);
}

void MainWindow::updateNavigationActions()
{
    const bool hasMany = m_files.size() > 1;
    const bool hasFiles = !m_files.isEmpty();
    const bool hasItem = m_imageView && m_imageView->itemCount() > 0;

    // Prev/Next are Image mode only. Slideshow may start from Gallery (enters
    // Image mode on start); still unavailable in Workspace.
    const bool imageNav = hasMany && m_imageView && m_imageView->isImageMode();
    const bool canSlideshow = hasMany && m_imageView && !m_imageView->isWorkspaceMode();
    m_previousAct->setEnabled(imageNav);
    m_nextAct->setEnabled(imageNav);
    if (m_firstAct) {
        m_firstAct->setEnabled(imageNav);
    }
    if (m_lastAct) {
        m_lastAct->setEnabled(imageNav);
    }
    m_slideshowAct->setEnabled(canSlideshow);
    if (m_slideshowAct) {
        if (canSlideshow) {
            m_slideshowAct->setStatusTip(tr("Start or stop the slideshow (Space)"));
        } else if (m_imageView && m_imageView->isWorkspaceMode()) {
            m_slideshowAct->setStatusTip(tr("Slideshow is not available in Workspace mode"));
        } else {
            m_slideshowAct->setStatusTip(tr("Open more than one image to use the slideshow"));
        }
    }
    if (m_imageView) {
        m_imageView->setImageModeNavigationEnabled(imageNav);
    }
    // Stop only when slideshow cannot run (Workspace or single file), not merely
    // because the user is browsing the Gallery with the action still available.
    if (!canSlideshow) {
        stopSlideshow();
    }

    // DOMAIN: rotate/flip only in Image or Workspace (not Gallery)
    const bool canTransform = hasItem && m_imageView
                              && !m_imageView->isGalleryMode();
    for (QAction *act : {m_rotateLeftAct, m_rotateRightAct, m_flipHAct, m_flipVAct}) {
        if (act) {
            act->setEnabled(canTransform);
        }
    }
    for (QAction *act : {m_resetScaleAct, m_resetRotationAct}) {
        if (act) {
            act->setEnabled(canTransform);
        }
    }

    // Zoom: Image / Workspace with content; Image with files loading also OK
    const bool canZoom = hasItem
                         || (m_imageView && m_imageView->isImageMode() && hasFiles);
    for (QAction *act : {m_zoomInAct, m_zoomOutAct, m_zoom1to1Act, m_zoomFitAct, m_zoomFillAct}) {
        if (act) {
            act->setEnabled(canZoom);
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
}

void MainWindow::onThumbnailActivated(int index)
{
    // User interaction: pause slideshow
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(index);
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

void MainWindow::goPrevious()
{
    if (m_files.size() <= 1) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    int idx = m_currentIndex - 1;
    if (idx < 0) {
        idx = m_files.size() - 1;
    }
    setCurrentIndex(idx);
}

void MainWindow::goNext()
{
    if (m_files.size() <= 1) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    int idx = m_currentIndex + 1;
    if (idx >= m_files.size()) {
        idx = 0;
    }
    setCurrentIndex(idx);
}

void MainWindow::goFirst()
{
    if (m_files.isEmpty()) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(0);
}

void MainWindow::goLast()
{
    if (m_files.isEmpty()) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(m_files.size() - 1);
}

void MainWindow::setSlideshowIntervalMs(int ms)
{
    m_slideshowIntervalMs = qBound(200, ms, 60000);
    if (m_slideshowTimer->isActive()) {
        m_slideshowTimer->setInterval(m_slideshowIntervalMs);
    }
}

void MainWindow::slideshowFaster()
{
    // mpv ]: higher playback speed → shorter dwell per slide
    const int next = qMax(200, int(qRound(m_slideshowIntervalMs / 1.25)));
    if (next == m_slideshowIntervalMs && m_slideshowIntervalMs <= 200) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Slideshow already at minimum interval (0.2 s)"), 2000);
        }
        return;
    }
    setSlideshowIntervalMs(next);
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Slideshow interval: %1 s").arg(m_slideshowIntervalMs / 1000.0, 0, 'f', 2), 2000);
    }
}

void MainWindow::slideshowSlower()
{
    // mpv [: lower playback speed → longer dwell per slide
    const int next = qMin(60000, int(qRound(m_slideshowIntervalMs * 1.25)));
    if (next == m_slideshowIntervalMs && m_slideshowIntervalMs >= 60000) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Slideshow already at maximum interval (60 s)"), 2000);
        }
        return;
    }
    setSlideshowIntervalMs(next);
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Slideshow interval: %1 s").arg(m_slideshowIntervalMs / 1000.0, 0, 'f', 2), 2000);
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
    if (!m_slideshowTimer || !m_slideshowTimer->isActive()) {
        return;
    }
    if (!m_slideshowCursorHidden) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
        m_slideshowCursorHidden = true;
    }
}

void MainWindow::armSlideshowCursorHide()
{
    if (!m_cursorHideTimer) {
        return;
    }
    showSlideshowCursor();
    m_cursorHideTimer->start();
}

void MainWindow::startSlideshow()
{
    if (m_files.size() <= 1 || isWorkspaceMode()) {
        m_slideshowAct->setChecked(false);
        return;
    }
    // Gallery: open the focused session image in Image mode, then advance.
    if (isGalleryMode()) {
        QString path;
        if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
            path = m_files.at(m_currentIndex);
        } else if (!m_files.isEmpty()) {
            path = m_files.first();
        }
        if (path.isEmpty()) {
            m_slideshowAct->setChecked(false);
            return;
        }
        showPathInImageMode(path);
    }
    if (m_slideshowFullscreen && !isFullScreen()) {
        showFullScreen();
    }
    m_slideshowTimer->start(m_slideshowIntervalMs);
    m_slideshowAct->setChecked(true);
    m_slideshowAct->setText(tr("Stop &Slideshow"));
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-stop"), QStyle::SP_MediaStop));
    qApp->installEventFilter(this);
    armSlideshowCursorHide();
    {
        const double sec = m_slideshowIntervalMs / 1000.0;
        m_imageView->flashHud(tr("▶  Slideshow"),
                              tr("%1 s").arg(sec, 0, 'f', sec >= 10.0 ? 0 : 1));
    }
}

void MainWindow::stopSlideshow()
{
    // Only announce when a running slideshow is actually stopped. Silent no-ops
    // (mode switches, navigation while idle) must not flash "Stop" on the HUD.
    const bool wasRunning = m_slideshowTimer && m_slideshowTimer->isActive();

    if (m_slideshowTimer) {
        m_slideshowTimer->stop();
    }
    if (m_cursorHideTimer) {
        m_cursorHideTimer->stop();
    }
    showSlideshowCursor();
    qApp->removeEventFilter(this);
    if (m_slideshowAct) {
        m_slideshowAct->setChecked(false);
        m_slideshowAct->setText(tr("Play &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
    }
    if (wasRunning && m_imageView) {
        m_imageView->flashHud(tr("■  Slideshow stopped"));
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (m_slideshowTimer && m_slideshowTimer->isActive()) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::HoverMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel:
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
    if (m_slideshowTimer->isActive()) {
        stopSlideshow();
    } else {
        startSlideshow();
    }
}

