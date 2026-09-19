// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session appearance load/store/apply (IDENTITY.md). Crop-mode Apply undo
// stays in imageview_crop.cpp (applyCropAppearance).

#include "imageview.h"
#include "cropsession.h"
#include "imagecache.h"
#include "contentxform.h"
#include "sessionappearance.h"

#include "imageitem.h"
#include "sessionappearance.h"

void ImageView::applyStoredAppearancePixels(ImageItem *item, const WorkspaceItemState &app,
                                            SessionImageId sid)
{
    const bool needsFullSource = app.hasCrop || app.contentHFlip || app.contentVFlip
        || app.contentQuarterTurns != 0;
    if (needsFullSource) {
        const QImage full = fullRasterForEdit(item->path());
        if (!full.isNull()) {
            installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource,
                                 sid);
            return;
        }
    }
    rematerializeItemContent(item, app);
}

const WorkspaceItemState *ImageView::resolveStoredAppearance(ImageItem *item,
                                                             WorkspaceItemState *fallback,
                                                             SessionImageId *sidOut)
{
    if (!item || !fallback) {
        return nullptr;
    }
    const SessionImageId sid = item->sessionId();
    if (sidOut) {
        *sidOut = sid;
    }
    if (sid != kInvalidSessionImageId) {
        // Seed orient/flip/grade from path XDG when the id slot is still empty
        // (restart / first bind). Crop is never seeded from path (IDENTITY).
        seedSessionAppearanceFromState(sid, item->path());
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            return it;
        }
        // Bound with no durable content after seed = full frame.
        // NEVER fall back to the path map — that leaks crop/flip across
        // independent session images that share a file path.
        return nullptr;
    }
    // Path map only when unbound (no session image id).
    if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
        *fallback = *st;
        return fallback;
    }
    return nullptr;
}

void ImageView::applyStoredAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState fallback;
    SessionImageId sid = kInvalidSessionImageId;
    const WorkspaceItemState *app = resolveStoredAppearance(item, &fallback, &sid);
    if (!app) {
        return;
    }
    applyStoredAppearancePixels(item, *app, sid);
}

void ImageView::applyContentAppearanceAfterDecode(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState fallback;
    const WorkspaceItemState *app = resolveStoredAppearance(item, &fallback, nullptr);
    if (!app || !SessionAppearance::hasContentAppearance(*app)) {
        return;
    }
    // Caller just installed full on-disk pixels; do not load again.
    rematerializeItemContent(item, *app);
}

void ImageView::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    if (!item) {
        return;
    }
    if (sid != kInvalidSessionImageId) {
        m_appearance.set(sid, s);
    } else {
        // Unbound only: path map is the sole store.
        m_itemStateBook.set(item->path(), s);
    }
}

QSize ImageView::cropRecordFileNative(const QString &path) const
{
    QSize fileNative = logicalSizeForPath(path);
    if (!isPositiveSize(fileNative) || fileNative.width() <= 1
        || isProvisionalImageSize(path)) {
        return {};
    }
    return fileNative;
}

bool ImageView::loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const
{
    if (!st || sid == kInvalidSessionImageId) {
        return false;
    }
    if (const WorkspaceItemState *it = m_appearance.get(sid)) {
        *st = *it;
        return true;
    }
    return false;
}

bool ImageView::loadPathBookAppearance(ImageItem *item, WorkspaceItemState *app) const
{
    if (!item || !app) {
        return false;
    }
    if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
        *app = *st;
        return true;
    }
    return false;
}

bool ImageView::loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                          SessionImageId *sidOut) const
{
    if (!item || !app) {
        return false;
    }
    const SessionImageId sid = CropSession::resolveSessionIdForItem(
        item, m_sessionId.currentIdValue());
    if (sidOut) {
        *sidOut = sid;
    }
    if (loadSessionAppearance(sid, app)) {
        return true;
    }
    if (CropSession::fillAppearanceFromItemSessionCrop(app, item)) {
        return true;
    }
    return loadPathBookAppearance(item, app);
}


void ImageView::storeAppearanceFromState(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    WorkspaceItemState slot = state;
    slot.sessionId = sid;
    slot.path = item->path();
    storeCropAppearance(item, sid, slot);
}

void ImageView::relayoutAfterAppearanceApply(ImageItem *item)
{
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

void ImageView::rematerializeIfContentXformMismatch(ImageItem *item,
                                                    const WorkspaceItemState &app)
{
    if (!item) {
        return;
    }
    if (!ContentXform::equal(
            item->hasAppliedContentXform() ? item->appliedContentXform()
                                           : ContentXform::Value{},
            ContentXform::Value::fromState(app))) {
        rematerializeItemContent(item, app);
    }
}

void ImageView::installRestoredCropPixelsFromFull(ImageItem *item, const WorkspaceItemState &app,
                                                  SessionImageId sid, const QImage &full)
{
    if (!tryRematerializeFromHost(item, app)) {
        installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        rematerializeIfContentXformMismatch(item, app);
    }
}

void ImageView::installRestoredCropPixels(ImageItem *item, const WorkspaceItemState &app,
                                          SessionImageId sid, const QImage &full)
{
    if (!item) {
        return;
    }
    CropSession::applyItemPlacementFromState(item, app, isImageMode());
    if (full.isNull()) {
        rematerializeItemContent(item, app);
        return;
    }
    installRestoredCropPixelsFromFull(item, app, sid, full);
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
    const QImage full = fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    installRestoredCropPixels(item, app, sid, full);
    fitImageOrUpdateWorkspace(item);
}

