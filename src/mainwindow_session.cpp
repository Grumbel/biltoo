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

} // namespace

bool MainWindow::isImageFile(const QString &path)
{
    return ImageLoader::isImageFile(path);
}

QStringList MainWindow::expandPaths(const QStringList &paths) const
{
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
                    images.append(full);
                }
            }
        } else if (info.isFile() && isImageFile(path)) {
            images.append(path);
        }
    }
    return images;
}

void MainWindow::sortFileList()
{
    if (m_files.size() <= 1) {
        return;
    }

    if (m_sortMode == SortMode::MTime) {
        std::stable_sort(m_files.begin(), m_files.end(), [](const QString &a, const QString &b) {
            const QFileInfo fa(a), fb(b);
            if (fa.lastModified() != fb.lastModified()) {
                return fa.lastModified() < fb.lastModified();
            }
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });
    } else {
        // Natural (numeric-aware) case-insensitive sort by file name
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(m_files.begin(), m_files.end(),
                         [&collator](const QString &a, const QString &b) {
                             return collator.compare(QFileInfo(a).fileName(),
                                                     QFileInfo(b).fileName()) < 0;
                         });
    }
}

void MainWindow::setSortMode(SortMode mode)
{
    m_sortMode = mode;
    if (mode == SortMode::Name) {
        m_sortNameAct->setChecked(true);
    } else {
        m_sortMTimeAct->setChecked(true);
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
        // setFiles rebuilds the list; restore multi-select and canvas selection
        m_thumbnailBar->setWorkspaceMode(true);
        syncThumbnailWorkspaceSelection();
    }

    int newIndex = 0;
    if (!current.isEmpty()) {
        newIndex = m_files.indexOf(current);
        if (newIndex < 0) {
            newIndex = 0;
        }
    }
    m_currentIndex = -1; // force reload
    setCurrentIndex(newIndex);
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

void MainWindow::appendFiles(const QStringList &paths)
{
    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
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
        m_thumbnailBar->setWorkspaceMode(true);
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
        return;
    }

    m_currentIndex = index;
    m_imageView->loadImage(m_files.at(m_currentIndex));
    m_thumbnailBar->setCurrentIndex(m_currentIndex);
    if (m_metadataPanel) {
        m_metadataPanel->setImagePath(m_files.at(m_currentIndex));
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
        if (isWorkspaceMode() && m_imageView) {
            m_imageView->removeWorkspacePath(path);
        }
    }

    m_thumbnailBar->setFiles(m_files);
    if (isWorkspaceMode()) {
        m_thumbnailBar->setWorkspaceMode(true);
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
        // Prefer the nearest index after the first removed slot
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
    } else {
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
    }
}

void MainWindow::updateNavigationActions()
{
    const bool hasMany = m_files.size() > 1;
    const bool hasFiles = !m_files.isEmpty();
    const bool hasItem = m_imageView && m_imageView->itemCount() > 0;

    // Session navigation is Image-mode oriented; disable in workspace mode
    // Prev/Next/slideshow are Image mode only (not Gallery, not Workspace).
    const bool imageNav = hasMany && m_imageView && m_imageView->isImageMode();
    m_previousAct->setEnabled(imageNav);
    m_nextAct->setEnabled(imageNav);
    if (m_firstAct) {
        m_firstAct->setEnabled(imageNav);
    }
    if (m_lastAct) {
        m_lastAct->setEnabled(imageNav);
    }
    m_slideshowAct->setEnabled(imageNav);
    if (m_imageView) {
        m_imageView->setImageModeNavigationEnabled(imageNav);
    }
    if (!imageNav) {
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
    m_slideshowIntervalMs = qMax(500, ms);
    if (m_slideshowTimer->isActive()) {
        m_slideshowTimer->setInterval(m_slideshowIntervalMs);
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

