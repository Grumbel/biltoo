// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include <QDebug>
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "archivepath.h"
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
            SessionAppearance::applyContentToItem(item, *app);
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

void ImageView::installImageModePendingTile(const QString &path, const QImage &preview)
{
    if (!isImageMode() || path.isEmpty()) {
        return;
    }
    // Slideshow owns the viewport with dwell/live blits. Pending tile used to
    // clearLiveCanvas + cancelSlideshowMotion after fade-end cleared the hold,
    // wiping the dwell we just armed (logs: underlayVisible=true item="-",
    // dwellT never restarted). Underlay is hidden for the whole show.
    if (m_slideshowProgressActive
        || m_liveTransitionActive || m_liveTransitionHold || m_liveTransitionAwaitingLoad) {
        return;
    }
    // Prefer an explicit preview argument; else a session-cached thumbnail for
    // this path (revisit during rapid next/prev).
    QImage pixels = preview;
    if (pixels.isNull()) {
        const auto it = m_previewByPath.constFind(path);
        if (it != m_previewByPath.cend()) {
            pixels = it.value();
        }
    }
    // Layout size = native when known; else preview aspect so fitInView fills
    // the window (not a provisional square that letterboxes the content).
    const QSize sz = layoutSizeForPath(path, pixels);

    setUpdatesEnabled(false);
    // Wipe the underlay under an active dwell camera would leave motion pointing
    // at a destroyed item — drop motion first; full load restarts it.
    if (m_slideshowProgressActive) {
        // User next/prev (no live hold): drop transition leftovers and per-image
        // motion biases so the new dwell / next auto-transition starts clean.
        // Mid live-advance keeps composite state; only cancel underlay motion.
        if (!(m_liveTransitionActive || m_liveTransitionHold
              || m_liveTransitionAwaitingLoad)) {
            cancelSlideshowTransition();
            m_motionBiasValid = false;
            m_motionBiasPath.clear();
        }
        cancelSlideshowMotion();
    }
    clearLiveCanvas();
    ImageItem *item = createPlaceholderItem(path, sz);
    if (!item) {
        setUpdatesEnabled(true);
        return;
    }
    if (!pixels.isNull()) {
        item->setPreviewImage(pixels);
    }
    if (m_currentSessionId != kInvalidSessionImageId) {
        item->setSessionId(m_currentSessionId);
    }
    if (m_sessionIndex >= 0) {
        item->setSessionIndex(m_sessionIndex);
    }
    item->setInteractive(false);
    item->setScaleHandlesEnabled(false);
    item->setItemScale(1.0);
    item->setPos(0, 0);
    item->setItemRotation(0.0);
    prepareImageModeCanvas();
    if (m_slideshowProgressActive) {
        // Stay on slideshow zoom (Fit/Fill/Actual), not normal Image fit.
        applySlideshowZoomFraming(item);
    } else {
        fitItem(item, currentFitAspectMode());
    }
    m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    setUpdatesEnabled(true);
    if (viewport()) {
        viewport()->update();
    }
    // Avoid statusChanged here during slideshow — updateNavigationActions must
    // not run mid-nav (and must not treat a transient empty canvas as end-of-show).
    if (!m_slideshowProgressActive) {
        emit statusChanged();
    }
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

    // Slideshow dual-blit already decoded this path (handoff) or preload did —
    // reuse pixels so goNext does not pay a second disk decode under the hold.
    if (role == LoadReplace) {
        QImage ready;
        if (path == m_handoffPath && !m_handoffImage.isNull()) {
            ready = m_handoffImage;
            m_handoffPath.clear();
            m_handoffImage = QImage();
        } else if (path == m_preloadPath && !m_preloadImage.isNull()) {
            ready = m_preloadImage;
            m_preloadPath.clear();
            m_preloadImage = QImage();
        }
        if (!ready.isNull()) {
            const QPointer<ImageView> guard(this);
            QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                                      Q_ARG(QString, path),
                                      Q_ARG(QImage, ready),
                                      Q_ARG(quint64, gen),
                                      Q_ARG(int, static_cast<int>(role)));
            return;
        }
    }

    // Image mode: drop the previous frame immediately so rapid next/prev is not
    // stuck on the old image until the background decode finishes. Reuses a
    // cached preview for this path when available.
    if (role == LoadReplace && isImageMode()) {
        installImageModePendingTile(path);
    }

    // QPointer: worker must not touch a destroyed view. Generation filters
    // superseding once the slot runs on the GUI thread.
    const QPointer<ImageView> guard(this);
    constexpr int kPreviewEdge = 512;

    // Thumbnail and full decode in parallel. Sequential (thumb then full) meant
    // Image mode stayed on the loading placeholder until *both* finished when
    // loadThumbnail was slow or as costly as a full decode.
    QThreadPool::globalInstance()->start([guard, path, role, gen]() {
        const QImage preview = ImageLoader::loadThumbnail(path, kPreviewEdge);
        if (!guard || preview.isNull()) {
            return;
        }
        ImageView *view = guard.data();
        if (!view) {
            return;
        }
        // Functor invoke avoids string/slot lookup and Q_ARG metatype issues.
        QMetaObject::invokeMethod(view, [guard, path, preview, gen, role]() {
            if (!guard) {
                return;
            }
            guard->onImagePreviewLoaded(path, preview, gen, static_cast<int>(role));
        }, Qt::QueuedConnection);
    }, 2);

    QThreadPool::globalInstance()->start([guard, path, role, gen]() {
        const QImage image = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        ImageView *view = guard.data();
        if (!view) {
            return;
        }
        QMetaObject::invokeMethod(view, [guard, path, image, gen, role]() {
            if (!guard) {
                return;
            }
            guard->onImageLoaded(path, image, gen, static_cast<int>(role));
        }, Qt::QueuedConnection);
    }, 1);
}

void ImageView::scheduleGalleryDecode(const QString &path)
{
    if (!isGalleryMode()) {
        return;
    }
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
        constexpr int kPreviewEdge = 384;
        const QImage preview = ImageLoader::loadThumbnail(path, kPreviewEdge);
        if (guard && !preview.isNull()) {
            ImageView *view = guard.data();
            if (view) {
                QMetaObject::invokeMethod(view, [guard, path, preview, gen]() {
                    if (!guard) {
                        return;
                    }
                    guard->onImagePreviewLoaded(path, preview, gen,
                                                static_cast<int>(LoadAdd));
                }, Qt::QueuedConnection);
            }
        }
        const QImage image = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        ImageView *view = guard.data();
        if (!view) {
            return;
        }
        QMetaObject::invokeMethod(view, [guard, path, image, gen]() {
            if (!guard) {
                return;
            }
            guard->onImageLoaded(path, image, gen, static_cast<int>(LoadAdd));
        }, Qt::QueuedConnection);
    });
}

void ImageView::onImagePreviewLoaded(const QString &path, const QImage &image, quint64 generation,
                                     int role)
{
    if (image.isNull()) {
        return;
    }
    ImageCache::put(path, image);
    // Session cache for rapid revisit (path is the decode source; size is small).
    m_previewByPath.insert(path, image);
    // Replace navigations: drop superseded previews.
    if (role == LoadReplace && generation != m_loadGeneration.load()) {
        return;
    }
    if (role == LoadReplace) {
        if (path != classicPath()) {
            return;
        }
        if (isImageMode()) {
            if (ImageItem *cur = targetItem()) {
                if (cur->path() == path && cur->hasDecodedPixels()) {
                    return; // full decode already won the race
                }
                // Upgrade loading placeholder (same path) in place when possible.
                if (cur->path() == path && !cur->hasDecodedPixels()) {
                    // If layout size was still provisional, adopt preview aspect
                    // and re-fit so the frame fills the window immediately.
                    if (isProvisionalImageSize(path)
                        && image.width() > 0 && image.height() > 0) {
                        cur->setIntrinsicSize(image.size());
                        cur->setPreviewImage(image);
                        if (!m_slideshowProgressActive) {
                            fitItem(cur, currentFitAspectMode());
                        } else {
                            applySlideshowZoomFraming(cur);
                        }
                        if (m_scene) {
                            m_scene->setSceneRect(
                                cur->sceneBoundingRect().adjusted(-8, -8, 8, 8));
                        }
                    } else {
                        // Pixels only — intrinsic size stays native (probe/cache).
                        cur->setPreviewImage(image);
                    }
                    if (viewport()) {
                        viewport()->update();
                    }
                    return;
                }
            }
            installImageModePendingTile(path, image);
            return;
        }
        // Empty multi-item canvas: fall through to per-item fill.
    }
    // Gallery / Workspace: fill undecoded occurrences of this path.
    for (ImageItem *item : m_items) {
        if (item && item->path() == path && !item->hasDecodedPixels()) {
            item->setPreviewImage(image);
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Feed shared cache (preview-sized) so slideshow/gallery reuse this decode.
    if (!image.isNull() && !path.isEmpty()) {
        const int edge = ImageCache::kPreviewEdge;
        if (qMax(image.width(), image.height()) > edge) {
            ImageCache::put(path, image.scaled(edge, edge, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
        } else {
            ImageCache::put(path, image);
        }
    }
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
            rememberImageSize(path, image.size());
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
            // Slideshow framing: when dwell motion is on, the camera sets the
            // transform (including handoff from a live transition). Applying
            // zoom framing first would centre the image then jump to motion t0.
            if (m_slideshowProgressActive
                && m_slideshowMotion == SlideshowMotion::Off) {
                applySlideshowZoomFraming(item);
            } else if (!m_slideshowProgressActive) {
                fitItem(item, currentFitAspectMode());
            }
            m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
            // Apply camera while updates are still blocked and any live hold still
            // covers the viewport — avoids a flash of identity / wrong pan pose.
            maybeStartSlideshowMotion();
            if (m_slideshowProgressActive && m_slideshowMotion != SlideshowMotion::Off
                && !m_slideshowMotionActive) {
                applySlideshowZoomFraming(item);
            }
            if (m_slideshowProgressActive) {
                item->setVisible(false);
                // Paused ←/→ loads the underlay while pure phase still paints
                // the previous path — refresh dwell to this decode.
                setSlideshowPhase(path, QString(), -1.0);
            }
            // Drop held live-transition overlay only after the new item is fitted
            // (and motion sample applied) so the outgoing underlay never flashes.
            releaseLiveTransitionHold();
            setUpdatesEnabled(true);
            if (m_slideshowTransitionPending) {
                startSlideshowTransitionAnimation();
            } else {
                viewport()->update();
            }
            emit statusChanged();
            return;
        }
        // Workspace with empty canvas: seed with navigated image — only for
        // genuine session navigation. Project load / membership adds schedule
        // LoadAdd with pending binds; seeding first would leave an unbound tile
        // (default placement, no flip/grade) and steal the first path's LoadAdd.
        if (m_items.isEmpty()
            && m_pendingSessionBinds.isEmpty()
            && !m_pendingWorkspacePaths.contains(path)) {
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
        SessionAppearance::applyContentToItem(item, app);
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
    // Mode leave / empty Workspace bumps generation and clears pending paths.
    // Reject superseded gallery window decodes so they cannot spawn tiles on
    // Workspace after the user switched modes mid-decode.
    if (generation != m_loadGeneration.load()) {
        emit statusChanged();
        return;
    }
    if (!image.isNull()) {
        rememberImageSize(path, image.size());
    }
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
        qWarning("ImageView: decode failed for %s", qPrintable(path));
        m_galleryDecodeFailed.insert(path);
        m_lastLoadError = path;
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

    int pathOrderCount = 0;
    for (const QString &p : m_pathOrder) {
        if (p == path) {
            ++pathOrderCount;
        }
    }

    // Pending binds whose SessionImageId is already on a live tile are satisfied
    // (placeholders / prior LoadAdd). Drop them so we do not create extras.
    for (int bi = m_pendingSessionBinds.size() - 1; bi >= 0; --bi) {
        const PendingSessionBind &b = m_pendingSessionBinds.at(bi);
        if (b.path != path || b.id == kInvalidSessionImageId) {
            continue;
        }
        if (findItemBySessionId(b.id)) {
            m_pendingSessionBinds.removeAt(bi);
        }
    }

    // Claim existing *unbound* tiles of this path for pending session binds
    // (e.g. empty-Workspace LoadReplace seeded the first path before LoadAdd).
    // Without this, have==wanted and the bind is never applied — placement,
    // content flips, and colour grade stay at defaults on that tile.
    for (ImageItem *existing : m_items) {
        if (!existing || existing->path() != path) {
            continue;
        }
        if (existing->sessionId() != kInvalidSessionImageId) {
            continue;
        }
        PendingSessionBind bound;
        bool haveBound = false;
        for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
            if (m_pendingSessionBinds.at(bi).path != path) {
                continue;
            }
            bound = m_pendingSessionBinds.takeAt(bi);
            haveBound = true;
            break;
        }
        if (!haveBound) {
            break;
        }
        if (bound.id != kInvalidSessionImageId) {
            existing->setSessionId(bound.id);
        }
        if (bound.index >= 0 && bound.id != kInvalidSessionImageId) {
            existing->setSessionIndex(bound.index);
        }
        // Full source pixels so applyStoredAppearance can crop/bake safely
        // (seed tiles may already have decoded defaults without content ops).
        existing->setSourceImage(image);
        applyStoredAppearance(existing);
        if (bound.id != kInvalidSessionImageId && m_appearance.get(bound.id)) {
            applyState(existing, *m_appearance.get(bound.id));
        }
        if (bound.id != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(existing);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(bound.id, existing->path(), appearance);
            }
            if (m_pendingSelectSessionIds.remove(bound.id)) {
                existing->setSelected(true);
            }
        }
    }

    int pendingBinds = 0;
    for (const PendingSessionBind &b : m_pendingSessionBinds) {
        if (b.path == path) {
            ++pendingBinds;
        }
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

    // Session pathOrder is the multiplicity source of truth. Do not create more
    // tiles than session rows for this path (pending binds only fill gaps).
    int wanted = pathOrderCount;
    if (wanted <= 0) {
        // Not in session pathOrder (ad-hoc workspace place): one tile per bind.
        wanted = qMax(have, pendingBinds > 0 ? pendingBinds : 1);
    }

    // Create missing occurrences (each duplicate is a normal separate tile).
    while (have < wanted) {

        ImageItem *item = createItemFromImage(path, image);
        if (!item) {
            break;
        }
        ++have;
        // Bind pending session row if any remain for this path (FIFO).
        // Skip binds already owned by another live item (should be rare after purge).
        PendingSessionBind bound;
        bool haveBound = false;
        for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
            if (m_pendingSessionBinds.at(bi).path != path) {
                continue;
            }
            const PendingSessionBind candidate = m_pendingSessionBinds.at(bi);
            if (candidate.id != kInvalidSessionImageId) {
                if (ImageItem *owner = findItemBySessionId(candidate.id)) {
                    if (owner != item) {
                        m_pendingSessionBinds.removeAt(bi);
                        --bi;
                        continue;
                    }
                }
            }
            bound = m_pendingSessionBinds.takeAt(bi);
            haveBound = true;
            if (bound.id != kInvalidSessionImageId) {
                item->setSessionId(bound.id);
            }
            if (bound.index >= 0 && bound.id != kInvalidSessionImageId) {
                item->setSessionIndex(bound.index);
            }
            break;
        }
        applyStoredAppearance(item);
        // Keep filmstrip overrides in sync (crop / grade) after drop or add.
        if (haveBound && bound.id != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(bound.id, item->path(), appearance);
            }
            // Paste: select tiles as they finish decoding.
            if (m_pendingSelectSessionIds.remove(bound.id)) {
                item->setSelected(true);
            }
        }
        if (isGalleryMode()) {
            item->setItemRotation(0.0);
            item->setItemShear(0.0);
            item->setItemHFlip(false);
            item->setItemVFlip(false);
            item->setItemOpacity(1.0);
        } else if (haveBound && bound.hasScenePos) {
            // Explicit drop: place at the drop point (new placement).
            item->setPos(bound.scenePos);
            item->setItemScale(1.0);
            item->setItemRotation(0.0);
            item->setItemOpacity(1.0);
            item->setStackZ(m_items.size() - 1);
            m_pendingScenePos.remove(path);
        } else if (haveBound && bound.id != kInvalidSessionImageId
                   && m_appearance.get(bound.id)) {
            // Thumbnail membership toggle: restore last Workspace pose for this
            // session image (detach saved it via rememberItemState).
            applyState(item, *m_appearance.get(bound.id));
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

    // Classic mode: decode off the GUI thread. scheduleImageLoad installs a
    // loading tile immediately, then upgrades to thumbnail and full decode.
    scheduleImageLoad(path, LoadReplace);
    emit statusChanged();
    return true;
}
