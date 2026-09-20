// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Durable bake of 90° rotate and flip into session content appearance.

#include "imageview.h"
#include "imageitem.h"
#include "contentxform.h"
#include "sessionappearance.h"
#include "thumtoocache.h"
#include "imagecache.h"

#include <QImage>

void ImageView::bakeItemRotate90(ImageItem *item, int quarterTurns)
{
    if (!item || quarterTurns == 0) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureContentBakeBeforeState(item);

    const SessionImageId sid = resolveContentEditSessionId(item);
    const int turns = ContentXform::normalizeQuarterTurns(
        beforeSt.contentQuarterTurns + quarterTurns);
    // Keep full-source crop geometry in sync with content orientation so
    // re-entering crop mode still frames the same region.
    WorkspaceItemState cropMap = appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentRotate90(cropMap, quarterTurns);
    if (cropMap.hasCrop) {
        item->setSessionCrop(true, cropMap.cropRect);
    }

    // Absolute want after this edit.
    WorkspaceItemState want = beforeSt;
    want.contentQuarterTurns = turns;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentHFlip = item->contentHFlip();
    want.contentVFlip = item->contentVFlip();
    want.colorAdjust = item->colorAdjustments();

    // ContentXform is ground truth: absolute want from store + delta, pure
    // materialize from unoriented host. Never stack incremental transforms.
    // ≤512 host: GUI pure. Multi-MP: soft stand-in from clamped host + async.
    if (!tryRematerializeFromHost(item, want)) {
        const QString path = item->path();
        const QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
        bool gotDisplay = false;
        if (!host.isNull()) {
            QImage soft = host;
            if (ContentXform::longEdge(host.size())
                > ContentXform::kGuiMaterializeMaxEdge) {
                soft = ImageCache::clampToMaxEdge(
                    host, ContentXform::kGuiMaterializeMaxEdge);
            }
            const QImage display = SessionAppearance::materializeDisplay(
                soft, want, SessionAppearance::PixelKind::SoftPreview);
            if (!display.isNull()) {
                // Soft attach must not be ignored when full was present.
                item->clearDecodedPixels();
                attachDisplaySample(item, display, want,
                                    SessionAppearance::PixelKind::SoftPreview);
                gotDisplay = true;
            }
        }
        if (!gotDisplay) {
            // No host at all: last resort incremental on whatever is shown.
            item->bakeRotate90(quarterTurns);
        }
        applyContentLayoutSize(item, want);
        scheduleAsyncHostRematerialize(path, sid, want);
    } else {
        applyContentLayoutSize(item, want);
    }

    // Write ContentXform absolute state first — before commitItemSessionEdit
    // captureState, so commit cannot resurrect stale path-map turns.
    {
        WorkspaceItemState s = want;
        s.sessionId = sid;
        s.path = item->path();
        s.orientation = 0.0;
        s.contentQuarterTurns = turns;
        if (sid != kInvalidSessionImageId) {
            // Preserve placement fields from previous appearance when present.
            if (const WorkspaceItemState *prev = m_itemWorld.getAppearance(sid)) {
                s.pos = prev->pos;
                s.scale = prev->scale;
                s.scaleY = prev->scaleY;
                s.shear = prev->shear;
                s.rotation = prev->rotation;
                s.opacity = prev->opacity;
                s.z = prev->z;
                s.hFlip = prev->hFlip;
                s.vFlip = prev->vFlip;
                s.sessionIndex = prev->sessionIndex;
            }
            m_itemWorld.setAppearance(sid, s);
            persistDurableContentAppearance(item, s, "bakeRotate");
        }
        // Keep path map content fields in sync so pack afterEach cannot leave
        // stale turns for any reader that still peeks at m_itemStateBook.byPath.
        {
            WorkspaceItemState pathSlot;
            if (const WorkspaceItemState *st = m_itemWorld.getPathState(item->path())) {
                pathSlot = *st;
            }
            pathSlot.path = item->path();
            pathSlot.contentQuarterTurns = turns;
            pathSlot.contentHFlip = want.contentHFlip;
            pathSlot.contentVFlip = want.contentVFlip;
            pathSlot.hasCrop = want.hasCrop;
            pathSlot.cropRect = want.cropRect;
            pathSlot.cropRotation = want.cropRotation;
            pathSlot.cropSourceSize = want.cropSourceSize;
            m_itemWorld.setPathState(item->path(), pathSlot);
        }
        item->setAppliedContentXform(ContentXform::Value::fromState(s));
    }

    commitItemSessionEdit(item);

    // Image mode: contentRect axes may have swapped — refresh tight sceneRect
    // so pan/fit are not locked to the pre-rotate box.
    if (isImageMode() && m_scene && m_items.size() == 1) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.cropRotation = cropMap.cropRotation;
    afterSt.cropSourceSize = cropMap.cropSourceSize;
    afterSt.contentHFlip = item->contentHFlip();
    afterSt.contentVFlip = item->contentVFlip();
    afterSt.contentQuarterTurns = turns;
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(tr("Rotate"), item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
}


void ImageView::bakeItemFlip(ImageItem *item, bool horizontal, bool vertical)
{
    if (!item || (!horizontal && !vertical)) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureContentBakeBeforeState(item);

    // Content-orientation flags track the source raster so durable crop mapping
    // (flip then quarter-turn on the full raster) stay consistent with the
    // display-space flip just applied to the oriented pixels.
    //
    // With quarter-turns t: display H/V axes map to source axes as
    //   t=0,2: same axes;  t=1,3: H↔V  (conjugation through 90°/270° CW).
    // Crop stays in post-content space, so mapCropThroughContentFlip below
    // still uses the display axes the user pressed.
    bool h = beforeSt.contentHFlip;
    bool v = beforeSt.contentVFlip;
    int turns = beforeSt.contentQuarterTurns % 4;
    if (turns < 0) {
        turns += 4;
    }
    const bool swapAxes = (turns == 1 || turns == 3);
    const bool srcH = swapAxes ? vertical : horizontal;
    const bool srcV = swapAxes ? horizontal : vertical;
    if (srcH) {
        h = !h;
    }
    if (srcV) {
        v = !v;
    }

    const SessionImageId sid = resolveContentEditSessionId(item);
    WorkspaceItemState cropMap = appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentFlip(cropMap, horizontal, vertical);
    if (cropMap.hasCrop) {
        item->setSessionCrop(true, cropMap.cropRect);
    }

    WorkspaceItemState want = beforeSt;
    want.contentHFlip = h;
    want.contentVFlip = v;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentQuarterTurns = cropMap.contentQuarterTurns;

    // Prefer pure rematerialize from unoriented host; else incremental + async.
    item->setContentHFlip(h);
    item->setContentVFlip(v);
    if (!tryRematerializeFromHost(item, want)) {
        item->bakeFlip(horizontal, vertical);
        item->setContentHFlip(h);
        item->setContentVFlip(v);
        scheduleAsyncHostRematerialize(item->path(), sid, want);
    }
    applyContentLayoutSize(item, want);

    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState s = captureState(item);
        s.sessionId = sid;
        s.hasCrop = cropMap.hasCrop;
        s.cropRect = cropMap.cropRect;
        s.cropRotation = cropMap.cropRotation;
        s.cropSourceSize = cropMap.cropSourceSize;
        s.contentHFlip = h;
        s.contentVFlip = v;
        s.contentQuarterTurns = cropMap.contentQuarterTurns;
        m_itemWorld.setAppearance(sid, s);
        persistDurableContentAppearance(item, s, "bakeFlip");
    } else if (cropMap.hasCrop) {
        WorkspaceItemState s = captureState(item);
        s.hasCrop = true;
        s.cropRect = cropMap.cropRect;
        s.cropRotation = cropMap.cropRotation;
        s.cropSourceSize = cropMap.cropSourceSize;
        s.contentHFlip = h;
        s.contentVFlip = v;
        m_itemWorld.setPathState(item->path(), s);
    }

    commitItemSessionEdit(item);

    {
        WorkspaceItemState tag;
        tag.contentHFlip = h;
        tag.contentVFlip = v;
        tag.contentQuarterTurns = beforeSt.contentQuarterTurns;
        tag.hasCrop = item->sessionHasCrop();
        tag.cropRect = item->sessionCropRect();
        tag.cropRotation = cropMap.cropRotation;
        tag.cropSourceSize = cropMap.cropSourceSize;
        item->setAppliedContentXform(ContentXform::Value::fromState(tag));
    }

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.cropRotation = cropMap.cropRotation;
    afterSt.cropSourceSize = cropMap.cropSourceSize;
    afterSt.contentHFlip = h;
    afterSt.contentVFlip = v;
    afterSt.contentQuarterTurns = beforeSt.contentQuarterTurns;
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(horizontal && !vertical ? tr("Flip horizontal")
                          : vertical && !horizontal ? tr("Flip vertical")
                          : tr("Flip"),
                           item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
}

