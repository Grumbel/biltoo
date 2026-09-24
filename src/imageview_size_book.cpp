// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// GallerySizeResolve host + contentLayoutSize. Book/probe: ImageSizeCoordinator; applyProbed on pipeline.

#include "imageview.h"
#include "content/contentxform.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"
#include "host/archivepath.h"
#include "host/pagepath.h"
#include "imageitem.h"
#include "host/imageloader.h"
#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "session/sessionappearance.h"
#include "util/biltoo_logging.h"
#include "util/biltoo_thread.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>


QSize ImageView::contentLayoutSize(const QString &path, SessionImageId sessionId,
                                   bool allowStoreAppearance) const
{
    WorkspaceItemState boundWant;
    const WorkspaceItemState *boundPtr = nullptr;
    bool hasBoundDurable = false;
    bool hasContentOrient = false;
    if (sessionId != kInvalidSessionImageId && hasSessionAppearance(sessionId)) {
        boundWant = sessionAppearanceValue(sessionId);
        boundPtr = &boundWant;
        hasBoundDurable = true;
        hasContentOrient = m_itemWorld.hasContentOrient(sessionId);
    }
    const WorkspaceItemState *pathState = nullptr;
    if (sessionId == kInvalidSessionImageId && !path.isEmpty()) {
        pathState = m_itemWorld.getPathState(path);
    }
    return SessionAppearance::resolveContentLayoutSize(
        logicalSizeForPath(path),
        m_size.book().known(path),
        allowStoreAppearance ? ThumtooCache::cachedSize(path) : QSize(),
        allowStoreAppearance,
        sessionId,
        path,
        hasBoundDurable,
        boundPtr,
        hasContentOrient,
        pathState);
}


void ImageView::applyProbedImageSize(const QString &path, const QSize &size)
{
    m_displayPipeline->applyProbedImageSize(path, size);
}


























void ImageView::setCentreProgress(const QString &title, const QString &detail)
{
    m_shell.setCentreProgress(title, detail);
}

void ImageView::clearCentreProgress()
{
    m_shell.clearCentreProgress();
}

// --- Logical size (was imageview_view.cpp) ---


// --- Size book / probe: ImageSizeCoordinator (thin forwards) ---

void ImageView::rememberImageSize(const QString &path, const QSize &size)
{
    m_size.rememberImageSize(path, size);
}

void ImageView::rememberSizeFromDecode(const QString &path, const QImage &image)
{
    m_size.rememberSizeFromDecode(path, image);
}

QSize ImageView::imageSizeForPath(const QString &path)
{
    return m_size.imageSizeForPath(path);
}

QSize ImageView::layoutSizeForPath(const QString &path, const QImage &previewHint)
{
    return m_size.layoutSizeForPath(path, previewHint);
}

void ImageView::primeGalleryGeometryFromCache(const QStringList &paths)
{
    m_size.primeGalleryGeometryFromCache(paths);
}

void ImageView::scheduleImageSizeProbe(const QString &path)
{
    m_size.scheduleImageSizeProbe(path);
}

QSize ImageView::logicalSizeForPath(const QString &path) const
{
    return m_size.logicalSizeForPath(path);
}

QSize ImageView::ensureLogicalSizeForPath(const QString &path)
{
    return m_size.ensureLogicalSizeForPath(path);
}
