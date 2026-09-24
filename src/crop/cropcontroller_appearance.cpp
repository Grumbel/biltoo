// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop appearance store/restore/apply (was ImageView crop appearance).

#include "crop/cropcontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "crop/cropsession.h"
#include "display/imagecache.h"
#include "display/displaypipelinecontroller.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "host/thumtoocache.h"

void CropController::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    if (!item) {
        return;
    }
    if (sid != kInvalidSessionImageId) {
        // Crop commit owns crop + content bake only. Do not setAppearance the
        // full DTO — that would re-sync attention/color/placement from freeze
        // and risk clearing components not in this write path.
        m_view->itemWorld().setCrop(sid, ItemComponents::cropFromState(s));
        m_view->itemWorld().setContentBake(sid, ItemComponents::contentBakeFromState(s));
    } else {
        // Unbound only: path map is the sole store.
        m_view->itemWorld().setPathState(item->path(), s);
    }
}

bool CropController::loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                          SessionImageId *sidOut) const
{
    if (!item || !app) {
        return false;
    }
    const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
    if (sidOut) {
        *sidOut = sid;
    }
    WorkspaceItemState sessionApp;
    const bool sessionLoaded = m_view->loadSessionAppearance(sid, &sessionApp);
    const WorkspaceItemState *pathSt =
        (sid == kInvalidSessionImageId) ? m_view->itemWorld().getPathState(item->path())
                                        : nullptr;
    // Stage 2 / 4a: durable crop from sparse-prefer store, not a parallel
    // captureState rebuild. Live applied xform still uses captureState.
    switch (SessionAppearance::cropRestoreSource(
        sessionLoaded, sid,
        sid != kInvalidSessionImageId && m_view->itemWorld().hasCrop(sid),
        m_view->itemAppliedContentXform(item).hasCrop,
        pathSt != nullptr)) {
    case SessionAppearance::CropRestoreSource::SessionStore:
        *app = sessionApp;
        return true;
    case SessionAppearance::CropRestoreSource::SparseCrop:
        *app = m_view->sessionAppearanceValue(sid);
        return true;
    case SessionAppearance::CropRestoreSource::LiveAppliedFreeze:
        *app = m_view->freezeItemAppearance(item);
        return true;
    case SessionAppearance::CropRestoreSource::PathMap:
        *app = *pathSt;
        return true;
    case SessionAppearance::CropRestoreSource::None:
        break;
    }
    return false;
}

void CropController::restoreSessionCropAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState app;
    SessionImageId sid = kInvalidSessionImageId;
    if (!loadRestoreCropAppearance(item, &app, &sid)) {
        return;
    }
    const QString path = item->path();
    const QImage full = m_view->hostDisplayPipeline().fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    CropSession::applyItemPlacementFromState(item, app, m_view->isImageMode());
    if (full.isNull()) {
        m_view->hostDisplayPipeline().rematerializeItemContent(item, app);
    } else if (!m_view->hostDisplayPipeline().tryRematerializeFromHost(item, app)) {
        m_view->hostDisplayPipeline().installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        if (!ContentXform::equal(m_view->itemAppliedContentXform(item),
                                 ContentXform::Value::fromState(app))) {
            m_view->hostDisplayPipeline().rematerializeItemContent(item, app);
        }
    }
    fitImageOrUpdateWorkspace(item);
}

void CropController::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Undo/redo after-image: pixels are already content-baked — attach only.
    if (!src.isNull()) {
        m_view->hostDisplayPipeline().attachDisplaySample(item, src, state, SessionAppearance::PixelKind::FullSource);
    } else {
        m_view->syncLiveContentMetaFromState(item, state);
    }
    m_view->applyState(item, state);
    // Seed appearance with the full state (including cropRotation) before
    // commitItemSessionEdit, which rebuilds the slot via captureState.
    {
        const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
        WorkspaceItemState slot = state;
        slot.sessionId = sid;
        slot.path = item->path();
        storeCropAppearance(item, sid, slot);
    }
    // Appearance persistence is commitItemSessionEdit → ItemWorld sparse tables (by id).
    // Do not write crop state into the path map for bound tiles.
    m_view->hostImage().commitItemSessionEdit(item);
    // Undo back to identity: commit no longer writes identity (avoids wiping
    // good rows on noisy commits), so clear durable state explicitly.
    if (!SessionAppearance::hasContentAppearance(state)) {
        ThumtooCache::clearContentAppearance(item->path());
    }
    if (m_view->isImageMode()) {
        m_view->hostImage().framing().armFit();
        m_view->fitItem(item, m_view->currentFitAspectMode());
    } else if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit m_view->statusChanged();
}

void CropController::emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                           ImageItem *item, const QImage &preferredDisplay,
                                           bool hasCrop)
{
    if (sid == kInvalidSessionImageId) {
        return;
    }
    // Prefer the crop bake just materialized when provided — not displayImage(),
    // which can still be pre-crop if soft attach was rejected.
    QImage appearance = preferredDisplay;
    if (appearance.isNull() && item) {
        appearance = m_view->hostSessionAppearanceImage(item);
    }
    if (appearance.isNull()) {
        return;
    }
    if (hasCrop) {
        emit m_view->sessionAppearanceChanged(sid, path, appearance);
    }
    emit m_view->sessionCropApplied(sid, path, appearance, hasCrop);
}
