// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "slideshow/slideshowclocks.h"
#include "view/viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "host/thumtoocache.h"
#include "session/sessionopen.h"
#include "session/sessionsort.h"
#include "ttfp_trace.h"
#include "projectfile.h"
#include "archivepath.h"
#include "pagepath.h"
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

// Project save/load, recent projects, Workspace background (split from mainwindow_session).

namespace {

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

} // namespace

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
    updateFileExportActions();
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
