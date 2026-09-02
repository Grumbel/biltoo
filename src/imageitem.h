// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEITEM_H
#define IMAGEITEM_H

#include "imageview_types.h"
#include "coloradjust.h"
#include <QGraphicsPixmapItem>
#include <QColor>
#include <QImage>
#include <QString>
#include <QPolygonF>
#include <QRect>

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

    enum class Handle {
        None,
        ScaleTopLeft,
        ScaleTopRight,
        ScaleBottomLeft,
        ScaleBottomRight,
        /** Edge stretch (non-uniform): change only one axis. */
        ScaleTop,
        ScaleRight,
        ScaleBottom,
        ScaleLeft,
        RotateTop,
        RotateRight,
        RotateBottom,
        RotateLeft,
        FlipH,
        FlipV,
        /** 90° object rotation (Workspace chrome). */
        Rotate90CCW,
        Rotate90CW,
        Raise,
        Lower,
        ResetScale,
        ResetRotation,
        OpacitySlider
    };

    explicit ImageItem(const QString &path, const QImage &image,
                       QGraphicsItem *parent = nullptr);
    /** Placeholder tile (Gallery virtualization) — geometry from @p intrinsicSize. */
    explicit ImageItem(const QString &path, const QSize &intrinsicSize,
                       QGraphicsItem *parent = nullptr);

    QString path() const { return m_path; }
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
    QSize imageSize() const;
    const QImage &sourceImage() const { return m_source; }
    bool hasDecodedPixels() const { return !m_source.isNull(); }
    /** Replace or clear decoded pixels; keeps path and intrinsic size. */
    void setSourceImage(const QImage &image);
    void clearDecodedPixels();

    /** Uniform scale factor (geometric mean of X/Y); prefer itemScaleX/Y when anisotropic. */
    qreal itemScale() const;
    qreal itemScaleX() const { return m_scaleX; }
    qreal itemScaleY() const { return m_scaleY; }
    /**
     * Workspace *placement* rotation only (free-rotate handle).
     * Content 90° turns are baked into pixels — not stored here.
     */
    qreal itemRotation() const { return m_rotation; }
    /** @deprecated content is baked; always 0 after bake pipeline. */
    qreal itemOrientation() const { return m_orientation; }
    qreal itemFineRotation() const { return m_fineRotation; }
    qreal itemOpacity() const { return m_opacity; }
    /** Persistent stacking order (selection may temporarily raise the item). */
    qreal stackZ() const { return m_stackZ; }
    /** Item-local pixmap/content rect (no chrome pad). */
    QRectF contentRect() const;
    /** Scene AABB of the pixmap only (no chrome pad). */
    QRectF contentSceneRect() const;
    /** Content quad in scene coordinates (respects item scale/rotation). */
    QPolygonF contentScenePolygon() const;
    void setStackZ(qreal z);
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
    ColorAdjustments colorAdjustments() const { return m_colorAdjust; }
    /**
     * Per-instance session crop in on-disk pixel coordinates (top-left origin).
     * Independent of path-keyed ImageView state so Workspace duplicates do not
     * share crops.
     */
    bool sessionHasCrop() const { return m_sessionHasCrop; }
    QRect sessionCropRect() const { return m_sessionCropRect; }
    void setSessionCrop(bool has, const QRect &rect)
    {
        m_sessionHasCrop = has;
        m_sessionCropRect = has ? rect : QRect();
    }

    /** Set both axes to the same factor (gallery layouts, zoom-by). */
    void setItemScale(qreal scale);
    void setItemScale(qreal scaleX, qreal scaleY);
    /** Workspace placement angle (free rotate). Does not touch pixels. */
    void setItemRotation(qreal degrees);
    /** Bake ±90° into source pixels; placement angle unchanged. */
    void bakeRotate90(int quarterTurns);
    /** Bake horizontal/vertical mirror into source pixels; clears flip flags. */
    void bakeFlip(bool horizontal, bool vertical);
    /** @deprecated use bakeRotate90 */
    void rotateOrientationBy(qreal degrees);
    void setOrientation(qreal degrees);
    void setFineRotation(qreal degrees);
    void setItemOpacity(qreal opacity);
    void setItemHFlip(bool on);
    void setItemVFlip(bool on);
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
    bool beginHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void updateHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void endHandleInteraction();
    bool hasActiveHandle() const { return m_activeHandle != Handle::None; }

    /** Paint transform chrome in device pixels (identity world transform). */
    void paintInteractionChrome(QPainter *painter) const;
    /** Selection outline only (multi-select); no scale/rotate/chrome buttons. */
    void paintSelectionFrame(QPainter *painter) const;
    /** View-driven hover highlight for chrome (keeps highlight in sync with hits). */
    void setHoverHandle(Handle h);
    Handle hoverHandle() const { return m_hoverHandle; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

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
    SessionImageId m_sessionId = kInvalidSessionImageId;
    int m_sessionIndex = -1; // list order cache only
    QImage m_source;
    ColorAdjustments m_colorAdjust;
    /** Valid when m_source is null (placeholder) or as size cache. */
    QSize m_intrinsicSize;
    qreal m_scaleX = 1.0;
    qreal m_scaleY = 1.0;
    qreal m_rotation = 0.0;
    /** Cardinal base (multiples of 90). */
    qreal m_orientation = 0.0;
    /** Free tilt relative to orientation (Workspace). */
    qreal m_fineRotation = 0.0;
    qreal m_opacity = 1.0;
    qreal m_stackZ = 0.0;
    bool m_hFlip = false;
    bool m_vFlip = false;
    /** Net baked content flips (chrome indicator); independent of m_hFlip/m_vFlip. */
    bool m_contentHFlip = false;
    bool m_contentVFlip = false;
    bool m_sessionHasCrop = false;
    QRect m_sessionCropRect;
    bool m_interactive = false;
    bool m_scaleHandlesEnabled = false;
    /** Scene-space crop cell for Grid-Crop gallery; empty = no crop. */
    QSizeF m_galleryCellSize;

    Handle m_activeHandle = Handle::None;
    Handle m_hoverHandle = Handle::None;
    /** Gallery: item under the mouse (no transform chrome). */
    bool m_galleryHovered = false;
    QPointF m_pressScenePos;
    qreal m_pressScaleX = 1.0;
    qreal m_pressScaleY = 1.0;
    qreal m_pressRotation = 0.0;
    QPointF m_pressItemPos;
    /** Scene position of the fixed anchor (opposite corner/edge) at press. */
    QPointF m_pressAnchorScene;
    /** Local-space anchor point kept fixed when not scaling from centre. */
    QPointF m_pressAnchorLocal;
    bool isScaleHandle(Handle h) const;
    bool isCornerScaleHandle(Handle h) const;
    bool isEdgeScaleHandle(Handle h) const;
    QPointF scaleAnchorLocal(Handle h) const;
    void applyScaleHandleDrag(const QPointF &scenePos, Qt::KeyboardModifiers mods);
};

#endif // IMAGEITEM_H
