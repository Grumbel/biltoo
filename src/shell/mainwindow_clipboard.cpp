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
#include <QMimeData>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <QApplication>

#include <functional>

// Session history, duplicate, and Workspace clipboard (split from mainwindow_session).

namespace {

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
    // One session row per selected path entry (including duplicate paths);
    // empty paths are skipped — those tiles cannot join the session list.
    const QStringList sourcePaths = m_imageView->selectedPaths();
    if (sourcePaths.isEmpty()) {
        return newIds;
    }

    // Allocate session rows *before* canvas copies so tiles bind on create
    // (no unbound window: Duplicate binds SessionImageId on create).
    const int firstNew = m_session.paths().size();
    for (const QString &path : sourcePaths) {
        if (path.isEmpty()) {
            qCritical("applyDuplicate: selected tile has empty path — no session row");
            continue;
        }
        const SessionImageId id = allocSessionId();
        m_session.append(path, id);
        newIds.append(id);
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

