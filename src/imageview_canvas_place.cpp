// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Add / place / move images onto the canvas.

#include "imageview.h"
#include "packorderview.h"
#include "imageitem.h"
#include "imagecache.h"
#include "sessionappearance.h"

#include <QSet>
#include <QPointer>
#include <QTimer>

bool ImageView::addImage(const QString &path)
{
    if (isImageMode()) {
        return false;
    }

    if (ImageItem *existing = findItemByPath(path)) {
        m_scene->clearSelection();
        existing->setSelected(true);
        ensureVisibleItem(existing);
        emit statusChanged();
        return true;
    }

    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}


bool ImageView::addImageForSession(const QString &path, SessionImageId sessionId,
                                     int sessionIndex)
{
    if (isImageMode() || path.isEmpty()) {
        return false;
    }
    if (sessionId != kInvalidSessionImageId) {
        if (ImageItem *existing = findItemBySessionId(sessionId)) {
            if (existing->scene() == m_scene) {
                // Re-apply store so an early-return does not leave a bare tile
                // (seed / prior decode without pose or content appearance).
                if (const WorkspaceItemState *app = appearance().get(sessionId)) {
                    applyStoredAppearance(existing);
                    applyState(existing, *app);
                }
                m_scene->clearSelection();
                existing->setSelected(true);
                ensureVisibleItem(existing);
                emit statusChanged();
                return true;
            }
        }
    }
    if (sessionIndex >= 0) {
        if (ImageItem *existing = findItemBySessionIndex(sessionIndex)) {
            if (sessionId != kInvalidSessionImageId) {
                existing->setSessionId(sessionId);
            }
            if (sessionId != kInvalidSessionImageId) {
                if (const WorkspaceItemState *app = appearance().get(sessionId)) {
                    applyStoredAppearance(existing);
                    applyState(existing, *app);
                }
            }
            m_scene->clearSelection();
            existing->setSelected(true);
            ensureVisibleItem(existing);
            emit statusChanged();
            return true;
        }
    }
    // Session-bound add: always decode fresh so content flips/crops of a peer
    // tile with the same path are not shared as baked pixels.
    if (sessionId != kInvalidSessionImageId || sessionIndex >= 0) {
        PendingSessionBind b;
        b.path = path;
        b.id = sessionId;
        b.index = sessionIndex;
        m_bindBook.append(b);
        if (sessionIndex >= 0) {
            m_bindBook.setIndexForPath(path, sessionIndex);
        }
        // Paste / membership must grow pathOrder so LoadAdd's wanted count
        // includes this session image. Without this, a path already on the
        // canvas left have==pathOrderCount and never created the new tile
        // (filmstrip row only, wrong thumb until appearance emit).
        if (sessionId != kInvalidSessionImageId) {
            bool alreadyOrdered = false;
            const PackOrderView packOrder = currentPackOrder();
            for (SessionImageId id : packOrder.ids()) {
                if (id == sessionId) {
                    alreadyOrdered = true;
                    break;
                }
            }
            if (!alreadyOrdered) {
                pathOrderAppendRow(path, sessionId);
            }
        } else {
            pathOrderAppendRow(path, kInvalidSessionImageId);
        }
    }
    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}


bool ImageView::addImageForSession(const QString &path, int sessionIndex)
{
    return addImageForSession(path, kInvalidSessionImageId, sessionIndex);
}


bool ImageView::addImageAt(const QString &path, const QPointF &scenePos)
{
    if (path.isEmpty()) {
        return false;
    }
    m_displayPipeline.loadGate().setPendingScenePos(path, scenePos);
    return addImage(path);
}


bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                                     SessionImageId sessionId, int sessionIndex)
{
    if (path.isEmpty() || isImageMode()) {
        return false;
    }
    // Same session id already on the canvas: move that tile (do not spawn a twin).
    if (sessionId != kInvalidSessionImageId) {
        if (ImageItem *existing = findItemBySessionId(sessionId)) {
            if (existing->scene() == m_scene) {
                // Drop out of gallery pack geometry (cell size + pack scale).
                existing->setGalleryCellSize({});
                existing->setItemScale(1.0);
                existing->setItemRotation(0.0);
                existing->setItemShear(0.0);
                existing->setItemOpacity(1.0);
                existing->setPos(scenePos);
                if (isWorkspaceMode()) {
                    existing->setInteractive(true);
                    existing->setScaleHandlesEnabled(true);
                }
                if (m_scene) {
                    m_scene->clearSelection();
                }
                existing->setSelected(true);
                // Persist free-form pose so a later appearance restore cannot
                // revive gallery pack coordinates.
                rememberItemState(existing);
                updateWorkspaceSceneRect();
                ensureVisibleItem(existing);
                emit statusChanged();
                return true;
            }
        }
    }
    // Always decode from disk for a new canvas instance. Never clone another
    // tile's baked pixels by path — that copied crop/flip from the first
    // occurrence and showed the wrong image under a correct filename
    // (IDENTITY: SessionImageId is independent of path).
    PendingSessionBind b;
    b.path = path;
    b.id = sessionId;
    b.index = sessionIndex;
    b.scenePos = scenePos;
    b.hasScenePos = true;
    if (sessionId != kInvalidSessionImageId || sessionIndex >= 0 || b.hasScenePos) {
        m_bindBook.append(b);
    }
    // Membership order is id-aware: each place of a session image is a row.
    // Path alone cannot express "two tiles, same file".
    if (sessionId != kInvalidSessionImageId) {
        bool alreadyOrdered = false;
        const PackOrderView packOrder = currentPackOrder();
        for (SessionImageId id : packOrder.ids()) {
            if (id == sessionId) {
                alreadyOrdered = true;
                break;
            }
        }
        if (!alreadyOrdered) {
            pathOrderAppendRow(path, sessionId);
        }
    } else {
        // Unbound place: still need a decode slot beyond existing path matches.
        pathOrderAppendRow(path, kInvalidSessionImageId);
    }
    // Legacy path-keyed pos kept as fallback when a bind is missing.
    m_displayPipeline.loadGate().setPendingScenePos(path, scenePos);

    // Immediate placeholder at the drop point so placement does not depend on
    // async decode ordering (and so archive ladder delays still show a tile).
    // Avoid rememberItemState / selection signals here — dropEvent is still on
    // the stack (filesDropped → handleDroppedUrls); re-entrant status/selection
    // updates were tripping Qt "destructor may have already run" asserts.
    {
        // Workspace scene units = content pixels at scale 1. Placeholder starts
        // at layout size with identity ContentXform (no pack cell, no flips).
        QSize sz = layoutSizeForPath(path, QImage());
        if (!isPositiveSize(sz) || sz.width() <= 1 || sz.height() <= 1) {
            sz = QSize(512, 512);
        }
        ImageItem *ph = new ImageItem(path, sz);
        ph->setPos(scenePos);
        ph->setGalleryCellSize({});
        ph->setItemScale(1.0, 1.0);
        ph->setItemRotation(0.0);
        ph->setItemShear(0.0);
        ph->setItemOpacity(1.0);
        ph->setItemHFlip(false);
        ph->setItemVFlip(false);
        ph->setContentHFlip(false);
        ph->setContentVFlip(false);
        ph->setSessionCrop(false, QRect());
        ph->clearAppliedContentXform();
        if (sessionId != kInvalidSessionImageId) {
            ph->setSessionId(sessionId);
        }
        if (sessionIndex >= 0) {
            ph->setSessionIndex(sessionIndex);
        }
        if (m_scene) {
            m_scene->addItem(ph);
        }
        m_items.append(ph);
        applyItemModeFlags(ph);
        registerItemDisplaySurface(ph);
        if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
            dbg && dbg[0] != '\0' && dbg[0] != '0') {
            fprintf(stderr,
                    "biltoo/drop: placeholder path=%s sid=%lld pos=(%.1f,%.1f) "
                    "size=%dx%d\n",
                    qPrintable(path), static_cast<long long>(sessionId),
                    scenePos.x(), scenePos.y(), sz.width(), sz.height());
        }
    }

    // Defer decode + UI signals until after the drop event stack unwinds.
    const QString pathCopy = path;
    QPointer<ImageView> guard(this);
    QTimer::singleShot(0, this, [guard, pathCopy]() {
        ImageView *const host = guard.data();
        if (!host || !host->isWorkspaceMode()) {
            return;
        }
        host->scheduleImageLoad(pathCopy, LoadAdd);
        host->updateWorkspaceSceneRect();
        emit host->statusChanged();
        // Do not emit workspacePathsChanged here: MainWindow defers
        // syncThumbnailCanvasMembership after the drop; a second rebind race
        // was implicated in Qt type/destructor asserts.
    });
    return true;
}


bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos)
{
    return placeOrMoveImageAt(path, scenePos, kInvalidSessionImageId, -1);
}

