// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Add / place / move images onto the canvas.

#include "imageview.h"
#include "packorderview.h"
#include "imageitem.h"
#include "itemcomponents.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "thumtoocache.h"
#include "contentxform.h"

#include <QSet>
#include <QPointer>
#include <QTimer>

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
                if (m_itemWorld.hasDurableAppearance(sessionId)) {
                    applyStoredAppearance(existing);
                    applyState(existing, sessionAppearanceValue(sessionId));
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
                setItemSessionId(existing, sessionId);
            }
            if (sessionId != kInvalidSessionImageId) {
                if (m_itemWorld.hasDurableAppearance(sessionId)) {
                    applyStoredAppearance(existing);
                    applyState(existing, sessionAppearanceValue(sessionId));
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
    m_displayPipeline.scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
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
                {
                    ItemComponents::Placement pl;
                    pl.pos = scenePos;
                    existing->applyPlacement(pl);
                }
                if (isWorkspaceMode()) {
                    existing->setInteractive(true);
                    existing->setScaleHandlesEnabled(true);
                }
                if (m_scene) {
                    m_scene->clearSelection();
                }
                existing->setSelected(true);
                // Pose-only: sparse Placement — do not setAppearance the whole
                // DTO (content already lives on the session id).
                persistGeometrySessionState(existing, existing->placement());
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
        // Workspace scene units = content pixels at scale 1. Placeholder must
        // use content-oriented layout size (turns + crop) so a filmstrip drop of
        // an already-rotated session image does not start with the unrotated box.
        QSize sz = layoutSizeForPath(path, QImage());
        if (!isPositiveSize(sz) || sz.width() <= 1 || sz.height() <= 1) {
            sz = QSize(512, 512);
        }
        {
            WorkspaceItemState want;
            if (sessionId != kInvalidSessionImageId
                && m_itemWorld.hasDurableAppearance(sessionId)) {
                want = sessionAppearanceValue(sessionId);
            } else if (!path.isEmpty()) {
                ThumtooCache::StoredContentAppearance stored;
                if (ThumtooCache::loadContentAppearance(path, &stored)
                    && !stored.isIdentity()) {
                    want.contentHFlip = stored.contentHFlip;
                    want.contentVFlip = stored.contentVFlip;
                    want.contentQuarterTurns = stored.contentQuarterTurns;
                    want.hasCrop = stored.hasCrop;
                    want.cropRect = stored.cropRect;
                    want.cropSourceSize = stored.cropSourceSize;
                    want.cropRotation = stored.cropRotation;
                }
            }
            if (SessionAppearance::hasContentAppearance(want)) {
                const QSize lay = ContentXform::layoutSize(sz, want);
                if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
                    sz = lay;
                }
                // Ensure ItemWorld has content ops so LoadAdd installDisplayPixels
                // materializes oriented pixels (not only the box). XDG/session
                // size above is not enough if seed was marked attempted empty.
                if (sessionId != kInvalidSessionImageId
                    && !m_itemWorld.hasContentBake(sessionId)) {
                    want.path = path;
                    want.sessionId = sessionId;
                    m_itemWorld.mergeContentFromState(sessionId, want);
                    hostSeedBook().clearSeedAttempted(sessionId);
                }
            }
        }
        ImageItem *ph = new ImageItem(path, sz);
        ph->setGalleryCellSize({});
        {
            ItemComponents::Placement pl;
            pl.pos = scenePos;
            ph->applyPlacement(pl);
        }
        clearLiveContentMeta(ph);
        if (sessionId != kInvalidSessionImageId) {
            setItemSessionId(ph, sessionId);
            if (sessionListIndex(ph) < 0 && sessionIndex >= 0) {
                ph->setSessionIndex(sessionIndex);
            }
        } else if (sessionIndex >= 0) {
            ph->setSessionIndex(sessionIndex);
        }
        if (m_scene) {
            m_scene->addItem(ph);
        }
        m_items.append(ph);
        applyItemModeFlags(ph);
        m_displayPipeline.registerItemDisplaySurface(ph);
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
        host->hostDisplayPipeline().scheduleImageLoad(pathCopy, LoadAdd);
        host->updateWorkspaceSceneRect();
        emit host->statusChanged();
        // Do not emit workspacePathsChanged here: MainWindow defers
        // syncThumbnailCanvasMembership after the drop; a second rebind race
        // was implicated in Qt type/destructor asserts.
    });
    return true;
}



