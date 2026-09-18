// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPSESSION_H
#define CROPSESSION_H

#include "imageview_types.h"
// WorkspaceItemState is in imageview_types.h

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>

class ImageItem;

/**
 * Crop-mode draft interaction handles (viewport chrome).
 * Shared by CropSession and ImageView input/paint.
 */
enum class CropHandle {
    None,
    /** Translate the draft rect without changing size. */
    Move,
    /** Rotate the draft about its centre. */
    Rotate,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    /**
     * Toggle: when on, the draft may extend outside the image (pad on apply).
     * When off, the draft is clamped to the image bounds.
     */
    ExpandToggle,
    /**
     * Shrink the draft to content: median border colour as background,
     * trim empty margins (GIMP-style autocrop). Axis-aligned.
     */
    Auto,
    /** Clears the draft rect to the full image (reset session crop on apply). */
    Reset,
    /** Leave crop mode and discard the draft. */
    Cancel,
    /** Leave crop mode and commit the draft (same as Enter). */
    Close
};

/**
 * Crop-mode draft state for one ImageView.
 *
 * Owns the draft rect, target binding, enter-stash for undo, and interaction
 * drag fields. Enter/apply/leave orchestration stays on ImageView (needs canvas
 * and SessionAppearanceStore). Session wipe should call clear().
 */
class CropSession
{
public:
    bool active() const { return mode; }
    bool isActive() const { return mode; }

    /**
     * True when install/ladder must not replace the draft sample for @p path.
     * Uses draftPath and targetItem path; does not walk the scene by session id.
     */
    bool locksPath(const QString &path) const
    {
        if (!draftSampleFrozen || path.isEmpty()) {
            return false;
        }
        if (!draftPath.isEmpty() && path == draftPath) {
            return true;
        }
        if (targetItem && targetItem->path() == path) {
            return true;
        }
        return false;
    }

    /**
     * True when @p item is the draft subject (pointer, session id, or path).
     * Host may still lock via targetId → other item path lookup.
     */
    bool locksItem(const ImageItem *item) const
    {
        if (!draftSampleFrozen || !item) {
            return false;
        }
        if (targetItem && item == targetItem) {
            return true;
        }
        if (targetId != kInvalidSessionImageId
            && item->sessionId() == targetId) {
            return true;
        }
        if (!item->path().isEmpty() && locksPath(item->path())) {
            return true;
        }
        return false;
    }

    /**
     * Pure draft layout gate: crop mode active but no applied/session crop yet.
     * After Apply, mode may still be true while crop pixels are attached —
     * that is not draft geometry (must not force orient-only layoutSize).
     */
    static bool isDraftLayoutGeometry(bool cropModeActive, bool appliedHasCrop,
                                      bool sessionHasCrop)
    {
        return cropModeActive && !appliedHasCrop && !sessionHasCrop;
    }

    /** Drop handle/rubber/drag interaction only. */
    void clearInteraction()
    {
        activeHandle = CropHandle::None;
        hoverHandle = CropHandle::None;
        rubberBanding = false;
        rubberOriginLocal = {};
        dragStartRect = {};
        dragStartLocal = {};
        rotateStartAngle = 0.0;
        rotateStartRotation = 0.0;
    }

    /**
     * Full leave / session wipe: inactive, no target, no enter stash, no pending
     * full rematerialize. Does not touch ImageItem pixels.
     */
    void clear()
    {
        mode = false;
        targetId = kInvalidSessionImageId;
        targetItem = nullptr;
        draftSampleFrozen = false;
        draftPath.clear();
        pendingFullRematerialize = false;
        pendingFullRematerializePath.clear();
        pendingFullRematerializeSid = kInvalidSessionImageId;
        pendingFullRematerializeWant = {};
        allowExpand = false;
        rotation = 0.0;
        showingFullImage = false;
        awaitingFullPath.clear();
        stashedPlacementRotation = 0.0;
        stashedPlacementShear = 0.0;
        hadStashedPlacement = false;
        enterSource = {};
        enterState = {};
        enterValid = false;
        rect = {};
        clearInteraction();
    }

    bool mode = false;
    SessionImageId targetId = kInvalidSessionImageId;
    ImageItem *targetItem = nullptr;

    /**
     * True after crop enter selected the subject (before mode is true if draft
     * attach is still pending). Install/ladder key off this — not mode alone.
     */
    bool draftSampleFrozen = false;
    QString draftPath;

    bool pendingFullRematerialize = false;
    QString pendingFullRematerializePath;
    SessionImageId pendingFullRematerializeSid = kInvalidSessionImageId;
    WorkspaceItemState pendingFullRematerializeWant;

    bool allowExpand = false;
    qreal rotation = 0.0;
    qreal rotateStartAngle = 0.0;
    qreal rotateStartRotation = 0.0;

    bool showingFullImage = false;
    QString awaitingFullPath;

    qreal stashedPlacementRotation = 0.0;
    qreal stashedPlacementShear = 0.0;
    bool hadStashedPlacement = false;

    QImage enterSource;
    WorkspaceItemState enterState;
    bool enterValid = false;

    QRectF rect;
    CropHandle activeHandle = CropHandle::None;
    CropHandle hoverHandle = CropHandle::None;
    bool rubberBanding = false;
    QPointF rubberOriginLocal;
    QRectF dragStartRect;
    QPointF dragStartLocal;
};

#endif // CROPSESSION_H
