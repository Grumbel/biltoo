// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEITEM_H
#define IMAGEITEM_H

#include "imageview_types.h"
#include "coloradjust.h"
#include "contentxform.h"
#include "iteminteractsession.h"
#include "itemhandle.h"
#include <QGraphicsPixmapItem>
#include <QColor>
#include <QImage>
#include <QCache>
#include <QHash>
#include <QString>
#include <QPolygonF>
#include <QRect>
#include <cstdint>
#include <memory>

#include "tilelod/tile_types.hpp"
#include "tilelod/tile_lod_item_bag.hpp"

/**
 * A single image on the workspace. Owns its pixmap, source pixels (for colour
 * sampling), and local scale/rotation/flip applied around the item centre.
 *
 * Geometry (pixmap + item transform) is independent of interaction chrome.
 * Scale/rotation/flip live in QGraphicsItem::transform; chrome is painted in
 * device/viewport pixels so anisotropic scale never stretches the controls.
 * Hit-testing compares view-pixel distances to the same logical handle centres.
 */
class ImageItem : public QGraphicsPixmapItem
{
public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    /** Workspace chrome handle; type lives in itemhandle.h (Stage 2 share). */
    using Handle = ItemHandle;

    explicit ImageItem(const QString &path, const QImage &image,
                       QGraphicsItem *parent = nullptr);
    /** Placeholder tile (Gallery virtualization) — geometry from @p intrinsicSize. */
    explicit ImageItem(const QString &path, const QSize &intrinsicSize,
                       QGraphicsItem *parent = nullptr);
    ~ImageItem() override;

    QString path() const { return m_path; }
    void setPath(const QString &path);
    /**
     * Stable session-image id (0 = unbound). Survives session insert/delete;
     * list index does not. Identity for appearance and Workspace association.
     */
    SessionImageId sessionId() const { return m_sessionId; }
    void setSessionId(SessionImageId id) { m_sessionId = id; }
    /**
     * @deprecated Order in the session list only — shifts on insert/delete.
     * Prefer sessionId() for identity.
     */
    int sessionIndex() const { return m_sessionIndex; }
    void setSessionIndex(int index) { m_sessionIndex = index; }
    /** DisplaySurfaceController id (0 = unbound). Host install policy key. */
    qint64 displaySurfaceId() const { return m_displaySurfaceId; }
    void setDisplaySurfaceId(qint64 id) { m_displaySurfaceId = id; }
    QSize imageSize() const;
    /**
     * Set logical layout size (probe / layout / crop). Does not touch pixels.
     * Always applied — samples never block this write.
     */
    void setIntrinsicSize(const QSize &size);
    const QImage &sourceImage() const { return m_source; }
    /**
     * Active display sample: full source when decoded, else soft preview.
     * May be null. Does not transfer ownership.
     */
    const QImage &displayImage() const
    {
        return hasDecodedPixels() ? m_source : m_preview;
    }
    const QImage &previewImage() const { return m_preview; }
    /**
     * True when a full (non-preview) decode is present. Soft preview pixels do
     * not count — gallery still schedules a higher ladder / full load.
     */
    bool hasDecodedPixels() const { return !m_source.isNull() && !m_previewPixels; }
    /** Any displayable pixels (full decode or soft preview). */
    bool hasDisplayPixels() const { return !m_source.isNull() || !m_preview.isNull(); }
    /** Long edge of current display pixels (0 if none). */
    int displayPixelLongEdge() const;
    /**
     * True when @a incomingLongEdge is strictly sharper than what we show
     * (or we have no pixels). Soft ladder upgrades use this.
     */
    bool shouldUpgradeDisplayTo(int incomingLongEdge) const;
    /** Replace or clear decoded pixels; keeps path and intrinsic size. */
    void setSourceImage(const QImage &image);
    /**
     * Assign display-ready pixels (appearance already baked). Does not
     * re-apply item-level flip/grade; skips multi-MP QPixmap::fromImage.
     */
    void setSourceImageReady(const QImage &image);
    /**
     * Low-res stand-in while a full decode is in flight. Does not change
     * intrinsic geometry; painted scaled into contentRect().
     */
    void setPreviewImage(const QImage &preview);
    void clearDecodedPixels();

    /** Uniform scale factor (geometric mean of X/Y); prefer itemScaleX/Y when anisotropic. */
    qreal itemScale() const;
    qreal itemScaleX() const { return m_scaleX; }
    qreal itemScaleY() const { return m_scaleY; }
    /** Horizontal shear factor k in R·H·S (0 = no shear). */
    qreal itemShear() const { return m_shear; }
    /**
     * Workspace *placement* rotation only (free-rotate handle).
     * Content 90° turns are baked into pixels — not stored here.
     */
    qreal itemRotation() const { return m_rotation; }

    /** Live Workspace pose as Placement (Stage 2 single reader for item pose). */
    ItemComponents::Placement placement() const;
    /** Apply Workspace pose (Stage 2 single writer for item pose). */
    void applyPlacement(const ItemComponents::Placement &pl);
    qreal itemOpacity() const { return m_opacity; }
    /** Persistent stacking order (selection may temporarily raise the item). */
    qreal stackZ() const { return m_stackZ; }
    /** Item-local pixmap/content rect (no chrome pad). */
    QRectF contentRect() const;
    /**
     * Local rect of painted content — same as contentRect() (logical geometry).
     * Soft samples fill the layout box; selection frames use this.
     */
    QRectF displayContentRect() const;
    /** Scene AABB of the pixmap only (no chrome pad). */
    QRectF contentSceneRect() const;
    /** Content quad in scene coordinates (respects item scale/rotation). */
    QPolygonF contentScenePolygon() const;
    bool itemHFlip() const { return m_hFlip; }
    bool itemVFlip() const { return m_vFlip; }
    /**
     * Net content (baked) flips relative to the on-disk pixels after crop.
     * Used by Workspace chrome so flip toggle buttons show the current state
     * after bakeFlip clears the transient display flags.
     */
    bool contentHFlip() const { return m_contentHFlip; }
    bool contentVFlip() const { return m_contentVFlip; }
    void setContentHFlip(bool on) { m_contentHFlip = on; }
    void setContentVFlip(bool on) { m_contentVFlip = on; }
    void setColorAdjustments(const ColorAdjustments &adj);
    /** Store grade for HUD without rebuilding the display pixmap. */
    void setColorAdjustmentsRecord(const ColorAdjustments &adj);
    ColorAdjustments colorAdjustments() const { return m_colorAdjust; }
    /**
     * Per-instance session crop in on-disk pixel coordinates (top-left origin).
     * Independent of path-keyed ImageView state so Workspace duplicates do not
     * share crops.
     */
    bool sessionHasCrop() const { return m_sessionHasCrop; }

    /**
     * Content xform that was applied to the current display sample.
     * Compared on install so rematerialize is driven by data, not aspect heuristics.
     */
    ContentXform::Value appliedContentXform() const { return m_appliedContentXform; }
    void setAppliedContentXform(const ContentXform::Value &x)
    {
        m_appliedContentXform = x;
        m_hasAppliedContentXform = true;
        clearTileGradedCache();
    }
    void clearAppliedContentXform()
    {
        m_appliedContentXform = {};
        m_hasAppliedContentXform = false;
        clearTileGradedCache();
    }
    bool hasAppliedContentXform() const { return m_hasAppliedContentXform; }

    QRect sessionCropRect() const { return m_sessionCropRect; }
    void setSessionCrop(bool has, const QRect &rect)
    {
        m_sessionHasCrop = has;
        m_sessionCropRect = has ? rect : QRect();
    }

    /** Bake ±90° into source pixels; placement angle unchanged. */
    void bakeRotate90(int quarterTurns);
    /** Bake horizontal/vertical mirror into source pixels; clears flip flags. */
    void bakeFlip(bool horizontal, bool vertical);
    void toggleHFlip();
    void toggleVFlip();
    void zoomBy(qreal factor);
    void rotateBy(qreal degrees);

    /**
     * Crop displayed content to @p localRect (item coordinates, contentRect space).
     * Bakes current H/V flips into the new source pixels and clears flip flags.
     * Returns false if the rect is empty or outside the image.
     */
    bool cropToLocalRect(const QRectF &localRect,
                         const QColor &padColor = QColor(0, 0, 0, 0),
                         qreal rotationDegrees = 0.0);

    /** When false, the item cannot be selected or dragged (classic viewer). */
    void setInteractive(bool on);
    bool isInteractive() const { return m_interactive; }

    /**
     * Gallery (packaged) layout: selectable but not movable; no on-canvas chrome.
     * Free-form workspace uses setInteractive(true) instead.
     */
    void setGallerySelectable(bool on);
    /**
     * Rebuild item paint cache after selection/content change.
     * Gallery uses ItemCoordinateCache (survives view scroll); this toggles
     * the mode so the next paint is not frozen.
     */
    void invalidateDeviceCache();

    /**
     * Gallery scroll path: bake display sample into QPixmap and enable
     * ItemCoordinateCache so OpenGL+scroll does not re-stretch soft every frame.
     * No-op for interactive (Workspace) items. Tile-LOD cells stay NoCache.
     */
    void syncGalleryScrollCache();

    /**
     * Gallery Grid-Crop: visible area is a centred cell of this size in *scene*
     * units (after item scale). Empty size clears cropping.
     */
    void setGalleryCellSize(const QSizeF &sceneSize);
    QSizeF galleryCellSize() const { return m_galleryCellSize; }

    /**
     * When false, corner scale (resize) handles are neither drawn nor hit-tested.
     * Used for fixed packaged layouts where scale is driven by the layout.
     */
    void setScaleHandlesEnabled(bool on);
    bool scaleHandlesEnabled() const { return m_scaleHandlesEnabled; }

    /** Map a scene position to integer pixel coordinates, or (-1,-1) if outside. */
    QPoint pixelAtScenePos(const QPointF &scenePos) const;
    QColor colorAtPixel(const QPoint &pixel) const;

    /** Which handle (if any) is under the given item-local position. */
    Handle handleAt(const QPointF &itemPos) const;

    /** Call after view zoom so handle hit areas/bounds stay correct. */
    void updateHandleLayout();

    /** True when a scale/rotate/chrome handle is under item-local @p itemPos. */
    bool hasHandleAt(const QPointF &itemPos) const;

    /**
     * View-driven handle interaction (bypasses QGraphicsItem shape delivery).
     * Used so rotated / anisotropically scaled chrome stays hittable even when
     * shape() alone would miss the device-space controls.
     */
    /**
     * Start continuous handle drag. Writes press scratch to @p outPress when
     * a scale/shear/rotate/opacity handle is armed (outPress->hasContinuousHandle()).
     * One-shot chrome buttons (flip/raise/…) return true without arming drag.
     */
    bool beginHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods,
                                HandlePressScratch *outPress = nullptr);
    void updateHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods,
                                 HandlePressScratch &press);
    /**
     * Finish continuous handle drag. @p continuous is the press-time handle
     * (HandlePressScratch::handle); geometry commit skipped for None / OpacitySlider.
     */
    void endHandleInteraction(Handle continuous = Handle::None);

    /** Paint transform chrome in device pixels (identity world transform). */
    void paintInteractionChrome(QPainter *painter) const;
    /** Selection outline only (multi-select); no scale/rotate/chrome buttons. */
    void paintSelectionFrame(QPainter *painter) const;
    /** View menu: corner marks for orient / grade / crop (default on). */
    static void setContentEditMarksVisible(bool on);
    static bool contentEditMarksVisible();
    /** View-driven hover highlight for chrome (keeps highlight in sync with hits). */
    void setHoverHandle(Handle h);
    Handle hoverHandle() const { return m_hoverHandle; }
    /** Translated tooltip for a handle (empty if none). */
    static QString handleToolTip(Handle h);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

    /** True when on-screen need exceeds soft max (tiles should own display). */
    bool tileLodWanted() const;
    bool tileLodSuppressed() const { return tileLodBag().suppressed; }
    /** True when at least one grid tile has arrived. */
    bool tileLodActive() const;
    /** Succeeded tiles in global path RAM (registry), even without a controller. */
    bool tileLodHasPathRam() const;
    /** All exact visible tiles present and scale hold settled. */
    bool tileLodViewportCovered() const;
    /** One-line BILTOO_TILE_DEBUG sample (empty if no session). */
    QString tileLodDebugLine() const;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    void applyLocalTransform();
    void updateDisplayedPixmap();
    void refreshStackingOrder();
    void notifyViewStatus();
    /** Local clip rect for gallery crop, or empty if none. */
    QRectF galleryClipLocal() const;
    /** Item-local centre of a handle (pre-transform local coordinates). */
    QPointF handleCenter(Handle h) const;
    /** Hit radius in item-local units for scale/rotate handles. */
    qreal handleHitRadius() const;
    qreal handleDrawSize() const;
    /**
     * Max singular value of (view × item) linear transform: screen px per local unit
     * along the most stretched axis. Used for drawing chrome at ~constant screen size.
     */
    qreal screenScale() const;
    /**
     * Min singular value of (view × item): local radius that still covers a given
     * screen-pixel hit target under rotation / anisotropic scale.
     */
    qreal deviceScaleMin() const;
    bool isChromeHandle(Handle h) const;
    bool isRotateHandle(Handle h) const;
    void drawCornerBracket(QPainter *painter, const QPointF &deviceCentre,
                           qreal dx, qreal dy, qreal armPx, bool hot) const;
    void paintInteractionChrome(QPainter *painter, const QRectF &localRect) const;
    /** Raise/Lower keep screen-upright glyphs (counter-rotated when painting). */
    bool isUprightChromeHandle(Handle h) const;
    void activateChromeHandle(Handle h);
    QRectF opacitySliderRect() const;
    qreal chromeButtonSize() const;
    /** Distance in screen pixels from itemPos to handle centre. */
    qreal handleDistanceScreenPx(Handle h, const QPointF &itemPos) const;
    void setOpacityFromSliderPos(const QPointF &scenePos);
    /** View-pixel position of an item-local point (first attached view). */
    QPointF localToViewPx(const QPointF &local) const;
    QPointF sceneToViewPx(const QPointF &scene) const;
    QList<Handle> activeHandles() const;

    QString m_path;
    // Tile session mutators — DisplayPipelineController / CropSession only (Stage 2).
    friend class DisplayPipelineController;
    friend class CropSession;
    void dropTileLodSession();
    void invalidateTilePathRam();
    void tickTileLod(int budget = 8);
    void setTileLodSuppressed(bool on);
    /** Plan/paint helpers (ImageItem paint + tick only). */
    void prepareTileLod();
    void prepareTileLodPlan();
    qreal tileDevicePerContent() const;
    ContentXform::Value tileContentXform() const;
    QSize tileNativeSize() const;
    void clearTileGradedCache() const;
    QImage resolveGradedTile(tilelod::TileKey const &key,
                             ColorAdjustments const &grade) const;
    /** Single access path for tile runtime state (ownership demotion prep). */
    tilelod::ItemBag &tileLodBag() { return m_tileLod; }
    const tilelod::ItemBag &tileLodBag() const { return m_tileLod; }

    /** Deep-zoom grid tiles + plan/paint scratch (Stage 2 bag; demote later). */
    tilelod::ItemBag m_tileLod;
    SessionImageId m_sessionId = kInvalidSessionImageId;
    int m_sessionIndex = -1; // list order cache only
    qint64 m_displaySurfaceId = 0;
    QImage m_source;
    ColorAdjustments m_colorAdjust;
    /** Valid when m_source is null (placeholder) or as size cache. */
    QSize m_intrinsicSize;
    /** Downscaled stand-in; intrinsic size stays full resolution. */
    QImage m_preview;
    bool m_previewPixels = false;
    qreal m_scaleX = 1.0;
    qreal m_scaleY = 1.0;
    qreal m_shear = 0.0;
    qreal m_rotation = 0.0;
    qreal m_opacity = 1.0;
    qreal m_stackZ = 0.0;
    bool m_hFlip = false;
    bool m_vFlip = false;
    /** Net baked content flips (chrome indicator); independent of m_hFlip/m_vFlip. */
    bool m_contentHFlip = false;
    static bool s_contentEditMarksVisible;
    bool m_contentVFlip = false;
    bool m_sessionHasCrop = false;
    QRect m_sessionCropRect;
    ContentXform::Value m_appliedContentXform;
    bool m_hasAppliedContentXform = false;
    bool m_interactive = false;
    bool m_scaleHandlesEnabled = false;
    /** Scene-space crop cell for Grid-Crop gallery; empty = no crop. */
    QSizeF m_galleryCellSize;

    /** View-driven hover + continuous-drag paint hot (set at begin continuous). */
    Handle m_hoverHandle = Handle::None;
    /** Gallery: item under the mouse (no transform chrome). */
    bool m_galleryHovered = false;
    bool isScaleHandle(Handle h) const;
    bool isShearHandle(Handle h) const;
    bool isCornerScaleHandle(Handle h) const;
    bool isEdgeScaleHandle(Handle h) const;
    QPointF scaleAnchorLocal(Handle h) const;
    /** Field mutators — only applyPlacement may write live pose (Stage 2). */
    void setItemScale(qreal scale);
    void setItemScale(qreal scaleX, qreal scaleY);
    void setItemShear(qreal shear);
    void setItemRotation(qreal degrees);
    void setItemOpacity(qreal opacity);
    void setItemHFlip(bool on);
    void setItemVFlip(bool on);
    void setStackZ(qreal z);

    void applyScaleHandleDrag(const QPointF &scenePos, Qt::KeyboardModifiers mods,
                               HandlePressScratch &press);
    void applyShearHandleDrag(const QPointF &scenePos, HandlePressScratch &press);
};

#endif // IMAGEITEM_H
