// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Gallery packaged-layout size gate host (GallerySizeResolveHost).

#include "gallery/gallerycontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "item/imagesizebook.h"
#include "host/thumtoocache.h"
#include "util/biltoo_logging.h"
#include "util/biltoo_thread.h"
#include "gallery/gallerylayout.h"

#include <QTimer>
#include <QGraphicsView>

bool GalleryController::hasDefinitiveHostSize(const QString &path) const
{
    return m_view->hostSizeBook().hasDefinitive(path);
}

void GalleryController::adoptResolvedSize(const QString &path, const QSize &size)
{
    m_view->rememberImageSize(path, size);
    m_view->applyProbedImageSize(path, size);
}

void GalleryController::adoptSizeProbeFailed(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_view->hostSizeBook().markFailed(path);
    if (const QSize native = m_view->hostSizeBook().contains(path)
            ? m_view->logicalSizeForPath(path)
            : QSize(256, 256);
        isPositiveSize(native)) {
        m_view->applyProbedImageSize(path, native);
    }
    for (ImageItem *item : m_view->liveItems()) {
        if (item && item->path() == path) {
            item->setToolTip(
                m_view->tr("Failed to read image size:\n%1").arg(path));
        }
    }
}

void GalleryController::scheduleSizeProbeBatch(const QStringList &paths)
{
    ThumtooCache::scheduleProbeBatch(paths);
}

QStringList GalleryController::sizeResolvePathOrder() const
{
    return m_view->currentPackOrder().paths();
}

bool GalleryController::sizeResolveLayoutDefersPopulate() const
{
    return layoutDefersPopulateUntilSizes(m_view->hostLayout().currentMode());
}

bool GalleryController::layoutDefersPopulateUntilSizes(LayoutMode mode)
{
    if (mode == LayoutMode::FreeForm) {
        return false;
    }
    return true;
}

void GalleryController::setSizeResolveProgress(const QString &title,
                                              const QString &detail)
{
    m_view->setCentreProgress(title, detail);
}

void GalleryController::clearSizeResolveProgress()
{
    if (m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Resolving sizes"))) {
        m_view->clearCentreProgress();
    }
}

void GalleryController::onSizeResolvePathSettled(const QString &path)
{
    Q_UNUSED(path);
    if (!m_view->isGalleryMode() || !m_sizeResolve.active()) {
        return;
    }
    scheduleSizeGatePlanRefresh();
}

void GalleryController::onSizeResolveGateComplete()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("GalleryController::onSizeResolveGateComplete");
    m_view->clearCentreProgress();
    if (m_view->isGalleryMode()) {
        m_view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    m_view->hostGalleryDecodeBook().setDeferPopulate(false);
    if (m_view->isGalleryMode() && !m_view->pathOrderIsEmpty()) {
        for (ImageItem *item : m_view->liveItems()) {
            if (item) {
                item->setVisible(false);
            }
        }
        const bool more = ensurePlaceholders();
        if (!more && !m_view->liveItems().isEmpty() && !m_view->hostLayout().isFreeForm()) {
            applyLayout(GalleryPackReason::EnterGallery);
            for (ImageItem *item : m_view->liveItems()) {
                if (item) {
                    item->setVisible(true);
                }
            }
            updateDecodeWindow();
            QPointer<ImageView> guard(m_view);
            QTimer::singleShot(0, m_view, [guard]() {
                ImageView *view = guard.data();
                if (view && view->isGalleryMode() && !view->liveItems().isEmpty()) {
                    view->hostGallery().updateDecodeWindow();
                }
            });
        }
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    m_view->notifyStatusChanged();
    m_view->notifyGallerySizeResolveFinished();
}

void GalleryController::onSizeResolveGateCancelled()
{
    m_view->hostGalleryDecodeBook().setDeferPopulate(false);
    if (m_view->isGalleryMode() && m_view->hostCentreProgress().titleRef().isEmpty()) {
        m_view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    if (m_view->isGalleryMode()) {
        int hidden = 0;
        for (ImageItem *item : m_view->liveItems()) {
            if (item && !item->isVisible()) {
                item->setVisible(true);
                ++hidden;
            }
        }
        if (m_view->liveItems().isEmpty() && !m_view->pathOrderIsEmpty()) {
            ensurePlaceholders();
            biltooModeDbg("sizeResolve CANCEL ensurePlaceholders items=%d pathOrder=%d",
                          m_view->itemCount(),
                          static_cast<int>(m_view->currentPackOrder().size()));
        } else if (hidden > 0) {
            biltooModeDbg("sizeResolve CANCEL unhide n=%d items=%d",
                          hidden, m_view->itemCount());
            if (m_view->viewport()) {
                m_view->viewport()->update();
            }
        }
    }
    m_view->clearCentreProgress();
    m_view->notifyGallerySizeResolveFinished();
}
