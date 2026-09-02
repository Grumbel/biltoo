// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    if (image.isNull()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, image);
    applyItemModeFlags(item);
    // Session crop survives navigation: apply only on full on-disk decodes.
    // Workspace Duplicate passes already-final pixels (possibly cropped) — do
    // not re-apply the path crop or the rect is interpreted on the wrong size.
    if (applyStoredSessionCrop) {
        // Prefer stable session-image id appearance; path map is legacy only.
        //
        // Image mode LoadReplace: the sole canvas item is the current session
        // image, so m_currentSessionId / m_sessionIndex identify it correctly.
        //
        // Gallery / Workspace LoadAdd: each tile is bound to its own session id
        // *after* creation (pendingSessionBinds). Using m_currentSessionId here
        // would bake the *navigated* image's crop into every newly decoded tile
        // when leaving Image crop for Gallery — do not apply cursor appearance
        // in multi-item modes.
        const WorkspaceItemState *app = nullptr;
        WorkspaceItemState pathFallback;
        if (isImageMode()) {
            if (m_currentSessionId != kInvalidSessionImageId) {
                if (const WorkspaceItemState *sit = m_appearance.get(m_currentSessionId)) {
                    app = &(*sit);
                }
                // Bound session image with no appearance entry = full frame, no path fallback.
            } else {
                // Path map only when unbound (no session image id).
                const auto it = m_itemStates.constFind(path);
                if (it != m_itemStates.cend()) {
                    pathFallback = *it;
                    app = &pathFallback;
                }
            }
        }
        if (app) {
            applySessionCrop(item, *app);
            applyContentBakes(item, *app);
            item->setSessionCrop(app->hasCrop, app->cropRect);
        }
    }
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

ImageItem *ImageView::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    auto *item = new ImageItem(path, intrinsicSize);
    applyItemModeFlags(item);
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

void ImageView::scheduleImageLoad(const QString &path, LoadRole role)
{
    if (path.isEmpty()) {
        return;
    }
    if (role == LoadAdd) {
        addPendingWorkspacePath(path);
    }
    // LoadRestore pending is owned by m_pendingRestoreStates (AUDIT M27).
    // AUDIT H3a: only LoadReplace advances the generation token so workspace
    // adds cannot cancel an in-flight Image-mode navigation decode.
    // (Cannot use ?: on atomic — pre-increment yields T, bare atomic is not T.)
    quint64 gen = m_loadGeneration.load();
    if (role == LoadReplace) {
        gen = ++m_loadGeneration;
    }
    emit statusChanged(); // pending count for status bar
    // QPointer: worker must not invokeMethod on a destroyed view (secondary
    // windows with WA_DeleteOnClose, app exit). Generation only filters
    // superseding once the slot runs on the GUI thread.
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, role, gen]() {
        const QImage image = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                                  Q_ARG(QString, path),
                                  Q_ARG(QImage, image),
                                  Q_ARG(quint64, gen),
                                  Q_ARG(int, static_cast<int>(role)));
    });
}

void ImageView::scheduleGalleryDecode(const QString &path)
{
    if (path.isEmpty() || m_galleryDecodeFailed.contains(path)
        || m_galleryDecodeScheduled.contains(path)) {
        return;
    }
    // Decode if any live occurrence still lacks pixels (duplicates share one decode).
    bool needsPixels = false;
    for (ImageItem *item : m_items) {
        if (item && item->path() == path && !item->hasDecodedPixels()) {
            needsPixels = true;
            break;
        }
    }
    if (!needsPixels) {
        return;
    }
    if (m_galleryDecodeScheduled.size() >= kMaxConcurrentGalleryDecodes) {
        return; // caller (updateGalleryDecodeWindow) will retry after a slot frees
    }
    m_galleryDecodeScheduled.insert(path);
    addPendingWorkspacePath(path);
    emit statusChanged();
    const quint64 gen = m_loadGeneration.load();
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, gen]() {
        const QImage image = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                                  Q_ARG(QString, path),
                                  Q_ARG(QImage, image),
                                  Q_ARG(quint64, gen),
                                  Q_ARG(int, static_cast<int>(LoadAdd)));
    });
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Replace loads only care about the latest request
    if (role == LoadReplace) {
        if (generation != m_loadGeneration) {
            return; // superseded by a newer navigation / open
        }
        if (path != classicPath() || isMultiItemMode()) {
            // Stale image-mode navigation or switched to workspace
            if (!(isMultiItemMode() && m_items.isEmpty() && path == classicPath())) {
                if (path != classicPath()) {
                    return;
                }
            }
        }
        if (image.isNull()) {
            m_lastLoadError = path;
            emit statusChanged();
            return;
        }
        if (isImageMode()) {
            m_lastLoadError.clear();
            // Suppress paints between removing the old item and fitting the new one
            // so we never present a native-scale (or empty) intermediate frame.
            setUpdatesEnabled(false);
            // Keep stashed Workspace/Gallery tiles — only replace the Image-mode item.
            clearLiveCanvas();
            ImageItem *item = createItemFromImage(path, image);
            if (!item) {
                setUpdatesEnabled(true);
                m_lastLoadError = path;
                emit statusChanged();
                return;
            }
            // Bind to the session cursor so Image-mode crop/flip targets the
            // matching Workspace slot (not every canvas instance of this path).
            if (m_currentSessionId != kInvalidSessionImageId) {
                item->setSessionId(m_currentSessionId);
            }
            if (m_sessionIndex >= 0) {
                item->setSessionIndex(m_sessionIndex);
            }
            // Never inherit Gallery/Workspace placement or scale.
            // DOMAIN: flips/crop and *cardinal* rotation persist across navigation.
            // Arbitrary Workspace rotation stays on the free-form item only.
            // Crop was applied in createItemFromImage from m_itemStates.
            item->setInteractive(false);
            item->setScaleHandlesEnabled(false);
            item->setItemScale(1.0);
            item->setPos(0, 0);
            {
                // Image mode: no Workspace placement rotation. Content 90°/flip
                // are already in pixels (createItemFromImage applies content bakes).
                item->setItemRotation(0.0);
                const auto it = m_itemStates.constFind(path);
                if (it != m_itemStates.cend()) {
                    // Legacy unbaked flips only if content flags not used yet.
                    if (!it->contentHFlip && !it->contentVFlip) {
                        item->setItemHFlip(it->hFlip);
                        item->setItemVFlip(it->vFlip);
                    }
                }
            }
            prepareImageModeCanvas();
            fitItem(item, currentFitAspectMode());
            m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
            setUpdatesEnabled(true);
            viewport()->update();
            emit statusChanged();
            return;
        }
        // Workspace with empty canvas: seed with navigated image
        if (m_items.isEmpty()) {
            ImageItem *item = createItemFromImage(path, image);
            if (item) {
                item->setSelected(true);
                m_fitMode = true;
                fitItem(item, currentFitAspectMode());
                emit statusChanged();
            }
        }
        return;
    }

    // Workspace add / restore
    if (role == LoadRestore) {
        // AUDIT M27: claim one pending restore state for this path (duplicates OK).
        int claim = -1;
        for (int i = 0; i < m_pendingRestoreStates.size(); ++i) {
            if (m_pendingRestoreStates.at(i).path == path) {
                claim = i;
                break;
            }
        }
        if (claim < 0) {
            return;
        }
        const WorkspaceItemState state = m_pendingRestoreStates.takeAt(claim);
        if (image.isNull()) {
            return;
        }
        // Do not apply path-keyed crop — restore uses this slot's own state
        // (Workspace duplicates must not inherit another instance's crop).
        ImageItem *item = createItemFromImage(path, image, /*applyStoredSessionCrop=*/false);
        if (!item) {
            return;
        }
        // Prefer live session-image appearance over the leave-mode snapshot when
        // Image-mode edits updated m_appearance while Workspace was stashed.
        WorkspaceItemState app = state;
        if (state.sessionId != kInvalidSessionImageId) {
            item->setSessionId(state.sessionId);
            if (const WorkspaceItemState *it = m_appearance.get(state.sessionId)) {
                app = *it;
                // Keep placement from the snapshot.
                app.pos = state.pos;
                app.scale = state.scale;
                app.scaleY = state.scaleY;
                app.rotation = state.rotation;
                app.opacity = state.opacity;
                app.z = state.z;
            }
        }
        if (state.sessionIndex >= 0) {
            item->setSessionIndex(state.sessionIndex);
        }
        if (app.hasCrop) {
            applySessionCrop(item, app);
        }
        applyContentBakes(item, app);
        item->setSessionCrop(app.hasCrop, app.cropRect);
        applyState(item, app);
        if (m_layoutMode != LayoutMode::FreeForm
            && !(isGalleryMode() && m_galleryRelayoutSuppressCount > 0)) {
            applyLayout(GalleryPackReason::SessionMutate);
        }
        emit statusChanged();
        emit workspacePathsChanged();
        return;
    }

    // LoadAdd: workspace new item, or Gallery placeholder fill / virtual window.
    // Duplicate paths are separate session images: fill every undecoded live
    // occurrence, then create until live count matches pathOrder occurrences.
    m_galleryDecodeScheduled.remove(path);
    if (!m_pendingWorkspacePaths.contains(path)) {
        // Cancelled (e.g. path removed from session) — drop the result.
        emit statusChanged();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
        return;
    }
    takePendingWorkspacePath(path);

    if (image.isNull()) {
        m_galleryDecodeFailed.insert(path);
        emit statusChanged();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
        return;
    }

    if (isImageMode()) {
        // Fill stashed Gallery placeholders while user is in Image mode.
        for (ImageItem *cand : m_gallery.stashedItems()) {
            if (cand && cand->path() == path && !cand->hasDecodedPixels()) {
                cand->setSourceImage(image);
                applyStoredAppearance(cand);
            }
        }
        emit statusChanged();
        return;
    }

    int wanted = 0;
    for (const QString &p : m_pathOrder) {
        if (p == path) {
            ++wanted;
        }
    }
    // Ad-hoc add (not yet reflected in pathOrder): at least one more than current.
    if (wanted <= 0) {
        for (ImageItem *it : m_items) {
            if (it && it->path() == path) {
                ++wanted;
            }
        }
        ++wanted;
    }

    bool sizeChanged = false;
    int have = 0;
    for (ImageItem *existing : m_items) {
        if (!existing || existing->path() != path) {
            continue;
        }
        ++have;
        if (!existing->hasDecodedPixels()) {
            const QSize before = existing->imageSize();
            existing->setSourceImage(image);
            applyStoredAppearance(existing);
            const QSize after = existing->imageSize();
            if (before.isValid() && after.isValid()
                && (before.width() != after.width() || before.height() != after.height())) {
                sizeChanged = true;
            } else {
                existing->update();
            }
        }
    }
    for (ImageItem *cand : m_gallery.stashedItems()) {
        if (cand && cand->path() == path && !cand->hasDecodedPixels()) {
            cand->setSourceImage(image);
            applyStoredAppearance(cand);
        }
    }

    // Create missing occurrences (each duplicate is a normal separate tile).
    while (have < wanted) {

        ImageItem *item = createItemFromImage(path, image);
        if (!item) {
            break;
        }
        ++have;
        // Bind pending session row if any remain for this path (FIFO).
        PendingSessionBind bound;
        bool haveBound = false;
        for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
            if (m_pendingSessionBinds.at(bi).path == path) {
                bound = m_pendingSessionBinds.takeAt(bi);
                haveBound = true;
                if (bound.id != kInvalidSessionImageId) {
                    item->setSessionId(bound.id);
                }
                if (bound.index >= 0) {
                    item->setSessionIndex(bound.index);
                }
                break;
            }
        }
        applyStoredAppearance(item);
        if (isGalleryMode()) {
            item->setItemRotation(0.0);
            item->setItemHFlip(false);
            item->setItemVFlip(false);
            item->setItemOpacity(1.0);
        } else if (haveBound && bound.hasScenePos) {
            item->setPos(bound.scenePos);
            item->setItemScale(1.0);
            item->setItemRotation(0.0);
            item->setItemOpacity(1.0);
            item->setStackZ(m_items.size() - 1);
            // Drop one legacy path-keyed pos if present so the hash does not grow.
            m_pendingScenePos.remove(path);
        } else if (m_pendingScenePos.contains(path)) {
            const QPointF pos = m_pendingScenePos.take(path);
            item->setPos(pos);
            item->setItemScale(1.0);
            item->setItemRotation(0.0);
            item->setItemOpacity(1.0);
            item->setStackZ(m_items.size() - 1);
        } else {
            const auto it = m_itemStates.constFind(path);
            if (it != m_itemStates.cend()) {
                applyState(item, *it);
            } else {
                WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
                const QSizeF sz(image.width(), image.height());
                s.pos = findEmptyPlacement(sz);
                applyState(item, s);
            }
        }
    }

    if (m_layoutMode != LayoutMode::FreeForm) {
        if (!m_pathOrder.isEmpty()) {
            reorderItemsByPaths(m_pathOrder);
        }
        if (!(isGalleryMode() && m_galleryRelayoutSuppressCount > 0)) {
            if (sizeChanged) {
                applyLayout(GalleryPackReason::ContentChange);
            } else {
                applyLayout(GalleryPackReason::SessionMutate);
            }
        }
    } else {
        updateWorkspaceSceneRect();
    }
    emit statusChanged();
    emit workspacePathsChanged();
    if (isGalleryMode()) {
        updateGalleryDecodeWindow();
    }
}


bool ImageView::loadImage(const QString &path)
{
    setClassicPath(path);
    m_lastLoadError.clear();

    if (isMultiItemMode()) {
        // Session navigation while in multi-item mode does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_items.isEmpty()) {
            scheduleImageLoad(path, LoadReplace);
        }
        emit statusChanged();
        return true;
    }

    // Classic mode: decode off the GUI thread. Keep the previous image and its
    // fit transform until the new decode arrives — resetting the transform here
    // would show the old pixmap at 1:1 for a frame (glitchy rapid navigation).
    scheduleImageLoad(path, LoadReplace);
    emit statusChanged();
    return true;
}
