// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYPIPELINEHOST_H
#define DISPLAYPIPELINEHOST_H

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
class QGraphicsScene;
class QWidget;

/**
 * Canvas / session surface for DisplayPipelineController.
 *
 * Dual ImageView (0.3): more than one view may implement this host. Shared
 * session state (ItemWorld, SessionDocument, size/bind/seed books, path
 * raster) is reached through the host rather than a single concrete
 * ImageView*. Stage 0 covers the dual-critical surface; long-tail view APIs
 * remain on DisplayPipelineController::view() until Stage 1.
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
};

#endif // DISPLAYPIPELINEHOST_H
