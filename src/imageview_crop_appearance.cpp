// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop appearance store/restore/apply (was section of imageview_appearance).

#include "imageview.h"
#include <QString>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include "biltoo_thread.h"
#include <QTimer>
#include <QImage>
#include "viewtransform.h"
#include "itemcomponents.h"
#include "cropsession.h"
#include "imagecache.h"
#include "contentxform.h"
#include "sessionappearance.h"
#include "thumtoocache.h"
#include "imageitem.h"
#include "imageloader.h"

// --- Crop appearance (was imageview_crop.cpp) ---

void ImageView::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    if (!item) {
        return;
    }
    if (sid != kInvalidSessionImageId) {
        // Crop commit owns crop + content bake only. Do not setAppearance the
        // full DTO — that would re-sync attention/color/placement from freeze
        // and risk clearing components not in this write path.
        m_itemWorld.setCrop(sid, ItemComponents::cropFromState(s));
        m_itemWorld.setContentBake(sid, ItemComponents::contentBakeFromState(s));
    } else {
        // Unbound only: path map is the sole store.
        m_itemWorld.setPathState(item->path(), s);
    }
}

bool ImageView::loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                          SessionImageId *sidOut) const
{
    if (!item || !app) {
        return false;
    }
    const SessionImageId sid = resolveContentEditSessionId(item);
    if (sidOut) {
        *sidOut = sid;
    }
    if (loadSessionAppearance(sid, app)) {
        return true;
    }
    // Stage 2 / 4a: durable crop from sparse-prefer store, not a parallel
    // captureState rebuild. Live applied xform still uses captureState.
    if (sid != kInvalidSessionImageId && m_itemWorld.hasCrop(sid)) {
        *app = sessionAppearanceValue(sid);
        return true;
    }
    if (itemAppliedContentXform(item).hasCrop) {
        *app = freezeItemAppearance(item);
        return true;
    }
    // Unbound path map may hold crop; bound crop is sparse only.
    if (sid == kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(item->path())) {
            *app = *st;
            return true;
        }
    }
    return false;
}

void ImageView::restoreSessionCropAppearance(ImageItem *item)
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
    const QImage full = m_displayPipeline.fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    CropSession::applyItemPlacementFromState(item, app, isImageMode());
    if (full.isNull()) {
        rematerializeItemContent(item, app);
    } else if (!tryRematerializeFromHost(item, app)) {
        m_displayPipeline.installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        if (!ContentXform::equal(itemAppliedContentXform(item),
                                 ContentXform::Value::fromState(app))) {
            rematerializeItemContent(item, app);
        }
    }
    m_cropCtrl.fitImageOrUpdateWorkspace(item);
}

void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Undo/redo after-image: pixels are already content-baked — attach only.
    if (!src.isNull()) {
        attachDisplaySample(item, src, state, SessionAppearance::PixelKind::FullSource);
    } else {
        syncLiveContentMetaFromState(item, state);
    }
    applyState(item, state);
    // Seed appearance with the full state (including cropRotation) before
    // commitItemSessionEdit, which rebuilds the slot via captureState.
    {
        const SessionImageId sid = resolveContentEditSessionId(item);
        WorkspaceItemState slot = state;
        slot.sessionId = sid;
        slot.path = item->path();
        storeCropAppearance(item, sid, slot);
    }
    // Appearance persistence is commitItemSessionEdit → ItemWorld sparse tables (by id).
    // Do not write crop state into the path map for bound tiles.
    commitItemSessionEdit(item);
    // Undo back to identity: commit no longer writes identity (avoids wiping
    // good rows on noisy commits), so clear durable state explicitly.
    if (!SessionAppearance::hasContentAppearance(state)) {
        ThumtooCache::clearContentAppearance(item->path());
    }
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::emitCropApplyAppearance(SessionImageId sid, const QString &path,
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
        appearance = sessionAppearanceImage(item);
    }
    if (appearance.isNull()) {
        return;
    }
    if (hasCrop) {
        emit sessionAppearanceChanged(sid, path, appearance);
    }
    emit sessionCropApplied(sid, path, appearance, hasCrop);
}
