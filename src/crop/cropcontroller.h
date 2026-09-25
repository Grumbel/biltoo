// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPCONTROLLER_H
#define CROPCONTROLLER_H

#include "crop/cropsession.h"
#include "crop/crophandle.h"
#include "crop/cropflash.h"
#include "crop/cropgeometry.h"
#include "crop/croprecipe.h"
#include "imageview_types.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QList>
#include <Qt>

class ImageView;
class ImageItem;
class QPainter;
class QMouseEvent;
class QKeyEvent;

/**
 * Crop-mode collaborator for ImageView (Phase 6 Tier 2b).
 *
 * Owns CropSession and enter/apply/leave / paint / input / raster orchestration.
 * ImageView keeps thin forwards for MainWindow and input routing.
 */
class CropController
{
public:

    explicit CropController(ImageView *view);

    ImageView *view() const { return m_view; }

    CropSession &session() { return m_crop; }
    const CropSession &session() const { return m_crop; }

    bool active() const { return m_crop.active(); }

    bool isCropDraftLockedItem(const ImageItem *item) const;
    bool isCropDraftLockedPath(const QString &path) const;
    void fitImageOrUpdateWorkspace(ImageItem *item);

    /** Crop appearance store/restore/apply (was ImageView). ImageView routers stay for undo/host. */
    void storeCropAppearance(ImageItem *item, SessionImageId sid, const WorkspaceItemState &s);
    bool loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                   SessionImageId *sidOut) const;
    void restoreSessionCropAppearance(ImageItem *item);
    void applyCropAppearance(ImageItem *item, const QImage &src, const WorkspaceItemState &state);
    void emitCropApplyAppearance(SessionImageId sid, const QString &path, ImageItem *item,
                                 const QImage &preferredDisplay, bool hasCrop);

    /**
     * Batch / panel crop: apply @p recipe to transformTargets (or single current).
     * GUI-safe; autocrop uses item pixels / ImageCache only (no sync full load).
     * @return number of items written.
     */
    int applyCropRecipeToTargets(const CropPanelRecipe &recipe);
    /** Apply recipe to an explicit target list (e.g. single current page). */
    int applyCropRecipeToItems(const CropPanelRecipe &recipe, const QList<ImageItem *> &targets);
    /** Clear crop fields on transformTargets (or current); keeps orient/colour. */
    int resetCropOnTargets();
    int resetCropOnItems(const QList<ImageItem *> &targets);
    /**
     * Soft live preview on one item: rematerialize with recipe crop, no undo,
     * no durable ItemWorld write. Apply commits via applyCropRecipeTo*.
     * @return true when a usable crop was shown.
     */
    bool previewCropRecipeOnItem(ImageItem *item, const CropPanelRecipe &recipe);

    void cancelCrop();
    void leaveCropModeInternal(bool apply);
    void setCropMode(bool on);
    void paintCropOverlay(QPainter &painter);
    void onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                            quint64 gen);
    void maybeUpgradeCropFullRaster(const QString &path, const QImage &image);


    bool tryMousePressCrop(QMouseEvent *event);
    bool tryMouseMoveCropDrag(QMouseEvent *event);
    bool tryMouseMoveCropHover(QMouseEvent *event);
    bool tryMouseReleaseCrop(QMouseEvent *event);
    bool tryKeyPressCrop(QKeyEvent *event);


private:
    // Enter/apply/paint helpers (no external callers)
    ImageItem *cropSessionBoundItem() const;
    ImageItem *cropTargetItem() const;
    void cancelPathRasterForCrop(const QString &path);
    void relayoutAfterCropLeave(ImageItem *item);
    void ensureCropRectValid();
    void alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor);
    void toggleCropMode();
    void requestCropViewportUpdate();
    void applyAutoCrop();
    void flashCropHud(const CropFlash::Hud &hud);
    void applyCrop();
    SessionImageId cropRecordSessionId(const ImageItem *item) const;
    void recordSessionCrop(ImageItem *item, const QRectF &localCrop);
    void pushCropAppearanceUndo(ImageItem *item, const QString &text);
    bool applyCropCommit(ImageItem *item);
    bool enterCropModeFromUi();
    bool prepareCropModeFullImage(ImageItem *item);
    QRectF cropRectView() const;
    void beginCropHandleDrag(CropHandle h, const QPoint &viewPos);
    void cropKeyboardMods(bool *shiftHeld, bool *ctrlHeld);
    void updateCropHandleDrag(const QPoint &viewPos);
    void endCropHandleDrag();
    bool contentLocalContains(ImageItem *item, const QPointF &local) const;
    void beginCropRubberBand(const QPoint &viewPos);
    void updateCropRubberBand(const QPoint &viewPos);
    void finishCropRubberBand();
    void endCropRubberBand();
    void requestCropFullRaster(const QString &path);
    CropGeometry::CropButtonLayout cropChromeLayout() const;
    CropHandle cropHandleAt(const QPoint &viewPos) const;
    QPolygonF cropPolygonView() const;
    QPointF itemLocalFromView(ImageItem *item, const QPoint &viewPos) const;
    QPolygonF mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const;

    ImageView *m_view = nullptr; // not owned
    CropSession m_crop;
};

#endif // CROPCONTROLLER_H
