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
    // Ground truth: native × ItemWorld content ops (same as filmstrip provider).
    const QSize native = SessionAppearance::pickNativeSize(
        logicalSizeForPath(path),
        m_size.book().known(path),
        allowStoreAppearance ? ThumtooCache::cachedSize(path) : QSize(),
        allowStoreAppearance);
    if (!(native.width() > 1 && native.height() > 1)) {
        // Callers still call layoutSizeForPath which schedules probes.
        return native; // may be empty/provisional
    }
    WorkspaceItemState want;
    if (sessionId != kInvalidSessionImageId && hasSessionAppearance(sessionId)) {
        want = sessionAppearanceValue(sessionId);
        // Placement/color-only durable row is not content orient — same strip as
        // installDisplayPixels / createItemFromImage (2205–2211).
        want = SessionAppearance::orientAuthorityWant(
            m_itemWorld.hasContentOrient(sessionId), want);
    } else if (sessionId == kInvalidSessionImageId && !path.isEmpty()) {
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(path)) {
            want = *st;
        }
    }
    // Bound: never path XDG. Unbound path rows may still use full XDG including crop.
    // Virtual plan / bulk open: never Store loadContentAppearance (GUI freeze).
    if (SessionAppearance::shouldAugmentFromPathStore(
            allowStoreAppearance, SessionAppearance::hasContentAppearance(want),
            path.isEmpty(), sessionId)) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored) && !stored.isIdentity()) {
            SessionAppearance::applyStoredContentAppearance(&want, stored, false);
        }
    }
    return SessionAppearance::layoutSizeOrNative(native, want);
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
