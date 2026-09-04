// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"
#include "projectfile.h"
#include "archivepath.h"
#include "archivereader.h"
#include "workspacebackgrounddialog.h"
#include "imageitem.h"

#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

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
    // First filter is the dialog default — include archives so .zip/.tar are visible.
    return QObject::tr(
               "Images and archives (%1 %2);;Images only (%1);;Archives only (%2);;All Files (*)")
        .arg(images, archives);
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
        if (m_mw) {
            QList<int> indices;
            for (const auto &e : m_entries) {
                indices.append(e.index);
            }
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
        // Already an archive member reference — keep as-is when valid.
        if (ArchivePath::isArchiveRef(path)) {
            if (isImageFile(path)) {
                const ArchivePath::Ref ref = ArchivePath::parse(path);
                if (ref.valid) {
                    images.append(ArchivePath::makeRef(ref.archivePath, ref.memberPath));
                }
            }
            continue;
        }
        const QFileInfo info(path);
        if (info.isDir()) {
            QDir::Filters filters = QDir::Files | QDir::Readable | QDir::NoDotAndDotDot;
            QDirIterator::IteratorFlags flags = m_recursive
                ? QDirIterator::Subdirectories
                : QDirIterator::NoIteratorFlags;
            QDirIterator it(path, filters, flags);
            while (it.hasNext()) {
                const QString full = it.next();
                if (ArchivePath::isArchiveFile(full) && ArchiveReader::isAvailable()) {
                    images.append(ArchiveReader::expandArchiveToImageRefs(full));
                } else if (isImageFile(full)) {
                    const QString c = canonicalImage(full);
                    if (!c.isEmpty()) {
                        images.append(c);
                    }
                }
            }
        } else if (info.isFile() && ArchivePath::isArchiveFile(path)
                   && ArchiveReader::isAvailable()) {
            images.append(ArchiveReader::expandArchiveToImageRefs(path));
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
    if (m_session.paths().size() <= 1) {
        return;
    }
    // Ensure parallel id vector matches (legacy / partial updates).
    m_session.ensureIdsAligned();

    auto nameLess = [](const QString &a, const QString &b) {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        return collator.compare(ArchivePath::displayName(a), ArchivePath::displayName(b)) < 0;
    };

    auto imageSize = [](const QString &path) {
        return ImageLoader::probeSize(path); // header-only when possible
    };

    // Sort indices so path list and stable ids stay aligned.
    QVector<int> order(m_session.paths().size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    auto pathAt = [&](int i) -> const QString & { return m_session.paths().at(i); };

    switch (m_sortMode) {
    case SortMode::MTime:
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const QFileInfo fa(pathAt(ia)), fb(pathAt(ib));
            if (fa.lastModified() != fb.lastModified()) {
                return fa.lastModified() < fb.lastModified();
            }
            return nameLess(pathAt(ia), pathAt(ib));
        });
        break;
    case SortMode::FileSize:
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const qint64 sa = QFileInfo(pathAt(ia)).size();
            const qint64 sb = QFileInfo(pathAt(ib)).size();
            if (sa != sb) {
                return sa < sb;
            }
            return nameLess(pathAt(ia), pathAt(ib));
        });
        break;
    case SortMode::Width:
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const int wa = imageSize(pathAt(ia)).width();
            const int wb = imageSize(pathAt(ib)).width();
            if (wa != wb) {
                return wa < wb;
            }
            return nameLess(pathAt(ia), pathAt(ib));
        });
        break;
    case SortMode::Height:
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const int ha = imageSize(pathAt(ia)).height();
            const int hb = imageSize(pathAt(ib)).height();
            if (ha != hb) {
                return ha < hb;
            }
            return nameLess(pathAt(ia), pathAt(ib));
        });
        break;
    case SortMode::PixelCount:
        std::stable_sort(order.begin(), order.end(), [&](int ia, int ib) {
            const QSize sa = imageSize(pathAt(ia));
            const QSize sb = imageSize(pathAt(ib));
            const qint64 pa = qint64(sa.width()) * sa.height();
            const qint64 pb = qint64(sb.width()) * sb.height();
            if (pa != pb) {
                return pa < pb;
            }
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

    if (m_session.paths().isEmpty()) {
        return;
    }

    const QString current = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                                ? m_session.paths().at(m_currentIndex)
                                : QString();
    sortFileList();
    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
    }

    int newIndex = 0;
    if (!current.isEmpty()) {
        newIndex = m_session.paths().indexOf(current);
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
        m_imageView->reorderItemsByPaths(m_session.paths());
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

    m_session.setPaths(images);
    m_session.validateUniqueIds("loadFiles");
    sortFileList();
    m_currentIndex = -1;

    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    applyThumbnailVisibility();

    // Session open is not a Workspace document. Drop any free-form arrangement
    // so Image↔Workspace does not resurrect previous tiles; only .qimgview
    // projects restore Workspace content.
    if (m_imageView) {
        m_imageView->discardStashedGallery();
        m_imageView->discardStashedWorkspace();
        m_imageView->clearDurableWorkspaceSnapshot();
        if (isWorkspaceMode()) {
            m_imageView->clearWorkspace();
        }
    }
    m_workspaceReturnActive = false;
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }

    int idx = startAt;
    if (idx < 0 || idx >= m_session.paths().size()) {
        idx = 0;
    }

    // Multi-file sessions open in Gallery (browse the set). A single file
    // stays in Image mode. Workspace is never auto-seeded from the session.
    if (m_session.paths().size() > 1) {
        enterGalleryMode(ImageView::LayoutMode::Masonry);
        setCurrentIndex(idx, /*ensureGalleryVisible=*/true);
    } else {
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
    rememberSessionHistory(m_session.paths());
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
    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("No readable images to add."), 5000);
        }
        return;
    }

    const QString current = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
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

    // Paths currently on the workspace (selection must be restored after setFiles)
    const QStringList workspacePaths = isWorkspaceMode() ? m_imageView->itemPaths() : QStringList();

    sortFileList();
    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    if (isWorkspaceMode()) {
        // setFiles rebuilds items; re-apply multi-select mode and canvas selection
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
        // If the canvas was empty, selection may still be empty — restore paths we had
        if (m_thumbnailBar->selectedIndices().isEmpty() && !workspacePaths.isEmpty()) {
            QList<int> indices;
            for (const QString &path : workspacePaths) {
                const int idx = m_session.paths().indexOf(path);
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
        newIndex = m_session.paths().indexOf(current);
        if (newIndex < 0) {
            newIndex = 0;
        }
    } else {
        // Jump to the first newly added image after sort
        newIndex = 0;
    }

    if (isWorkspaceMode()) {
        m_currentIndex = newIndex;
        if (m_metadataPanel) {
            m_metadataPath.clear();
        }
        updateStatus();
        updateNavigationActions();
    } else {
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
        updateNavigationActions();
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

void MainWindow::setCurrentIndex(int index, bool ensureGalleryVisible)
{
    if (m_session.paths().isEmpty() || index < 0 || index >= m_session.paths().size()) {
        return;
    }
    if (index == m_currentIndex) {
        // Still refresh Image canvas after a mode switch that cleared the live
        // item, or if classicPath was left pointing at a different file.
        if (isImageMode() && m_imageView) {
            const QString path = m_session.paths().at(m_currentIndex);
            if (m_imageView->itemCount() == 0
                || m_imageView->classicPath() != path) {
                m_imageView->loadImage(path);
            }
        }
        return;
    }
    if (m_imageView && m_imageView->isCropMode()) {
        m_imageView->cancelCrop();
    }

    m_currentIndex = index;
    const QString path = m_session.paths().at(m_currentIndex);

    // Publish session cursor before decode so Image-mode items bind the correct
    // sessionIndex (crop/flip sync to the matching Workspace slot).
    // Slideshow auto-advance must not pulse filename/index (only user nav or pinned HUD).
    if (m_imageView) {
        m_imageView->setSessionPosition(m_currentIndex, m_session.paths().size(),
                                        !m_slideshowAdvancing);
        m_imageView->setCurrentSessionId(currentSessionId());
    }

    // DOMAIN: only Image mode replaces the single-image canvas.
    // Gallery/Workspace keep multi-object canvas; update session cursor only.
    // Gallery: do not exclusive-select (Ctrl+click multi-select is owned by the view).
    if (isImageMode()) {
        m_imageView->loadImage(path);
    } else if (isGalleryMode() && m_imageView) {
        // Keyboard / programmatic nav may ask to scroll; mouse selection does not.
        if (ensureGalleryVisible) {
            m_imageView->revealGalleryPath(path);
        }
    } else if (m_imageView) {
        m_imageView->focusSessionPath(path);
    }

    m_thumbnailBar->setCurrentIndex(m_currentIndex);
    // Metadata refresh is gated on dock visibility inside updateMetadataPanel
    // (called from updateStatus). Force a path invalidation so a later dock
    // open still reloads for this selection.
    if (m_metadataPanel) {
        m_metadataPath.clear();
    }
    updateWindowTitle();
    updateStatus();
    updateNavigationActions();
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

    if (m_imageView && m_imageView->undoStack() && !m_sessionUndoGuard) {
        // push() calls redo() → applySessionRemoveIndices
        m_imageView->undoStack()->push(new SessionRemoveCommand(this, entries));
        return;
    }
    applySessionRemoveIndices(sorted);
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

    const QString currentPath = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                                    ? m_session.paths().at(m_currentIndex)
                                    : QString();

    // Remove highest indices first so remaining indices stay valid
    if (m_imageView) {
        m_imageView->setPreserveUndoOnDestroy(true);
        // Thumb-strip setFiles can resize the splitter → scrollbar range rebuild.
        // Snapshot once; each remove pins sceneRect; reassert after the churn.
        if (isGalleryMode()) {
            m_imageView->snapshotGalleryViewport();
            m_imageView->setGalleryRelayoutSuppressed(true);
        }
    }
    for (int i = sorted.size() - 1; i >= 0; --i) {
        const int idx = sorted.at(i);
        if (idx < 0 || idx >= m_session.paths().size()) {
            continue;
        }
        const QString path = m_session.pathAt(idx);
        const SessionImageId sid = m_session.idAt(idx);
        m_session.removeAt(idx);
        // Drop the canvas object for this session image only (not every path match).
        if (m_imageView && sid != kInvalidSessionImageId
            && (isWorkspaceMode() || isGalleryMode())) {
            m_imageView->removeWorkspaceSessionId(sid);
        }
    }
    if (m_imageView) {
        m_imageView->setPreserveUndoOnDestroy(false);
    }

    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    if (isWorkspaceMode()) {
        m_thumbnailBar->setMultiSelectEnabled(true);
        syncThumbnailWorkspaceSelection();
    }
    applyThumbnailVisibility();
    // Gallery suppress stays on until the end of this function (and one more
    // event-loop tick) so setCurrentIndex / status updates cannot repack.

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
            m_imageView->setGalleryRelayoutSuppressed(false);
        }
        return;
    }

    int newIndex = 0;
    if (!currentPath.isEmpty()) {
        newIndex = m_session.paths().indexOf(currentPath);
    }
    if (newIndex < 0) {
        newIndex = qBound(0, sorted.first(), m_session.paths().size() - 1);
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
        m_imageView->reassertGalleryViewport();
        // Release suppress and reassert again after splitter/layout events.
        QTimer::singleShot(0, m_imageView, [v = m_imageView]() {
            if (!v) {
                return;
            }
            v->reassertGalleryViewport();
            v->setGalleryRelayoutSuppressed(false);
        });
    }
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
        const int idx = qBound(0, e.index, m_session.size());
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
    // Path-only fallback (first match per path). Prefer removeSessionIds.
    if (paths.isEmpty() || m_session.isEmpty()) {
        return;
    }
    QList<int> indices;
    for (const QString &path : paths) {
        const int idx = m_session.paths().indexOf(path);
        if (idx >= 0) {
            indices.append(idx);
        }
    }
    removeSessionIndices(indices);
}

void MainWindow::updateNavigationActions()
{
    const bool hasMany = m_session.paths().size() > 1;
    const bool hasFiles = !m_session.paths().isEmpty();
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

    // Rotate/flip: Image (current), Workspace (selection), Gallery (selection)
    const bool canTransform = m_imageView && m_imageView->hasTransformTargets();
    for (QAction *act : {m_rotateLeftAct, m_rotateRightAct, m_flipHAct, m_flipVAct}) {
        if (act) {
            act->setEnabled(canTransform);
        }
    }
    // Crop: Image mode, or exactly one Gallery/Workspace selection.
    if (m_cropAct) {
        const bool canCrop = m_imageView && m_imageView->hasSingleCropTarget();
        m_cropAct->setEnabled(canCrop);
        if (!canCrop && m_imageView && m_imageView->isCropMode()) {
            m_imageView->cancelCrop();
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

    // Zoom: Image / Workspace with content; Image with files loading also OK
    const bool canZoom = hasItem
                         || (m_imageView && m_imageView->isImageMode() && hasFiles);
    for (QAction *act : {m_zoomInAct, m_zoomOutAct, m_zoom1to1Act, m_zoomFitAct, m_zoomFillAct,
                         m_zoomRegionAct}) {
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
    if (m_openSelectionNewWindowAct) {
        const bool canOpenSel = m_imageView && !m_imageView->selectedPaths().isEmpty();
        m_openSelectionNewWindowAct->setEnabled(canOpenSel);
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

void MainWindow::goPrevious()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    int idx = m_currentIndex - 1;
    if (idx < 0) {
        idx = m_session.paths().size() - 1;
    }
    setCurrentIndex(idx);
}

void MainWindow::goNext()
{
    if (m_session.paths().size() <= 1) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    int idx = m_currentIndex + 1;
    if (idx >= m_session.paths().size()) {
        idx = 0;
    }
    setCurrentIndex(idx);
}

void MainWindow::goFirst()
{
    if (m_session.paths().isEmpty()) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(0);
}

void MainWindow::goLast()
{
    if (m_session.paths().isEmpty()) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(m_session.paths().size() - 1);
}

void MainWindow::setSlideshowIntervalMs(int ms)
{
    // 0 ms = as fast as the event loop allows; upper bound keeps UI usable.
    m_slideshowIntervalMs = qBound(0, ms, 60000);
    // Transition must finish before the next dwell; keep it ≤ half interval.
    if (m_imageView) {
        const int half = m_slideshowIntervalMs / 2;
        const int tr = m_imageView->slideshowTransitionDurationMs();
        if (half >= 0 && tr > half) {
            m_imageView->setSlideshowTransitionDurationMs(half);
        }
    }
    if (m_slideshowTimer && m_slideshowTimer->isActive()) {
        // Restart so the current dwell and the HUD progress line match the new
        // interval instead of keeping a stale remaining time.
        m_slideshowTimer->start(m_slideshowIntervalMs);
        if (m_imageView) {
            m_imageView->setSlideshowProgress(true, m_slideshowIntervalMs);
            m_imageView->reapplySlideshowFraming();
        }
    }
}

namespace {

/** Human-readable slideshow dwell for HUD / status line. */
QString formatSlideshowInterval(int ms)
{
    if (ms <= 0) {
        return QCoreApplication::translate("MainWindow", "0 ms (max speed)");
    }
    if (ms < 1000) {
        return QCoreApplication::translate("MainWindow", "%1 ms").arg(ms);
    }
    const double sec = ms / 1000.0;
    // Whole seconds when exact; one decimal otherwise.
    if (ms % 1000 == 0) {
        return QCoreApplication::translate("MainWindow", "%1 s").arg(ms / 1000);
    }
    return QCoreApplication::translate("MainWindow", "%1 s").arg(sec, 0, 'f', 1);
}

/**
 * Adaptive step toward a shorter interval (faster slideshow).
 * Fine additive steps near zero; coarser / multiplicative at longer dwells.
 */
int slideshowIntervalFaster(int ms)
{
    if (ms <= 0) {
        return 0;
    }
    if (ms <= 50) {
        return qMax(0, ms - 10);
    }
    if (ms <= 200) {
        return qMax(0, ms - 25);
    }
    if (ms <= 1000) {
        return qMax(0, ms - 100);
    }
    if (ms <= 5000) {
        return qMax(1000, int(qRound(ms / 1.25)));
    }
    return qMax(5000, int(qRound(ms / 1.25)));
}

/**
 * Adaptive step toward a longer interval (slower slideshow).
 */
int slideshowIntervalSlower(int ms)
{
    if (ms < 50) {
        return ms + 10;
    }
    if (ms < 200) {
        return ms + 25;
    }
    if (ms < 1000) {
        return ms + 100;
    }
    if (ms < 5000) {
        const int next = int(qRound(ms * 1.25));
        return qMin(5000, qMax(ms + 1, next));
    }
    const int next = int(qRound(ms * 1.25));
    return qMin(60000, qMax(ms + 1, next));
}

} // namespace

void MainWindow::slideshowFaster()
{
    // mpv ]: higher playback speed → shorter dwell per slide
    const int next = slideshowIntervalFaster(m_slideshowIntervalMs);
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
    const int next = slideshowIntervalSlower(m_slideshowIntervalMs);
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
    if (!m_cursorHideTimer || !m_slideshowTimer || !m_slideshowTimer->isActive()) {
        return;
    }
    // Show on activity, then hide after 1 s of inactivity (timer interval).
    showSlideshowCursor();
    m_cursorHideTimer->start();
}

void MainWindow::startSlideshow()
{
    if (m_session.paths().size() <= 1 || isWorkspaceMode()) {
        m_slideshowAct->setChecked(false);
        return;
    }
    // Gallery: open the focused session image in Image mode, then advance.
    if (isGalleryMode()) {
        QString path;
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            path = m_session.paths().at(m_currentIndex);
        } else if (!m_session.paths().isEmpty()) {
            path = m_session.paths().first();
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
        m_imageView->setSlideshowProgress(true, m_slideshowIntervalMs);
        // Frame the current (already loaded) slide: zoom mode when motion is
        // off, cover + camera when motion is on. maybeStart alone skipped
        // zoom mode and left slide 0 on ordinary Image-mode fit.
        m_imageView->reapplySlideshowFraming();
        m_imageView->flashHud(tr("▶  Slideshow"),
                              formatSlideshowInterval(m_slideshowIntervalMs));
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
    if (m_imageView) {
        m_imageView->cancelSlideshowTransition();
        m_imageView->cancelSlideshowMotion();
    }
    showSlideshowCursor();
    qApp->removeEventFilter(this);
    if (m_slideshowAct) {
        m_slideshowAct->setChecked(false);
        m_slideshowAct->setText(tr("Play &Slideshow"));
        m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
    }
    if (m_imageView) {
        m_imageView->setSlideshowProgress(false);
        if (wasRunning) {
            m_imageView->flashHud(tr("■  Slideshow stopped"));
        }
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
    if (m_slideshowTimer->isActive()) {
        stopSlideshow();
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
        return ArchivePath::displayName(paths.first());
    }
    if (sameDir) {
        // Prefer archive filename when all members share one container.
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
        .arg(ArchivePath::displayName(paths.first()));
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
        const QString abs = ArchivePath::canonicalSessionPath(p);
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
        act->setStatusTip(entry.size() <= 3
                              ? entry.join(QStringLiteral(", "))
                              : tr("%1 paths").arg(entry.size()));
        connect(act, &QAction::triggered, this, &MainWindow::openHistoryEntry);
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
    if (m_imageView->undoStack() && !m_sessionUndoGuard) {
        m_imageView->undoStack()->push(
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
    m_imageView->duplicateSelected();

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
    // Copies are the only selected items (duplicateSelected cleared + selected them).
    m_session.validateUniqueIds("applyDuplicate");
    m_imageView->bindSelectedSessionIndices(firstNew);
    m_imageView->bindSelectedSessionIds(newIds.toList());
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
        m_imageView->applyLayout(GalleryPackReason::SessionMutate);
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

const char kWorkspaceClipMime[] = "application/x-qimgview-workspace-items";

QByteArray encodeWorkspaceClipboard(const QList<WorkspaceItemState> &items)
{
    QJsonArray arr;
    for (const WorkspaceItemState &s : items) {
        QJsonObject o = ProjectFile::appearanceToJson(s, /*includePose=*/true);
        o.insert(QStringLiteral("path"), s.path);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("qimgview-workspace-clipboard"));
    root.insert(QStringLiteral("version"), 1);
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
        != QLatin1String("qimgview-workspace-clipboard")) {
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
    if (m_imageView->undoStack() && !m_sessionUndoGuard) {
        m_imageView->undoStack()->push(new WorkspaceCutCommand(this, items));
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
    if (m_imageView->undoStack() && !m_sessionUndoGuard) {
        m_imageView->undoStack()->push(new WorkspacePasteCommand(this, items));
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
        tr("QImgView Project (*.qimgview);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString out = path;
    if (!out.endsWith(QLatin1String(".qimgview"), Qt::CaseInsensitive)) {
        out += QStringLiteral(".qimgview");
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
        tr("QImgView Project (*.qimgview);;All Files (*)"));
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

bool MainWindow::writeProjectToPath(const QString &projectPath, QString *error)
{
    ProjectDocument doc;
    doc.version = 1;
    if (isWorkspaceMode()) {
        doc.mode = QStringLiteral("workspace");
    } else if (isGalleryMode()) {
        doc.mode = QStringLiteral("gallery");
    } else {
        doc.mode = QStringLiteral("image");
    }

    QHash<QString, QString> pathToSha; // absolute path → sha256
    const QFileInfo projInfo(projectPath);
    const QDir projDir = projInfo.absoluteDir();

    auto ensureAsset = [&](const QString &sessionPath) -> QString {
        if (sessionPath.isEmpty()) {
            return {};
        }
        // Archive members: hash the container file, not the virtual member path.
        QString hashPath = sessionPath;
        if (ArchivePath::isArchiveRef(sessionPath)) {
            hashPath = ArchivePath::archiveFilePath(sessionPath);
        }
        if (hashPath.isEmpty()) {
            return {};
        }
        if (pathToSha.contains(hashPath)) {
            return pathToSha.value(hashPath);
        }
        const QString sha = ProjectFile::fileSha256(hashPath);
        if (sha.isEmpty()) {
            return {};
        }
        pathToSha.insert(hashPath, sha);
        ProjectAsset a;
        a.sha256 = sha;
        a.path = hashPath;
        const QString rel = projDir.relativeFilePath(hashPath);
        if (!rel.startsWith(QLatin1String(".."))) {
            a.pathRelative = rel;
        }
        doc.assets.append(a);
        return sha;
    };

    // Workspace poses from live tiles (session-id keyed).
    QHash<SessionImageId, WorkspaceItemState> poses;
    if (m_imageView) {
        for (ImageItem *item : m_imageView->liveItems()) {
            if (!item || item->sessionId() == kInvalidSessionImageId) {
                continue;
            }
            poses.insert(item->sessionId(), m_imageView->captureState(item));
        }
    }

    for (int i = 0; i < m_session.size(); ++i) {
        const QString path = m_session.pathAt(i);
        const SessionImageId id = m_session.idAt(i);
        const QString sha = ensureAsset(path);
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
        // Archive members need the full ref stored; asset is only the container.
        if (ArchivePath::isArchiveRef(path)) {
            im.hasAppearance = true;
        }
        if (poses.contains(id)) {
            const WorkspaceItemState &p = poses.value(id);
            im.appearance.pos = p.pos;
            im.appearance.scale = p.scale;
            im.appearance.scaleY = p.scaleY;
            im.appearance.shear = p.shear;
            im.appearance.rotation = p.rotation;
            im.appearance.opacity = p.opacity;
            im.appearance.z = p.z;
            im.appearance.hFlip = p.hFlip;
            im.appearance.vFlip = p.vFlip;
            im.hasWorkspacePose = true;
            im.hasAppearance = true;
        }
        im.appearance.path = path;
        im.appearance.sessionId = id;
        doc.images.append(im);
    }

    if (m_imageView) {
        WorkspaceBackground wb = m_imageView->workspaceBackground();
        if (!wb.isAppDefault()) {
            // JSON only: absolute path, optional path relative to the project
            // file, and SHA-256 checksum. Never copy tile bytes into a side folder.
            if (wb.mode == WorkspaceBackgroundMode::ImageTile
                && !wb.imagePath.isEmpty()) {
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
            doc.hasWorkspaceBackground = true;
            doc.workspaceBackground = wb;
        }
    }

    return ProjectFile::save(projectPath, doc, error);
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

    QHash<QString, ProjectAsset> assetsBySha;
    for (const ProjectAsset &a : doc.assets) {
        assetsBySha.insert(a.sha256.toLower(), a);
    }

    QStringList paths;
    QVector<SessionImageId> ids;
    // Parallel to paths/ids: appearance and pose payloads before id finalization.
    QVector<WorkspaceItemState> appearanceByRow;
    QVector<bool> rowHasAppearance;
    QVector<bool> rowHasPose;
    QStringList missing;
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
                skipAllMissing = true;
            } else if (choice == QMessageBox::Yes) {
                const QString picked = QFileDialog::getOpenFileName(
                    this,
                    tr("Locate image"),
                    QFileInfo(hint).absolutePath().isEmpty()
                        ? QFileInfo(projectPath).absolutePath()
                        : QFileInfo(hint).absolutePath(),
                    tr("Images (*.png *.jpg *.jpeg *.webp *.tif *.tiff *.bmp *.gif);;All Files (*)"));
                if (!picked.isEmpty()) {
                    bool acceptPicked = true;
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
                            acceptPicked = (useAnyway == QMessageBox::Yes);
                        }
                    }
                    if (acceptPicked) {
                        resolved = picked;
                    }
                }
            }
        }
        if (resolved.isEmpty()) {
            missing.append(resolveErr.isEmpty() ? im.assetSha256.left(12) : resolveErr);
            continue;
        }
        // Asset resolves to a filesystem file (image or archive container).
        // Archive members keep their member path from the stored appearance ref.
        QString sessionPath = resolved;
        if (im.hasAppearance && ArchivePath::isArchiveRef(im.appearance.path)) {
            const ArchivePath::Ref ref = ArchivePath::parse(im.appearance.path);
            if (ref.valid) {
                sessionPath = ArchivePath::makeRef(resolved, ref.memberPath);
            }
        }
        paths.append(sessionPath);
        const SessionImageId id =
            im.id != kInvalidSessionImageId ? im.id : kInvalidSessionImageId;
        ids.append(id);

        WorkspaceItemState st = im.appearance;
        st.path = sessionPath;
        st.sessionId = id;
        appearanceByRow.append(st);
        rowHasAppearance.append(im.hasAppearance || im.hasWorkspacePose);
        rowHasPose.append(im.hasWorkspacePose);
    }

    if (paths.isEmpty()) {
        if (error) {
            *error = tr("No images could be resolved from the project.");
        }
        return false;
    }

    stopSlideshow();
    // Replace session with preserved ids where possible.
    QVector<SessionImageId> finalIds = ids;
    for (int i = 0; i < finalIds.size(); ++i) {
        if (finalIds.at(i) == kInvalidSessionImageId) {
            finalIds[i] = m_session.allocId();
        }
        // Keep appearance rows keyed to the final session id.
        appearanceByRow[i].sessionId = finalIds.at(i);
    }
    m_session.clear();
    m_session.replaceAll(paths, finalIds);
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
        m_imageView->appearance().clear();
        for (int i = 0; i < appearanceByRow.size(); ++i) {
            if (!rowHasAppearance.at(i)) {
                continue;
            }
            const SessionImageId sid = finalIds.at(i);
            if (sid == kInvalidSessionImageId) {
                continue;
            }
            m_imageView->setSessionAppearance(sid, appearanceByRow.at(i));
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

    if (m_thumbnailBar) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
    }
    applyThumbnailVisibility();

    // Workspace canvas is a subset of the session: only images with a saved
    // pose (hasWorkspacePose) belong on the canvas. Gallery still shows all.
    const bool wantWorkspace =
        (doc.mode == QLatin1String("workspace")) || poseCount > 0;
    if (wantWorkspace) {
        if (!isWorkspaceMode()) {
            enterWorkspaceMode();
        }
        if (m_imageView) {
            // Pose (pos/scale/shear/rotation/opacity/z) is already in m_appearance.
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
                const SessionImageId sid = finalIds.at(i);
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
            enterGalleryMode(ImageView::LayoutMode::Masonry);
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

    updateWindowTitle();
    updateWorkspaceActionVisibility();
    if (!missing.isEmpty() && statusBar()) {
        statusBar()->showMessage(
            tr("Project loaded with %n missing image(s).", "", missing.size()), 8000);
    }
    m_workspaceDirty = false;
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
        // Show filename; full path in status tip. Missing files stay listed but open will fail gracefully.
        QAction *act = m_recentProjectsMenu->addAction(
            QStringLiteral("%1. %2").arg(i + 1).arg(fi.fileName()));
        act->setData(p);
        act->setStatusTip(p);
        if (!fi.isFile()) {
            act->setEnabled(false);
            act->setText(act->text() + tr(" (missing)"));
        }
        connect(act, &QAction::triggered, this, &MainWindow::openRecentProject);
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
    const WorkspaceBackground before = m_imageView->workspaceBackground();
    WorkspaceBackgroundDialog dlg(this);
    dlg.setAppDefaultColors(
        m_imageView->backgroundColor(),
        m_imageView->backgroundColorAlt(),
        m_imageView->backgroundPattern() == ImageView::BackgroundPattern::Checkerboard);
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
    if (m_imageView->undoStack() && !m_sessionUndoGuard) {
        m_imageView->undoStack()->push(
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

void MainWindow::workspaceBackgroundDefault(bool checked)
{
    if (!m_imageView) {
        return;
    }
    // Permanent AppDefault: nothing to preview. Keep the control checked and
    // disabled via syncWorkspaceBackgroundActions (avoids a stuck toggle).
    if (m_imageView->workspaceBackground().isAppDefault()) {
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
