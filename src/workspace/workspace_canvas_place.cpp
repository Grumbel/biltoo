// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session add / drop place-or-move on the multi-item canvas (was ImageView).

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "display/imagecache.h"
#include "session/sessionappearance.h"
#include "session/sessionbindbook.h"
#include "session/packorderview.h"
#include "view/viewtransform.h"
#include "content/contentxform.h"

#include <QGraphicsItem>
#include <QPointer>
#include <QTimer>
#include <cstdlib>
#include <cstdio>

bool WorkspaceController::addImageForSession(const QString &path, SessionImageId sessionId,
                                     int sessionIndex)
{
    if (m_view->isImageMode() || path.isEmpty()) {
        return false;
    }
    if (sessionId != kInvalidSessionImageId) {
        if (ImageItem *existing = m_view->findItemBySessionId(sessionId)) {
            if (existing->scene() == m_view->canvasScene()) {
                // Re-apply store so an early-return does not leave a bare tile
                // (seed / prior decode without pose or content appearance).
                if (m_view->itemWorld().hasDurableAppearance(sessionId)) {
                    m_view->applyStoredAppearance(existing);
                    m_view->applyState(existing, m_view->sessionAppearanceValue(sessionId));
                }
                m_view->canvasScene()->clearSelection();
                existing->setSelected(true);
                m_view->ensureVisible(static_cast<const QGraphicsItem *>(existing),
                                  ViewTransform::kEnsureVisibleMargin,
                                  ViewTransform::kEnsureVisibleMargin);
                emit m_view->statusChanged();
                return true;
            }
        }
    }
    if (sessionIndex >= 0) {
        if (ImageItem *existing = m_view->hostFindItemBySessionIndex(sessionIndex)) {
            if (sessionId != kInvalidSessionImageId) {
                m_view->setItemSessionId(existing, sessionId);
            }
            if (sessionId != kInvalidSessionImageId) {
                if (m_view->itemWorld().hasDurableAppearance(sessionId)) {
                    m_view->applyStoredAppearance(existing);
                    m_view->applyState(existing, m_view->sessionAppearanceValue(sessionId));
                }
            }
            m_view->canvasScene()->clearSelection();
            existing->setSelected(true);
            m_view->ensureVisible(static_cast<const QGraphicsItem *>(existing),
                                  ViewTransform::kEnsureVisibleMargin,
                                  ViewTransform::kEnsureVisibleMargin);
            emit m_view->statusChanged();
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
        m_view->hostBindBook().append(b);
        // Paste / membership must grow pathOrder so LoadAdd's wanted count
        // includes this session image. Without this, a path already on the
        // canvas left have==pathOrderCount and never created the new tile
        // (filmstrip row only, wrong thumb until appearance emit).
        if (sessionId != kInvalidSessionImageId) {
            bool alreadyOrdered = false;
            const PackOrderView packOrder = m_view->currentPackOrder();
            for (SessionImageId id : packOrder.ids()) {
                if (id == sessionId) {
                    alreadyOrdered = true;
                    break;
                }
            }
            if (!alreadyOrdered) {
                m_view->hostPathOrderAppendRow(path, sessionId);
            }
        } else {
            m_view->hostPathOrderAppendRow(path, kInvalidSessionImageId);
        }
    }
    m_view->hostDisplayPipeline().scheduleImageLoad(path, LoadAdd);
    emit m_view->statusChanged();
    return true;
}

bool WorkspaceController::placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                                     SessionImageId sessionId, int sessionIndex)
{
    if (path.isEmpty() || m_view->isImageMode()) {
        return false;
    }
    // Same session id already on the canvas: move that tile (do not spawn a twin).
    if (sessionId != kInvalidSessionImageId) {
        if (ImageItem *existing = m_view->findItemBySessionId(sessionId)) {
            if (existing->scene() == m_view->canvasScene()) {
                // Drop out of gallery pack geometry (cell size + pack scale).
                existing->setGalleryCellSize({});
                {
                    ItemComponents::Placement pl;
                    pl.pos = scenePos;
                    existing->applyPlacement(pl);
                }
                if (m_view->isWorkspaceMode()) {
                    existing->setInteractive(true);
                    existing->setScaleHandlesEnabled(true);
                }
                if (m_view->canvasScene()) {
                    m_view->canvasScene()->clearSelection();
                }
                existing->setSelected(true);
                // Pose-only: sparse Placement — do not setAppearance the whole
                // DTO (content already lives on the session id).
                m_view->persistGeometrySessionState(existing, existing->placement());
                m_view->updateWorkspaceSceneRect();
                m_view->ensureVisible(static_cast<const QGraphicsItem *>(existing),
                                  ViewTransform::kEnsureVisibleMargin,
                                  ViewTransform::kEnsureVisibleMargin);
                emit m_view->statusChanged();
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
        m_view->hostBindBook().append(b);
    }
    // Membership order is id-aware: each place of a session image is a row.
    // Path alone cannot express "two tiles, same file".
    if (sessionId != kInvalidSessionImageId) {
        bool alreadyOrdered = false;
        const PackOrderView packOrder = m_view->currentPackOrder();
        for (SessionImageId id : packOrder.ids()) {
            if (id == sessionId) {
                alreadyOrdered = true;
                break;
            }
        }
        if (!alreadyOrdered) {
            m_view->hostPathOrderAppendRow(path, sessionId);
        }
    } else {
        // Unbound place: still need a decode slot beyond existing path matches.
        m_view->hostPathOrderAppendRow(path, kInvalidSessionImageId);
    }
    // Legacy path-keyed pos kept as fallback when a bind is missing.
    m_view->hostDisplayPipeline().loadGate().setPendingScenePos(path, scenePos);

    // Immediate placeholder at the drop point so placement does not depend on
    // async decode ordering (and so archive ladder delays still show a tile).
    // Avoid rememberItemState / selection signals here — dropEvent is still on
    // the stack (filesDropped → handleDroppedUrls); re-entrant status/selection
    // updates were tripping Qt "destructor may have already run" asserts.
    {
        // Do not XDG-seed contentBake on place (bound id = ItemWorld only).
        // Prior seed wrote path orient into bake while host stayed raw.
        QSize sz = m_view->contentLayoutSize(path, sessionId);
        if (!isPositiveSize(sz) || sz.width() <= 1 || sz.height() <= 1) {
            sz = QSize(512, 512);
        }
        ImageItem *ph = new ImageItem(path, sz);
        ph->setGalleryCellSize({});
        {
            ItemComponents::Placement pl;
            pl.pos = scenePos;
            ph->applyPlacement(pl);
        }
        m_view->clearLiveContentMeta(ph);
        if (sessionId != kInvalidSessionImageId) {
            m_view->setItemSessionId(ph, sessionId);
            if (m_view->sessionListIndex(ph) < 0 && sessionIndex >= 0) {
                ph->setSessionIndex(sessionIndex);
            }
        } else if (sessionIndex >= 0) {
            ph->setSessionIndex(sessionIndex);
        }
        if (m_view->canvasScene()) {
            m_view->canvasScene()->addItem(ph);
        }
        m_view->liveItems().append(ph);
        m_view->applyItemModeFlags(ph);
        m_view->hostDisplayPipeline().registerItemDisplaySurface(ph);
        // Immediate content-baked soft when session/XDG want is known — do not
        // wait for LoadAdd with an unrotated host painted into an oriented box
        // (looked like stretch + missing rotation on filmstrip drop).
        if (sessionId != kInvalidSessionImageId
            && SessionAppearance::hasContentAppearance(
                   m_view->sessionAppearanceValue(sessionId))) {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                m_view->hostDisplayPipeline().installDisplayPixels(
                    ph, host, SessionAppearance::PixelKind::SoftPreview, sessionId);
            }
        }
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
    QPointer<ImageView> guard(m_view);
    QTimer::singleShot(0, m_view, [guard, pathCopy]() {
        ImageView *const host = guard.data();
        if (!host || !host->m_view->isWorkspaceMode()) {
            return;
        }
        host->hostDisplayPipeline().scheduleImageLoad(pathCopy, LoadAdd);
        host->m_view->updateWorkspaceSceneRect();
        emit host->statusChanged();
        // Do not emit workspacePathsChanged here: MainWindow defers
        // syncThumbnailCanvasMembership after the drop; a second rebind race
        // was implicated in Qt type/destructor asserts.
    });
    return true;
}
