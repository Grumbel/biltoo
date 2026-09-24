// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYPIPELINEHOST_H
#define DISPLAYPIPELINEHOST_H

#include "imageview_types.h"
#include "content/contentxform.h"
#include "color/coloradjust.h"

#include <QList>
#include <QSize>
#include <QString>

class ItemWorld;
class ImageItem;
class SessionDocument;
class SessionIdentity;
class ImageSizeBook;
class PathRasterService;
class SessionBindBook;
class SessionSeedBook;
class SlideshowController;
class CropController;
class GalleryController;
class ImageController;
class GalleryDecodeBook;
class ViewFraming;
class GallerySizeResolve;
class QGraphicsScene;
class QWidget;
class QObject;

/**
 * Canvas / session surface for DisplayPipelineController.
 *
 * Dual ImageView (0.3): more than one view may implement this host. Shared
 * session state (ItemWorld, SessionDocument, books, path raster) and
 * controller bags are reached through the host rather than a single concrete
 * ImageView*.
 *
 * Stage 0: dual-critical session/canvas state.
 * Stage 1: controller hosts, appearance/framing helpers, status notify,
 *          primary/target items — shrinks DisplayPipelineController::view().
 *
 * Follows the GallerySizeResolveHost / TileNeighborPrefetchHost pattern:
 * collaborator owns behaviour; host is a narrow virtual surface.
 *
 * @see docs/IMAGEVIEW_ITEM_OWNERSHIP.md § Dual ImageView
 */
class DisplayPipelineHost
{
public:
    virtual ~DisplayPipelineHost() = default;

    // --- Stage 0: dual-critical session / canvas ---
    virtual ItemWorld &itemWorld() = 0;
    virtual const ItemWorld &itemWorld() const = 0;

    virtual QList<ImageItem *> &liveItems() = 0;
    virtual const QList<ImageItem *> &liveItems() const = 0;

    virtual QGraphicsScene *canvasScene() = 0;

    virtual bool isImageMode() const = 0;
    virtual bool isGalleryMode() const = 0;
    virtual bool isWorkspaceMode() const = 0;

    virtual SessionDocument *sessionDocument() const = 0;

    virtual SessionIdentity &hostSessionId() = 0;
    virtual const SessionIdentity &hostSessionId() const = 0;

    virtual ImageSizeBook &hostSizeBook() = 0;
    virtual const ImageSizeBook &hostSizeBook() const = 0;

    virtual PathRasterService *hostPathRaster() = 0;
    virtual const PathRasterService *hostPathRaster() const = 0;

    virtual SessionBindBook &hostBindBook() = 0;
    virtual const SessionBindBook &hostBindBook() const = 0;

    virtual SessionSeedBook &hostSeedBook() = 0;
    virtual const SessionSeedBook &hostSeedBook() const = 0;

    /** Logical image size for @p path (never soft-raster dimensions). */
    virtual QSize logicalSizeForPath(const QString &path) const = 0;

    /** QGraphicsView viewport for update/geometry; may be null. */
    virtual QWidget *viewportWidget() = 0;

    // --- Stage 1: controller hosts ---
    virtual SlideshowController &hostSlideshow() = 0;
    virtual const SlideshowController &hostSlideshow() const = 0;

    virtual CropController &hostCrop() = 0;
    virtual const CropController &hostCrop() const = 0;

    virtual GalleryDecodeBook &hostGalleryDecodeBook() = 0;
    virtual const GalleryDecodeBook &hostGalleryDecodeBook() const = 0;

    virtual GalleryController &hostGallery() = 0;
    virtual const GalleryController &hostGallery() const = 0;

    virtual ImageController &hostImage() = 0;
    virtual const ImageController &hostImage() const = 0;

    virtual ViewFraming &hostFraming() = 0;
    virtual const ViewFraming &hostFraming() const = 0;

    virtual GallerySizeResolve &hostGallerySizeResolve() = 0;
    virtual const GallerySizeResolve &hostGallerySizeResolve() const = 0;

    // --- Stage 1: appearance / identity / framing helpers ---
    virtual WorkspaceItemState sessionAppearanceValue(SessionImageId id) const = 0;
    virtual bool itemHasAppliedContentXform(const ImageItem *item) const = 0;
    virtual ContentXform::Value itemAppliedContentXform(const ImageItem *item) const = 0;
    virtual void setItemSessionId(ImageItem *item, SessionImageId id) = 0;
    virtual void syncLiveContentMetaFromState(ImageItem *item,
                                              const WorkspaceItemState &state) = 0;
    virtual void syncLiveColorFromState(ImageItem *item, const ColorAdjustments &grade,
                                        bool rebuildDisplay = false) = 0;
    virtual void clearLiveContentMeta(ImageItem *item) = 0;
    virtual QSize contentLayoutSize(const QString &path, SessionImageId sessionId,
                                    bool allowStoreAppearance = true) const = 0;
    virtual int sessionListIndex(const ImageItem *item) const = 0;

    virtual ImageItem *primaryItem() const = 0;
    virtual ImageItem *targetItem() const = 0;

    virtual void applyImageModeFraming(ImageItem *item) = 0;
    virtual void syncImageModeSceneRect(ImageItem *item) = 0;
    virtual void captureStickyPanAnchor(ImageItem *item) = 0;

    virtual void setUpdatesEnabled(bool enabled) = 0;

    /** Emit statusChanged (and optional workspacePathsChanged). */
    virtual void notifyStatusChanged() = 0;
    virtual void notifyWorkspacePathsChanged() = 0;

    /**
     * QObject for timer parent / QPointer lifetime (typically the ImageView).
     * Stage 1 still needs a QObject identity for async guards.
     */
    virtual QObject *hostObject() = 0;
};

#endif // DISPLAYPIPELINEHOST_H
