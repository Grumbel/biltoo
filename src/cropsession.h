// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPSESSION_H
#define CROPSESSION_H

#include "imageview_types.h"
#include "crophandle.h"
#include "contentxform.h"
#include "sessionappearance.h"
// WorkspaceItemState is in imageview_types.h

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QPolygonF>
#include <QString>
#include <QtMath>

class ImageItem;

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
    /** Minimum draft side in content pixels during handle drag. */
    static constexpr qreal kMinDraftSidePx = 4.0;

    bool active() const { return mode; }

    /** True for frame drag handles (not chrome buttons). */
    static bool isChromeButton(CropHandle h)
    {
        return h == CropHandle::Reset || h == CropHandle::Close
            || h == CropHandle::Cancel || h == CropHandle::ExpandToggle
            || h == CropHandle::Auto;
    }

    static bool isGeometryHandle(CropHandle h)
    {
        return h != CropHandle::None && !isChromeButton(h);
    }

    /**
     * Raster chosen for crop enter install: prefer unoriented ImageCache,
     * else item sample only when there is no prior session crop bake.
     */
    struct EnterFullRaster {
        QImage image;
        bool unoriented = false;
    };

    /** Prefer ImageCache path sample; fall back to item display when no prior crop. */
    static EnterFullRaster pickEnterFullRaster(ImageItem *item, const QString &path,
                                              bool hadPriorCrop);

    /**
     * Prefer ImageCache for Apply host; fall back to item display only when
     * the live pixels are not already a prior crop bake.
     */
    static QImage pickApplyHost(ImageItem *item, const QString &path, bool *fromCache);

    /**
     * True when enter can KEEP the live display pixels (no re-bake): full-frame,
     * no prior crop, applied/live grade already matches content-only want.
     */
    static bool canKeepDisplayForEnter(const ImageItem *item,
                                       const ContentXform::Value &wantX,
                                       const WorkspaceItemState &contentOnly,
                                       bool hadPriorCrop, bool needGeomBake);

    /** Zero free placement (item rotate/shear/flip) so crop draft is content-only. */
    static void clearItemFreePlacementForDraft(ImageItem *item);

    /**
     * Long edge for Thumtoo full-pixel schedule during crop: native size clamped
     * to ImageCache::kDisplayMaxEdge, or 8192 when size unknown.
     */
    static int fullRasterScheduleEdge(const QString &path);

    /**
     * Pure Apply pixel bake: host raster + session want → display sample.
     * When @p hostFromCache is false, orient is assumed already baked into host
     * so bake content flips/turns are cleared. Multi-MP hosts are clamped and
     * marked SoftPreview.
     */
    struct ApplyBakeResult {
        QImage display;
        WorkspaceItemState bake;
        bool multiMp = false;
        bool ok() const { return !display.isNull(); }
    };

    static ApplyBakeResult materializeApplyDisplay(const QImage &host, bool hostFromCache,
                                                   WorkspaceItemState st);

    /**
     * Pure enter-install sample: content-only want from prior appearance, clamp
     * host for GUI bake limits, materialize orient/colour when @p unorientedSource.
     * Does not touch ImageItem or caches.
     */
    struct EnterInstallSample {
        WorkspaceItemState contentOnly;
        ContentXform::Value wantX;
        bool hadPriorCrop = false;
        bool needGeomBake = false;
        bool needColor = false;
        QImage display;
        SessionAppearance::PixelKind kind = SessionAppearance::PixelKind::FullSource;
    };

    static EnterInstallSample prepareEnterInstallSample(
        const QImage &full, bool unorientedSource,
        const WorkspaceItemState *app, bool haveApp);

    static SessionImageId sessionIdForRecord(const ImageItem *item,
                                             SessionImageId boundTargetId,
                                             SessionImageId imageModeCurrentId);

    static bool isAxisAlignedFullFrame(const QRectF &local, const QRectF &contentRect,
                                       qreal eps = 0.5);

    struct RecordGeometry {
        QRectF localClamped;
        QRect sourceRect;
        bool clearCrop = false;
        bool valid() const { return !localClamped.isEmpty() && !sourceRect.isEmpty(); }
    };

    RecordGeometry computeRecordGeometry(const QRectF &localCrop, const QRectF &contentRect,
                                         const QPointF &itemOffset, int imageW, int imageH,
                                         bool hFlip, bool vFlip) const;

    static void applyScenePosDelta(ImageItem *item, const QPointF &delta);

    void seedApplyCropState(WorkspaceItemState *st, const QPointF &itemOffset,
                            const QSize &imageSize) const;

    void restoreEnterScale(ImageItem *item) const;

    static void applyKeepEnterFlags(ImageItem *item, const WorkspaceItemState &contentOnly,
                                    const ContentXform::Value &wantX);

    static QSize cropBasisSize(const QSize &imageSize, const QSize &fileNative,
                               const WorkspaceItemState *orientFromAppearance,
                               const ImageItem *item);

    void applyRecordToState(WorkspaceItemState *s, const RecordGeometry &rec,
                            const QSize &cropBasis) const;

    bool applyPaddedAutoTrim(const QRectF &contentRect, const QSize &srcSize,
                             const QRect &trimmed, int padPx = 2);

    void queueFullRematerializeIfSoft(bool hostFromCache, bool multiMp,
                                      const QString &path, SessionImageId sid,
                                      const WorkspaceItemState &st);

    static QImage pickEnterSnapshotPixels(const ImageItem *item);

    static void applyEnterDraftFlags(ImageItem *item, const ContentXform::Value &wantX);

    bool shouldPushResetUndo(const QSize &currentSourceSize) const;

    static void clearItemPixelsForDraftReinstall(ImageItem *item);

    static bool maybePutUnorientedHostCache(const QString &path, const QImage &full,
                                            bool unorientedSource, bool coversNative);

    void finishRubber(const QRectF &contentRect);

    bool shouldAcceptFullRasterUpgrade(const QString &path, const QImage &image,
                                       const ImageItem *item, bool coversNative) const;

    void applyCommitPlacementRotation(ImageItem *item) const;

    static QSize ensureApplyIntrinsicSize(ImageItem *item, qreal cropW, qreal cropH,
                                          const QString &pathForLog);

    static bool appearanceHasCrop(const WorkspaceItemState *app, bool haveApp);

    enum class ApplyHostStatus { Ok, NeedFull, NoPixels };
    static ApplyHostStatus classifyApplyHost(const QImage &host, bool hostFromCache,
                                             const ImageItem *item);

    void releaseAllTileLod(ImageItem *boundByIdFallback);

    static SessionAppearance::PixelKind applyPixelKind(bool multiMp);

    /** Fill hasCrop/cropRect/content flips from the live item session crop. */
    static bool fillAppearanceFromItemSessionCrop(WorkspaceItemState *app,
                                                  const ImageItem *item);

    /** True when enter host is null and a prior crop should force a full load. */
    static bool shouldRequestFullOnNullEnter(bool hadPriorCrop, const QString &path);

    /** End handle drag with content clamp (no-op content when empty). */
    void finishHandleDrag(const QRectF &contentRect);

    /**
     * Restore free placement from appearance, or zero it in Image mode.
     * Always clears item H/V flip (content flips live in appearance bake).
     */
    static void applyItemPlacementFromState(ImageItem *item,
                                            const WorkspaceItemState &app,
                                            bool imageModeZeroPose);

    bool isHandleHot(CropHandle h) const
    {
        return hoverHandle == h || activeHandle == h;
    }

    bool isHandleDragging() const { return activeHandle != CropHandle::None; }

    bool hasHoverHandle() const { return hoverHandle != CropHandle::None; }
    bool isActive() const { return mode; }

    /**
     * True when install/ladder must not replace the draft sample for @p path.
     * Uses draftPath and targetItem path; does not walk the scene by session id.
     * Defined in cropsession.cpp (needs complete ImageItem).
     */
    bool locksPath(const QString &path) const;

    /**
     * Path lock including host-resolved bound item path when draftPath is empty.
     * @p boundItemPath from cropSessionBoundItem()->path().
     */
    bool locksResolvedPath(const QString &path, const QString &boundItemPath) const
    {
        if (locksPath(path) || isDraftSampleFrozenForPath(path)) {
            return true;
        }
        if (!isDraftSampleFrozen() || path.isEmpty()) {
            return false;
        }
        return !boundItemPath.isEmpty() && boundItemPath == path;
    }


    /**
     * True when @p item is the draft subject (pointer, session id, or path).
     * Host may still lock via targetId → other item path lookup.
     * Defined in cropsession.cpp (needs complete ImageItem).
     */
    bool locksItem(const ImageItem *item) const;

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

    /** @return true when hover handle changed. */
    bool setHoverHandle(CropHandle h)
    {
        if (hoverHandle == h) {
            return false;
        }
        hoverHandle = h;
        return true;
    }

    void beginHandleDrag(CropHandle h, const QRectF &startRect, const QPointF &startLocal);

    void setRotateStart(qreal rotationDeg, qreal angleDeg)
    {
        rotateStartRotation = rotationDeg;
        rotateStartAngle = angleDeg;
    }

    void endHandleDrag() { activeHandle = CropHandle::None; }

    void endHandleDragClamped(const QRectF &contentRect)
    {
        endHandleDrag();
        ensureRectValid(contentRect);
    }


    /** Expand limits when allowExpand (4× content padding). */
    QRectF expandLimits(const QRectF &contentRect) const;

    void applyMoveDrag(const QPointF &local, const QRectF &contentRect);
    void applyRotateDrag(const QPointF &local, const QRectF &contentRect, qreal minSide,
                         bool shiftSnap, bool ctrlSnap);
    void applyResizeDrag(const QPointF &local, const QRectF &contentRect, const QRectF &limits,
                         qreal minSide, bool fromCenter, bool forceSquare);

    /** Dispatch move/rotate/resize for the current active handle. */
    void applyActiveHandleDrag(const QPointF &local, const QRectF &contentRect, qreal minSide,
                               bool shiftSnap, bool ctrlSnap);


    /** Integer session crop rect from draft (content-local minus item offset). */
    QRect integerCropForOffset(const QPointF &itemOffset) const;

    /** Integer crop in unflipped source space (flip-aware). */
    QRect sourceCropFromLocal(const QRectF &local, const QPointF &itemOffset,
                              int imageW, int imageH, bool hFlip, bool vFlip) const;




    /** Start rubber-band: origin + zero-size axis-aligned draft. */
    void beginRubberDraft(const QPointF &originLocal);

    void applyRubberBand(const QPointF &local, const QRectF &contentRect,
                         bool shiftSnap, bool ctrlFromCenter);

    void beginRubber(const QPointF &originLocal)
    {
        rubberBanding = true;
        rubberOriginLocal = originLocal;
    }

    void endRubber() { rubberBanding = false; }

    bool isRubberbanding() const { return rubberBanding; }

    bool isShowingFullImage() const { return showingFullImage; }

    bool isAllowExpand() const { return allowExpand; }


    void bindTarget(ImageItem *item, SessionImageId sid, const QString &path)
    {
        targetItem = item;
        targetId = sid;
        draftPath = path;
        draftSampleFrozen = true;
    }

    void clearTargetBinding()
    {
        targetItem = nullptr;
        targetId = kInvalidSessionImageId;
        draftSampleFrozen = false;
        draftPath.clear();
    }

    void setEnterSnapshot(const QImage &source, const WorkspaceItemState &state, bool valid)
    {
        enterSource = source;
        enterState = state;
        enterValid = valid;
    }

    void clearEnterSnapshot()
    {
        enterValid = false;
        enterSource = {};
        enterState = {};
    }

    /**
     * Bind target, enter snapshot, and zero placement rotation for crop grips.
     * Host still owns PathRaster cancel and full-frame install.
     * Suppresses tile LOD on the bound item until leave.
     */
    void beginEnterSession(ImageItem *item, const QImage &enterSrc,
                           const WorkspaceItemState &enterSt, bool snapshotValid);

    /** Copy live session crop flags from @p item into enter-state bag. */
    static void seedEnterCropFlags(WorkspaceItemState *st, const ImageItem *item);

    void stashPlacement(qreal rot, qreal shear)
    {
        stashedPlacementRotation = rot;
        stashedPlacementShear = shear;
        hadStashedPlacement = qAbs(rot) > 0.05 || qAbs(shear) > 1e-4;
    }

    /** Apply stashed placement rotation/shear to @p item when stash is valid. */
    void restoreStashedPlacement(ImageItem *item) const;

    /** After Apply/Cancel: restore placement unless commit kept crop-frame rotation. */
    void finishLeave(ImageItem *item, bool preserveCropFrameRotation) const
    {
        if (item && !preserveCropFrameRotation) {
            restoreStashedPlacement(item);
        }
    }


    /** Restore item pos/scale from enter-stash (Workspace cancel path). */
    void restoreEnterPlacementPose(ImageItem *item) const;


    void clearPlacementStash()
    {
        stashedPlacementRotation = 0.0;
        stashedPlacementShear = 0.0;
        hadStashedPlacement = false;
    }

    /** Failed prepare: drop mode, target, enter stash, placement stash, interaction. */
    void abortEnter()
    {
        mode = false;
        clearTargetBinding();
        clearEnterSnapshot();
        clearPlacementStash();
        clearInteraction();
    }

    /** Restore placement stash on item then abort enter (failed prepare). */
    void abortEnterRestoringPlacement(ImageItem *item)
    {
        if (item) {
            restoreStashedPlacement(item);
        }
        abortEnter();
    }

    /**
     * Capture pending full rematerialize and clear those fields.
     * @return true if a bake was pending.
     */
    bool takePendingFullRematerialize(QString *path, SessionImageId *sid,
                                      WorkspaceItemState *want)
    {
        const bool pending = pendingFullRematerialize;
        if (path) {
            *path = pending ? pendingFullRematerializePath : QString();
        }
        if (sid) {
            *sid = pending ? pendingFullRematerializeSid : kInvalidSessionImageId;
        }
        if (want) {
            *want = pending ? pendingFullRematerializeWant : WorkspaceItemState{};
        }
        pendingFullRematerialize = false;
        pendingFullRematerializePath.clear();
        pendingFullRematerializeSid = kInvalidSessionImageId;
        pendingFullRematerializeWant = {};
        return pending;
    }

    void queuePendingFullRematerialize(const QString &path, SessionImageId sid,
                                       const WorkspaceItemState &want)
    {
        pendingFullRematerialize = true;
        pendingFullRematerializePath = path;
        pendingFullRematerializeSid = sid;
        pendingFullRematerializeWant = want;
    }

    void clearAwaitingFull() { awaitingFullPath.clear(); }

    /** @return true when the awaiting-full path changed. */
    bool setAwaitingFull(const QString &path)
    {
        if (awaitingFullPath == path) {
            return false;
        }
        awaitingFullPath = path;
        return true;
    }

    /** @return true when the flag changed. */
    bool setShowingFullImage(bool on)
    {
        if (showingFullImage == on) {
            return false;
        }
        showingFullImage = on;
        return true;
    }

    /** Toggle allow-expand; @return true when the flag changed. */
    bool toggleAllowExpand()
    {
        return setAllowExpand(!allowExpand);
    }

    /** @return true when allow-expand flag changed. */
    bool setAllowExpand(bool on)
    {
        if (allowExpand == on) {
            return false;
        }
        allowExpand = on;
        return true;
    }

    /** @return true when rotation changed. */
    bool setRotation(qreal deg)
    {
        if (qFuzzyCompare(rotation, deg)) {
            return false;
        }
        rotation = deg;
        return true;
    }

    /** Fold rotation into (-180, 180]. */
    void normalizeRotation()
    {
        while (rotation > 180.0) {
            rotation -= 360.0;
        }
        while (rotation <= -180.0) {
            rotation += 360.0;
        }
    }

    bool isNearZeroRotation(qreal eps = 1e-3) const
    {
        return qAbs(rotation) < eps;
    }

    /** @return true when the draft rect changed. */
    bool setRect(const QRectF &r)
    {
        if (rect == r) {
            return false;
        }
        rect = r;
        return true;
    }

    /**
     * True when draft matches @p contentRect (axis-aligned full frame within 0.5px).
     */
    bool isFullFrameDraft(const QRectF &contentRect) const
    {
        if (!hasValidRect()) {
            return true;
        }
        const QRectF &r = rect;
        return qAbs(r.left() - contentRect.left()) < 0.5
            && qAbs(r.top() - contentRect.top()) < 0.5
            && qAbs(r.width() - contentRect.width()) < 0.5
            && qAbs(r.height() - contentRect.height()) < 0.5;
    }

    bool hasValidRect() const { return rect.isValid() && !rect.isEmpty(); }

    /**
     * Normalize draft and constrain to @p contentRect (unless allowExpand).
     * Pure geometry — host supplies contentRect from the target item.
     */
    void ensureRectValid(const QRectF &contentRect);

    /** Expand draft to full content (Reset chrome); clears rotation. */
    void resetDraftToContent(const QRectF &contentRect);

    /**
     * Clamp @p local to content when expand is off; normalize.
     * @return empty if result is too small.
     */
    QRectF clampLocalCrop(const QRectF &local, const QRectF &contentRect) const;



    /**
     * Map a source-pixel trim rect into content space and set as axis-aligned draft.
     * Clears expand and rotation. Caller must ensureRectValid afterward if needed.
     */
    void setRectFromSourcePixelTrim(const QRectF &contentRect, const QSize &srcSize,
                                    const QRect &trimmed);

    /** Draft → source pixel search box (intersected with source bounds). */
    QRect sourceSearchRectFromDraft(const QRectF &contentRect, const QSize &srcSize) const;

    /**
     * If draft rotation is near zero but placement stash has free rotation,
     * seed draft rotation from the stash. @return true when rotation changed.
     */
    bool seedRotationFromStashedPlacement(qreal freeRotationEps);



    /** Draft corners in item-local content space. */
    QPolygonF polygonLocal() const;

    /**
     * Seed draft from prior session crop appearance or full @p contentRect.
     * @p itemOffset is ImageItem::offset(); @p imageSize is layout intrinsic.
     */
    void initRectFromPriorAppearance(const QRectF &contentRect,
                                     const QPointF &itemOffset,
                                     const QSize &imageSize,
                                     const WorkspaceItemState *app,
                                     bool haveApp);


    QRectF normalizedRect() const { return rect.normalized(); }

    const QRectF &currentRect() const { return rect; }

    QPointF draftCenterLocal() const { return rect.center(); }

    QRectF draftRectOr(const QRectF &fallback) const
    {
        return hasValidRect() ? currentRect() : fallback;
    }


    /** Scene footprint of the draft at placement scales (Workspace Apply). */
    void draftFootprint(qreal itemScaleX, qreal itemScaleY,
                        qreal *cropW, qreal *cropH, qreal *footW, qreal *footH) const
    {
        const qreal w = currentRect().width();
        const qreal h = currentRect().height();
        if (cropW) {
            *cropW = w;
        }
        if (cropH) {
            *cropH = h;
        }
        if (footW) {
            *footW = w * itemScaleX;
        }
        if (footH) {
            *footH = h * (itemScaleY > 0.0 ? itemScaleY : itemScaleX);
        }
    }



    qreal currentRotation() const { return rotation; }

    const QRectF &dragStartRectRef() const { return dragStartRect; }

    const QPointF &dragStartLocalRef() const { return dragStartLocal; }

    qreal rotateStartRotationValue() const { return rotateStartRotation; }

    qreal rotateStartAngleValue() const { return rotateStartAngle; }

    CropHandle currentActiveHandle() const { return activeHandle; }

    CropHandle currentHoverHandle() const { return hoverHandle; }

    bool isDraftSampleFrozen() const { return draftSampleFrozen; }

    /** True when draft freeze applies to @p path (path lock or draftPath). */
    bool isDraftSampleFrozenForPath(const QString &path) const
    {
        if (!draftSampleFrozen || path.isEmpty()) {
            return false;
        }
        return locksPath(path) || (!draftPath.isEmpty() && draftPath == path);
    }


    const QImage &enterSourceRef() const { return enterSource; }

    const QString &awaitingFullPathRef() const { return awaitingFullPath; }

    bool isAwaitingFullPath(const QString &path) const
    {
        return !path.isEmpty() && awaitingFullPath == path;
    }

    /** Active crop waiting on this path for a better full-raster delivery. */
    bool acceptsFullRasterUpgrade(const QString &path) const
    {
        return mode && isAwaitingFullPath(path);
    }

    /** Draft size in integer content pixels (size badge / Apply). */
    QSize draftPixelSize() const;

    const QString &draftPathRef() const { return draftPath; }

    SessionImageId targetIdValue() const { return targetId; }

    bool hasStashedPlacement() const { return hadStashedPlacement; }

    qreal stashedPlacementRotationValue() const { return stashedPlacementRotation; }

    qreal stashedPlacementShearValue() const { return stashedPlacementShear; }

    // Placement stash is rotation/shear only (see stashPlacement).

    bool isEnterValid() const { return enterValid; }

    const WorkspaceItemState &enterStateRef() const { return enterState; }

    /** Enter-stash placement scales (identity if enter invalid). */
    qreal enterScaleX() const
    {
        return isEnterValid() && enterState.scale > 1e-6 ? enterState.scale : 0.0;
    }
    qreal enterScaleY() const
    {
        if (!isEnterValid()) {
            return 0.0;
        }
        return enterState.scaleY > 1e-6 ? enterState.scaleY : enterScaleX();
    }

    QPointF enterPos() const
    {
        return isEnterValid() ? enterState.pos : QPointF();
    }

    bool enterHadCrop() const
    {
        return isEnterValid() && enterState.hasCrop;
    }

    bool enterSourceDiffersFrom(const QSize &sz) const
    {
        return isEnterValid() && enterSource.size() != sz;
    }



    const QPointF &rubberOriginLocalRef() const { return rubberOriginLocal; }

    bool hasTargetId() const { return targetId != kInvalidSessionImageId; }

    ImageItem *target() const { return targetItem; }

    /**
     * Full-frame draft is on the item: turn mode on and clear interaction.
     * Called after prepareCropModeFullImage succeeds.
     */
    void activateModeAfterDraft()
    {
        mode = true;
        clearInteraction();
    }

    /** @return true when crop mode flag changed. */
    bool setMode(bool on)
    {
        if (mode == on) {
            return false;
        }
        mode = on;
        return true;
    }

    /**
     * Full leave / session wipe: inactive, no target, no enter stash, no pending
     * full rematerialize. Does not touch ImageItem pixels.
     */
    /** Clear tile LOD suppress on the bound target pointer (if any). */
    void releaseTargetTileLod();

    void clear()
    {
        mode = false;
        clearTargetBinding();
        takePendingFullRematerialize(nullptr, nullptr, nullptr);
        allowExpand = false;
        rotation = 0.0;
        showingFullImage = false;
        awaitingFullPath.clear();
        clearPlacementStash();
        clearEnterSnapshot();
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
