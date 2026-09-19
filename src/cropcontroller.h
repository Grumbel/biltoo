// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPCONTROLLER_H
#define CROPCONTROLLER_H

#include "cropsession.h"
#include "crophandle.h"
#include "cropflash.h"
#include "cropgeometry.h"
#include "imageview_types.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <Qt>

class ImageView;
class ImageItem;
class QPainter;
class QMouseEvent;

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

    ImageItem *cropSessionBoundItem() const;
    ImageItem *cropTargetItem() const;
    bool isCropDraftLockedItem(const ImageItem *item) const;
    bool isCropDraftLockedPath(const QString &path) const;
    void cancelPathRasterForCrop(const QString &path);
    void fitImageOrUpdateWorkspace(ImageItem *item);
    void relayoutAfterCropLeave(ImageItem *item);
    void ensureCropRectValid();
    void alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor);
    void toggleCropMode();
    void requestCropViewportUpdate();
    void applyAutoCrop();
    void flashCropHud(const CropFlash::Hud &hud);
    void applyCrop();
    void cancelCrop();
    SessionImageId cropRecordSessionId(const ImageItem *item) const;
    void recordSessionCrop(ImageItem *item, const QRectF &localCrop);
    void pushCropAppearanceUndo(ImageItem *item, const QString &text);
    bool applyCropCommit(ImageItem *item);
    void leaveCropModeInternal(bool apply);
    bool enterCropModeFromUi();
    void setCropMode(bool on);
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
    void paintCropOverlay(QPainter &painter);
    void onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                            quint64 gen);
    void requestCropFullRaster(const QString &path);
    void maybeUpgradeCropFullRaster(const QString &path, const QImage &image);

    CropGeometry::CropButtonLayout cropChromeLayout() const;
    CropHandle cropHandleAt(const QPoint &viewPos) const;
    QPolygonF cropPolygonView() const;
    QPointF itemLocalFromView(ImageItem *item, const QPoint &viewPos) const;
    QPolygonF mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const;

    bool tryMousePressCrop(QMouseEvent *event);
    bool tryMouseMoveCropDrag(QMouseEvent *event);
    bool tryMouseMoveCropHover(QMouseEvent *event);

private:
    ImageView *m_view = nullptr; // not owned
    CropSession m_crop;
};

#endif // CROPCONTROLLER_H
