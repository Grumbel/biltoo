// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Durable bake of 90° rotate and flip into session content appearance.

#include "imageview.h"
#include "imageitem.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "item/itemcomponents.h"
#include "display/displayquality.h"

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

    // Absolute want after this edit. Flips/grade come from beforeSt
    // (captureContentBakeBeforeState → ItemWorld/applied xform), not a second
    // dig into live ImageItem fields.
    WorkspaceItemState want = beforeSt;
    want.contentQuarterTurns = turns;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    // Dual-write live chrome once want is absolute (ItemWorld updated below).
    syncLiveContentMetaFromState(item, want);

    // Pixel path owned by DisplayPipelineController (try / soft / async).
    // Never stack incremental transforms on display (ECS_GUI_BYPASSES #7).
    m_displayPipeline.rematerializeItemContent(item, want);
    applyContentLayoutSize(item, want);

    // Write ContentXform absolute state first — before commitItemSessionEdit
    // captureState, so commit cannot resurrect stale path-map turns.
    {
        WorkspaceItemState s = want;
        s.sessionId = sid;
        s.path = item->path();
        s.contentQuarterTurns = turns;
        if (sid != kInvalidSessionImageId) {
            // Content bake/crop only — do not re-sync attention/color/placement.
            m_itemWorld.setContentBake(sid, ItemComponents::contentBakeFromState(s));
            m_itemWorld.setCrop(sid, ItemComponents::cropFromState(s));
            persistDurableContentAppearance(item, s, "bakeRotate");
        }
        // Unbound only: path map is the sole content store. Bound content is
        // sparse + XDG (IDENTITY — do not dual-write orient by path).
        if (sid == kInvalidSessionImageId) {
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
        // Applied fingerprint already set by syncLiveContentMetaFromState(want)
        // (survives soft clearDecodedPixels; attachDisplaySample reasserts).
    }

    commitItemSessionEdit(item);

    // Image mode: contentRect axes may have swapped — refresh tight sceneRect
    // so pan/fit are not locked to the pre-rotate box.
    if (isImageMode() && m_scene && m_items.size() == 1) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    // Undo after-image: absolute content from want; live pose from the item.
    // Do not rebuild via captureState — sparse bake/crop already match want.
    WorkspaceItemState afterSt = want;
    ItemComponents::applyPlacementToState(afterSt, placementFromItem(item));
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

    WorkspaceItemState want = beforeSt;
    want.contentHFlip = h;
    want.contentVFlip = v;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentQuarterTurns = cropMap.contentQuarterTurns;

    // Install applied ContentXform fingerprint, then pipeline rematerialize.
    syncLiveContentMetaFromState(item, want);
    m_displayPipeline.rematerializeItemContent(item, want);
    applyContentLayoutSize(item, want);

    if (sid != kInvalidSessionImageId) {
        // Content bake/crop only — do not re-sync attention/color/placement.
        WorkspaceItemState s = want;
        s.sessionId = sid;
        m_itemWorld.setContentBake(sid, ItemComponents::contentBakeFromState(s));
        m_itemWorld.setCrop(sid, ItemComponents::cropFromState(s));
        persistDurableContentAppearance(item, s, "bakeFlip");
    } else if (cropMap.hasCrop) {
        WorkspaceItemState s = want;
        ItemComponents::applyPlacementToState(s, placementFromItem(item));
        m_itemWorld.setPathState(item->path(), s);
    }

    commitItemSessionEdit(item);

    // Applied fingerprint already set by syncLiveContentMetaFromState(want).
    // Undo after-image matches want content + live pose (no captureState rebuild).
    WorkspaceItemState afterSt = want;
    ItemComponents::applyPlacementToState(afterSt, placementFromItem(item));
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(horizontal && !vertical ? tr("Flip horizontal")
                          : vertical && !horizontal ? tr("Flip vertical")
                          : tr("Flip"),
                           item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
}

