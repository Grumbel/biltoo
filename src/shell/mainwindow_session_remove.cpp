// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/mainwindow_includes.h"
#include "slideshowclocks.h"
#include "viewtransform.h"
#include <QtMath>
#include <algorithm>
#include "thumtoocache.h"
#include "session/sessionopen.h"
#include "session/sessionsort.h"
#include "session/sessionexpand.h"
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

// Session remove/restore undo chrome (split from mainwindow_session).

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

