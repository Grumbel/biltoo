// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Panel / multi-select crop apply (non-interactive; no crop-mode draft).

#include "crop/cropcontroller.h"
#include "crop/croprecipe.h"

#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "session/sessionappearance.h"
#include "display/displaypipelinecontroller.h"
#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "host/imageloader.h"
#include "item/imagesizebook.h"
#include "gallery/gallerycontroller.h"
#include "util/biltoo_thread.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmap>
#include <QProgressDialog>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QUndoStack>
#include <QAtomicInt>
#include "util/contentundomacro.h"

namespace {

QList<ImageItem *> cropBatchTargets(ImageView *view)
{
    QList<ImageItem *> targets = view->transformTargets();
    if (targets.isEmpty()) {
        ImageItem *item = view->targetItem();
        if (!item && view->isImageMode() && !view->liveItems().isEmpty()) {
            item = view->liveItems().first();
        }
        if (item) {
            targets.append(item);
        }
    }
    return targets;
}

QSize logicalSizeForCrop(ImageView *view, ImageItem *item, const QImage &sampleHint)
{
    if (!view || !item) {
        return {};
    }
    const QString path = item->path();
    const SessionImageId sid = view->hostResolveContentEditSessionId(item);

    if (sid != kInvalidSessionImageId && view->itemWorld().hasCrop(sid)) {
        const ItemComponents::Crop c = view->itemWorld().crop(sid);
        if (c.sourceSize.isValid() && c.sourceSize.width() > 0 && c.sourceSize.height() > 0) {
            return c.sourceSize;
        }
    }
    {
        WorkspaceItemState st = view->freezeItemAppearance(item);
        if (st.cropSourceSize.isValid() && st.cropSourceSize.width() > 0
            && st.cropSourceSize.height() > 0) {
            return st.cropSourceSize;
        }
    }

    QSize native = ThumtooCache::cachedSize(path);
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        native = view->hostSizeBook().known(path);
    }
    if (SessionAppearance::isUsableNativeSize(native)) {
        return native;
    }

    if (!view->itemHasAppliedContentXform(item)
        || !view->itemAppliedContentXform(item).hasCrop) {
        const QSize isz = item->imageSize();
        if (isz.width() > 1 && isz.height() > 1) {
            return isz;
        }
    }

    // Soft sample aspect is acceptable when native size is still unknown.
    if (!sampleHint.isNull() && sampleHint.width() > 1 && sampleHint.height() > 1) {
        return sampleHint.size();
    }
    return {};
}

/** Deep-copied item pixels when already on the tile. */
QImage sampleFromItem(ImageItem *item)
{
    if (!item) {
        return {};
    }
    QImage src = item->sourceImage();
    if (src.isNull()) {
        const QPixmap pm = item->pixmap();
        if (!pm.isNull()) {
            src = pm.toImage();
        }
    }
    if (src.isNull()) {
        return {};
    }
    return src.copy();
}

/**
 * Soft stand-in edge for autocrop detect. Full native is unnecessary; margin
 * trim on ≤512 matches Gallery soft band and keeps workers cheap.
 */
constexpr int kAutocropSampleEdge = 512;

/**
 * Load soft samples for paths that lack item pixels. Work runs off-GUI into a
 * private map (not ImageCache) so planning never races the host cache hash.
 * @return path → sample (missing entries stayed unloadable).
 */
QHash<QString, QImage> loadAutocropSamples(const QStringList &paths,
                                           QWidget *progressParent,
                                           ImageView *hudView)
{
    QHash<QString, QImage> out;
    if (paths.isEmpty()) {
        return out;
    }

    // Dedupe while preserving order for progress labels.
    QStringList unique;
    QSet<QString> seen;
    unique.reserve(paths.size());
    for (const QString &p : paths) {
        if (p.isEmpty() || seen.contains(p)) {
            continue;
        }
        seen.insert(p);
        unique.append(p);
    }
    if (unique.isEmpty()) {
        return out;
    }

    // Warm host cache in parallel (optional; results collected privately).
    for (const QString &p : unique) {
        (void)ImageCache::ensure(p, kAutocropSampleEdge);
    }

    QMutex mu;
    QAtomicInt finished(0);
    const int total = unique.size();

    for (const QString &path : unique) {
        // Prefer a ready host sample without decoding again.
        {
            QImage hit = ImageCache::get(path, /*minLongEdge=*/0);
            if (!hit.isNull()) {
                QMutexLocker lock(&mu);
                out.insert(path, hit.copy());
                finished.fetchAndAddRelaxed(1);
                continue;
            }
        }
        QThreadPool::globalInstance()->start([path, &mu, &out, &finished]() {
            ASSERT_NOT_GUI_THREAD();
            QImage img = ImageLoader::loadThumbnail(path, kAutocropSampleEdge);
            if (img.isNull()) {
                // Last resort: any smaller host sample that appeared meanwhile.
                img = ImageCache::get(path);
            }
            if (!img.isNull()) {
                QMutexLocker lock(&mu);
                out.insert(path, img.copy());
            }
            finished.fetchAndAddRelaxed(1);
        });
    }

    QProgressDialog progress(
        QCoreApplication::translate("CropController", "Loading samples for autocrop…"),
        QCoreApplication::translate("CropController", "Cancel"),
        0, total, progressParent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(200);
    progress.setValue(0);

    while (finished.loadRelaxed() < total) {
        if (progress.wasCanceled()) {
            break;
        }
        const int done = finished.loadRelaxed();
        progress.setValue(done);
        progress.setLabelText(
            QCoreApplication::translate("CropController",
                                        "Loading samples for autocrop… %1 / %2")
                .arg(done)
                .arg(total));
        if (hudView) {
            hudView->hostHud().centreProgress().set(
                QCoreApplication::translate("CropController", "Autocrop"),
                QStringLiteral("%1 / %2").arg(done).arg(total));
            if (hudView->viewport()) {
                hudView->viewport()->update();
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(15);
    }
    progress.setValue(total);
    if (hudView) {
        hudView->hostHud().centreProgress().clear();
        if (hudView->viewport()) {
            hudView->viewport()->update();
        }
    }

    QMutexLocker lock(&mu);
    return out;
}

struct PlannedCrop {
    ImageItem *item = nullptr;
    QString path;
    SessionImageId sid = kInvalidSessionImageId;
    QSize logical;
    QRect crop;
    WorkspaceItemState beforeSt;
    QImage beforeSrc;
};

} // namespace

int CropController::applyCropRecipeToTargets(const CropPanelRecipe &recipe)
{
    return applyCropRecipeToItems(recipe, cropBatchTargets(m_view));
}

int CropController::applyCropRecipeToItems(const CropPanelRecipe &recipe,
                                           const QList<ImageItem *> &targets)
{
    if (!m_view || targets.isEmpty()) {
        return 0;
    }

    // Phase 0 (autocrop): gather soft samples for tiles that have no pixels yet.
    QHash<QString, QImage> loadedSamples;
    if (recipe.mode == CropPanelRecipe::Mode::Autocrop) {
        QStringList needLoad;
        for (ImageItem *item : targets) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            if (sampleFromItem(item).isNull()) {
                needLoad.append(item->path());
            }
        }
        if (!needLoad.isEmpty()) {
            QWidget *parent = m_view->window();
            loadedSamples = loadAutocropSamples(needLoad, parent, m_view);
        }
    }

    // Phase 1: pure plan — no ItemWorld / pixel mutation.
    QList<PlannedCrop> plan;
    plan.reserve(targets.size());
    int skippedNoSample = 0;
    int skippedNoSize = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QString path = item->path();

        QImage sample;
        if (recipe.mode == CropPanelRecipe::Mode::Autocrop) {
            sample = sampleFromItem(item);
            if (sample.isNull() && !path.isEmpty()) {
                sample = loadedSamples.value(path);
            }
            if (sample.isNull()) {
                ++skippedNoSample;
                continue;
            }
        }

        const QSize logical = logicalSizeForCrop(m_view, item, sample);
        if (logical.width() < 1 || logical.height() < 1) {
            ++skippedNoSize;
            continue;
        }

        const QRect crop = CropRecipeUtil::computeCropRect(recipe, logical, sample);
        if (!CropRecipeUtil::isUsableCrop(crop, logical)) {
            continue;
        }

        PlannedCrop p;
        p.item = item;
        p.path = path;
        p.sid = m_view->hostResolveContentEditSessionId(item);
        p.logical = logical;
        p.crop = crop;
        p.beforeSrc = item->sourceImage().copy();
        p.beforeSt = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(p.beforeSt, item->placement());
        plan.append(p);
    }

    if (plan.isEmpty()) {
        if (skippedNoSample > 0 || skippedNoSize > 0) {
            m_view->hostHud().showFlash(
                m_view->tr("Autocrop"),
                m_view->tr("No samples yet (%1) · no size (%2)")
                    .arg(skippedNoSample)
                    .arg(skippedNoSize),
                [v = m_view]() {
                    if (v && v->viewport()) {
                        v->viewport()->update();
                    }
                });
        }
        return 0;
    }

    ContentUndoMacro macro(
        m_view->hostUndoStack(),
        m_view->tr("Crop (%1)").arg(plan.size()),
        plan.size());

    // Phase 2: apply with light progress when N is large.
    QProgressDialog applyProgress(
        m_view->tr("Applying crop…"),
        QString(), // no cancel mid-write (undo reverses the macro)
        0, plan.size(), m_view->window());
    applyProgress.setWindowModality(Qt::WindowModal);
    applyProgress.setMinimumDuration(300);
    applyProgress.setCancelButton(nullptr);

    int n = 0;
    for (PlannedCrop &p : plan) {
        ImageItem *item = p.item;
        if (!item) {
            continue;
        }

        WorkspaceItemState afterSt = m_view->freezeItemAppearance(item);
        ItemComponents::applyPlacementToState(afterSt, item->placement());
        afterSt.hasCrop = true;
        afterSt.cropRect = p.crop;
        afterSt.cropSourceSize = p.logical;
        afterSt.cropRotation = 0.0;
        afterSt.sessionId = p.sid != kInvalidSessionImageId
            ? p.sid
            : m_view->hostResolveContentEditSessionId(item);
        afterSt.path = p.path.isEmpty() ? item->path() : p.path;

        applyCropAppearance(item, QImage(), afterSt);
        m_view->hostDisplayPipeline().rematerializeItemContent(item, afterSt);

        WorkspaceItemState afterSnap = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(afterSnap, item->placement());
        afterSnap.hasCrop = true;
        afterSnap.cropRect = p.crop;
        afterSnap.cropSourceSize = p.logical;
        afterSnap.sessionId = afterSt.sessionId;

        m_view->pushItemContentCommand(
            m_view->tr("Crop"), item, p.beforeSrc,
            item->sourceImage().copy(), p.beforeSt, afterSnap);
        ++n;
        applyProgress.setValue(n);
        if ((n & 7) == 0) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        }
    }
    applyProgress.setValue(plan.size());

    if (n > 0 && m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    if (n > 0) {
        emit m_view->statusChanged();
        if (skippedNoSample > 0 || skippedNoSize > 0) {
            m_view->hostHud().showFlash(
                m_view->tr("Crop"),
                m_view->tr("Applied %1 · skipped no-sample %2 · no-size %3")
                    .arg(n)
                    .arg(skippedNoSample)
                    .arg(skippedNoSize),
                [v = m_view]() {
                    if (v && v->viewport()) {
                        v->viewport()->update();
                    }
                });
        }
    }
    return n;
}

int CropController::resetCropOnTargets()
{
    return resetCropOnItems(cropBatchTargets(m_view));
}

int CropController::resetCropOnItems(const QList<ImageItem *> &targets)
{
    if (!m_view || targets.isEmpty()) {
        return 0;
    }

    struct PlannedReset {
        ImageItem *item = nullptr;
        SessionImageId sid = kInvalidSessionImageId;
        WorkspaceItemState beforeSt;
        QImage beforeSrc;
    };

    QList<PlannedReset> plan;
    plan.reserve(targets.size());
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        WorkspaceItemState beforeSt = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(beforeSt, item->placement());
        const SessionImageId sid = m_view->hostResolveContentEditSessionId(item);
        if (!beforeSt.hasCrop && beforeSt.cropRect.isEmpty()) {
            if (sid == kInvalidSessionImageId || !m_view->itemWorld().hasCrop(sid)) {
                continue;
            }
        }
        PlannedReset p;
        p.item = item;
        p.sid = sid;
        p.beforeSt = beforeSt;
        p.beforeSrc = item->sourceImage().copy();
        plan.append(p);
    }

    if (plan.isEmpty()) {
        return 0;
    }

    ContentUndoMacro macro(
        m_view->hostUndoStack(),
        m_view->tr("Reset crop (%1)").arg(plan.size()),
        plan.size());

    int n = 0;
    for (PlannedReset &p : plan) {
        ImageItem *item = p.item;
        if (!item) {
            continue;
        }

        WorkspaceItemState afterSt = p.beforeSt;
        afterSt.hasCrop = false;
        afterSt.cropRect = QRect();
        afterSt.cropSourceSize = QSize();
        afterSt.cropRotation = 0.0;
        afterSt.sessionId = p.sid;
        afterSt.path = item->path();

        applyCropAppearance(item, QImage(), afterSt);
        m_view->hostDisplayPipeline().rematerializeItemContent(item, afterSt);

        WorkspaceItemState afterSnap = m_view->captureContentBakeBeforeState(item);
        ItemComponents::applyPlacementToState(afterSnap, item->placement());
        afterSnap.hasCrop = false;
        afterSnap.cropRect = QRect();
        afterSnap.cropSourceSize = QSize();
        afterSnap.sessionId = afterSt.sessionId;

        m_view->pushItemContentCommand(
            m_view->tr("Reset crop"), item, p.beforeSrc,
            item->sourceImage().copy(), p.beforeSt, afterSnap);
        ++n;
    }

    if (n > 0 && m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    if (n > 0) {
        emit m_view->statusChanged();
    }
    return n;
}

bool CropController::previewCropRecipeOnItem(ImageItem *item, const CropPanelRecipe &recipe)
{
    if (!m_view || !item) {
        return false;
    }
    const QString path = item->path();
    QImage sample;
    if (recipe.mode == CropPanelRecipe::Mode::Autocrop) {
        sample = sampleFromItem(item);
        if (sample.isNull() && !path.isEmpty()) {
            // Cache-only; do not block the slider path on disk I/O.
            sample = ImageCache::get(path);
            if (!sample.isNull()) {
                sample = sample.copy();
            }
        }
        if (sample.isNull()) {
            return false;
        }
    }
    const QSize logical = logicalSizeForCrop(m_view, item, sample);
    if (logical.width() < 1 || logical.height() < 1) {
        return false;
    }
    const QRect crop = CropRecipeUtil::computeCropRect(recipe, logical, sample);
    if (!CropRecipeUtil::isUsableCrop(crop, logical)) {
        // Identity / full frame: show uncropped soft for feedback.
        WorkspaceItemState st = m_view->freezeItemAppearance(item);
        st.hasCrop = false;
        st.cropRect = QRect();
        st.cropSourceSize = QSize();
        st.cropRotation = 0.0;
        m_view->hostDisplayPipeline().rematerializeItemContent(item, st);
        return false;
    }

    WorkspaceItemState st = m_view->freezeItemAppearance(item);
    ItemComponents::applyPlacementToState(st, item->placement());
    st.hasCrop = true;
    st.cropRect = crop;
    st.cropSourceSize = logical;
    st.cropRotation = 0.0;
    // Display only — do not storeCropAppearance / ItemWorld until Apply.
    m_view->hostDisplayPipeline().rematerializeItemContent(item, st);
    return true;
}
