// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <cstdlib>
#include "archivepath.h"
#include "pagepath.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "sessionappearance.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPaintEvent>
#include <QOpenGLWidget>
#include <QScrollBar>
#include <QRubberBand>
#include <QTimer>
#include <QSet>
#include <QThreadPool>
#include <QPointer>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWheelEvent>
#include <QtMath>
#include <atomic>
#include <cmath>
#include <algorithm>


ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
    , m_gallery(this)
    , m_workspace(this)
    , m_image(this)
{
    m_scene = new QGraphicsScene(this);
    // BSP indexing is fragile with frequent add/remove (Duplicate + Delete):
    // deferred paints can walk a tree that still holds freed items. Linear
    // search is fine for Workspace/Gallery counts we care about.
    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(m_scene);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        // Rubber-band / programmatic selects: keep Gallery Shift-range anchor
        // on a still-selected tile when the previous anchor was dropped.
        if (isGalleryMode()) {
            if (m_gallery.selectionAnchor()
                && !m_gallery.selectionAnchor()->isSelected()) {
                ImageItem *next = nullptr;
                for (QGraphicsItem *gi : m_scene->selectedItems()) {
                    if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                        next = ii;
                        break;
                    }
                }
                m_gallery.setSelectionAnchor(next);
            }
            // Gallery tiles use DeviceCoordinateCache; selection chrome is drawn
            // in paint() and stays frozen until the cache is rebuilt.
            for (ImageItem *item : m_items) {
                if (item) {
                    item->invalidateDeviceCache();
                }
            }
            if (viewport()) {
                viewport()->update();
            }
            // Refresh interest snapshot so FocusFull tracks the new Primary.
            updateGalleryDecodeWindow();
        }
        // Workspace: Primary = first selected; Near = remaining selection.
        if (isWorkspaceMode()) {
            QStringList primary;
            QStringList near;
            for (QGraphicsItem *gi : m_scene->selectedItems()) {
                if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                    const QString path = ii->path();
                    if (path.isEmpty()) {
                        continue;
                    }
                    if (primary.isEmpty()) {
                        primary.append(path);
                    } else {
                        near.append(path);
                    }
                }
            }
            if (!primary.isEmpty()) {
                (void)ThumtooCache::setInterest(
                    near, {}, ThumtooCache::kBatchOverviewEdge,
                    ThumtooCache::kGalleryLadderEdge, primary,
                    ThumtooCache::kImageLadderEdge);
                ensureWorkspaceQualityClimb();
            }
        }
        emit statusChanged();
        emit canvasSelectionChanged();
    });
    m_undoStack = new QUndoStack(this);
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<quint64>("quint64");

    // Durable native size from thumtoo (GUI thread via Qt Executor).
    // Invalid/empty size still clears probe scheduling and advances the Gallery
    // size-resolve gate (failed request_size must not stick forever).
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::sizeReady, this,
            [this](const QString &path, const QSize &size) {
                if (path.isEmpty()) {
                    return;
                }
                m_sizeProbeScheduled.remove(path);
                const bool valid = size.isValid() && size.width() > 0 && size.height() > 0;
                if (valid) {
                    // Prefer a size already learned from a full decode.
                    if (m_imageSizeByPath.contains(path) && !isProvisionalImageSize(path)) {
                        // Still try LQIP if tiles are blank (probe may have written LQIP).
                    } else {
                        rememberImageSize(path, size);
                        applyProbedImageSize(path, size);
                    }
                    // LQIP may already be in ImageCache (size probe callback).
                    // Still paint blank tiles; never block later soft upgrades.
                    if (isGalleryMode()) {
                        QImage lqip = ImageCache::get(path);
                        if (lqip.isNull()) {
                            lqip = ThumtooCache::cachedLqipImage(path);
                            if (!lqip.isNull()) {
                                ImageCache::put(path, lqip);
                            }
                        }
                        if (!lqip.isNull()) {
                            for (ImageItem *item : m_items) {
                                if (!item || item->path() != path) {
                                    continue;
                                }
                                if (item->hasDisplayPixels()) {
                                    continue;
                                }
                                installDisplayPixels(item, lqip,
                                                     SessionAppearance::PixelKind::SoftPreview,
                                                     item->sessionId());
                            }
                        }
                    }
                }
                noteGallerySizeProbeSettled(path);
            });

    // Soft preview: install better ladder pixels; clear inflight when matched.
        connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderProvenance, this,
            [this](const QString &path, int, int) {
                if (path.isEmpty()) {
                    return;
                }
                ImageItem *item = targetItem();
                if (!item) {
                    item = primaryItem();
                }
                if (item && item->path() == path) {
                    refreshStatus();
                }
            });
    m_pathRaster = new PathRasterService(this);
    connect(m_pathRaster, &PathRasterService::rasterImproved, this,
            [this](const QString &path, int longEdge) {
                if (path.isEmpty()) {
                    return;
                }
                const QImage img = ImageCache::get(path);
                if (img.isNull()) {
                    return;
                }
                if (m_slideshowProgressActive
                    && (path == m_ssFromPath || path == m_ssToPath)) {
                    onSlideshowRasterReady(path, img);
                    // PreferCache often lands at 1024 first; keep climbing to
                    // viewport target (2048+) until adequate or terminal.
                    if (m_pathRaster) {
                        const int target = cappedDisplayEdgeForPath(
                            path, slideshowTargetEdge());
                        const int need = target * 7 / 10;
                        if (longEdge > 0 && longEdge < need) {
                            m_pathRaster->ensure(
                                path, target, logicalSizeForPath(path),
                                PathRasterService::ClimbPolicy::EscalateToFull);
                        }
                    }
                    return;
                }
                if (isImageMode() && !m_slideshowProgressActive
                    && path == classicPath()) {
                    (void)tryInstallImageModeSample(path, img);
                    // PreferCache plateaued below viewport need → quiet native.
                    // Full escalate is PathRasterService ClimbPolicy::EscalateToFull.
                    emit statusChanged();
                }
                // Gallery: soft may land in ImageCache via noteDelivery while the
                // tile still shows LQIP — mirror ladderReady install.
                if (isGalleryMode()) {
                    onImagePreviewLoaded(path, img, m_loadGeneration.load(),
                                         static_cast<int>(LoadAdd));
                }
                if (isWorkspaceMode()) {
                    onImagePreviewLoaded(path, img, m_loadGeneration.load(),
                                         static_cast<int>(LoadAdd));
                }
            });
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderReady, this,
            &ImageView::onLadderReady);

    connect(this, &ImageView::statusChanged, this, [this]() {
        if (m_hudVisible || m_hudFlashVisible || m_slideshowPausedHud) {
            viewport()->update();
        }
    });

        {
        const char *p = std::getenv("BILTOO_PERF");
        const char *d = std::getenv("THUMTOO_DEBUG");
        m_perfEnabled = (p && p[0] && p[0] != '0')
            || (d && d[0] && d[0] != '0');
        if (m_perfEnabled) {
            m_perfFpsClock.start();
        }
    }
    m_hudFlashTimer = new QTimer(this);
    m_hudFlashTimer->setSingleShot(true);
    m_layoutDebounceTimer = new QTimer(this);
    m_layoutDebounceTimer->setSingleShot(true);
    m_layoutDebounceTimer->setInterval(48);
    connect(m_layoutDebounceTimer, &QTimer::timeout, this, [this]() {
        if (isGalleryMode() && m_layoutMode != LayoutMode::FreeForm) {
            applyLayout(m_debouncedPackReason);
        }
    });
    connect(m_hudFlashTimer, &QTimer::timeout, this, [this]() {
        m_hudFlashVisible = false;
        m_hudIdentityPulse = false;
        m_hudAction.clear();
        m_hudDetail.clear();
        viewport()->update();
    });

    m_slideshowProgressTimer = new QTimer(this);
    m_slideshowProgressTimer->setInterval(33); // ~30 Hz; cheap 1px redraw
    connect(m_slideshowProgressTimer, &QTimer::timeout, this, [this]() {
        if (m_hudVisible && m_slideshowProgressActive && m_slideshowProgressIntervalMs > 0) {
            viewport()->update();
        }
    });

    setRenderHint(QPainter::SmoothPixmapTransform, true);
    // OpenGL viewport — overlays must use drawForeground (see paintEvent).
    setViewport(new QOpenGLWidget);
    setAcceptDrops(true);
    if (viewport()) {
        viewport()->setAcceptDrops(true);
    }
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(m_bgColor));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    // QGraphicsView delivers moves via the viewport — both need tracking or
    // MouseMove only arrives while a button is held (breaks slideshow cursor).
    setMouseTracking(true);
    if (viewport()) {
        viewport()->setMouseTracking(true);
    }
    setAlignment(Qt::AlignCenter);
    // Full updates: HUD, edge affordances and workspace chrome are painted in
    // paintEvent on top of the scene. SmartViewportUpdate scrolls/blits the
    // viewport and leaves overlay trails (e.g. Vertical gallery scroll).
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    // Scrolling moves tiles under a stationary cursor — refresh gallery HUD path.
    auto refreshHover = [this]() {
        if (isGalleryMode() && !m_lastHoverViewPos.isNull()) {
            updateGalleryHoverAt(m_lastHoverViewPos);
        }
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        // Debounce: every scroll pixel used to scan all tiles + start pool
        // work and could peg a core while the user was only panning.
        scheduleGalleryDecodeWindowRefresh(150);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        scheduleGalleryDecodeWindowRefresh(150);
    });

    // Recover Gallery tiles that received soft pixels but never repainted
    // (DeviceCoordinateCache + BoundingRectViewportUpdate stalls).
    m_gallerySoftWatchdog = new QTimer(this);
    m_gallerySoftWatchdog->setInterval(1000);
    connect(m_gallerySoftWatchdog, &QTimer::timeout, this, [this]() {
        if (isGalleryMode()) {
            gallerySoftWatchdogTick();
        }
    });
    m_gallerySoftWatchdog->start();
}

ImageView::~ImageView()
{
    // Invalidate any queued onImageLoaded invocations from the thread pool.
    ++m_loadGeneration;

    if (m_hudFlashTimer) {
        m_hudFlashTimer->stop();
    }
    if (m_slideshowProgressTimer) {
        m_slideshowProgressTimer->stop();
    }
    if (m_layoutDebounceTimer) {
        m_layoutDebounceTimer->stop();
    }

    // Scene clear emits selectionChanged; our handler calls viewport()->update().
    // That is unsafe once ~QWidget has started deleting children — tear the
    // scene down here while ImageView is still fully constructed.
    discardStashedGallery();

    if (m_scene) {
        disconnect(m_scene, nullptr, this, nullptr);
        m_scene->blockSignals(true);
        m_scene->clear();
        m_items.clear();
        m_pendingWorkspacePaths.clear();
        gallerySoftResetAll();
        setScene(nullptr);
        delete m_scene;
        m_scene = nullptr;
    }
}



QSize ImageView::probeImageSize(const QString &path) const
{
    // Archive member probes must not extract on the GUI thread (large zip
    // open would freeze Gallery virtualization). Prefer thumtoo cache-only
    // size; otherwise a neutral placeholder until ladder/sizeReady reflows.
    if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
        || PagePath::isPdfImageRef(path)) {
        if (const QSize cached = ThumtooCache::cachedSize(path); cached.isValid()) {
            return cached;
        }
        return QSize(1024, 1024);
    }
    // Header-only when possible (Qt, then VIPS); see ImageLoader::probeSize.
    QSize s = ImageLoader::probeSize(path);
    if (!s.isValid() || s.width() <= 0 || s.height() <= 0) {
        s = QSize(1000, 1000);
    }
    return s;
}

void ImageView::rememberImageSize(const QString &path, const QSize &size)
{
    if (path.isEmpty() || !isPositiveSize(size)) {
        return;
    }
    // HARD RULE: logical size is identity. Soft / ladder sample dimensions must
    // never replace a known larger size (that made 512px "become" the image).
    const auto it = m_imageSizeByPath.constFind(path);
    if (it != m_imageSizeByPath.cend() && isPositiveSize(*it)
        && !isProvisionalImageSize(path)
        && isMuchSmallerArea(size, *it)) {
        return;
    }
    m_imageSizeByPath.insert(path, size);
    m_provisionalSizePaths.remove(path);
}

void ImageView::rememberSizeFromDecode(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    // Durable index is authoritative when present.
    if (const QSize cached = ThumtooCache::cachedSize(path); isPositiveSize(cached)) {
        rememberImageSize(path, cached);
        return;
    }
    // Already have a definitive logical size — leave samples alone.
    if (m_imageSizeByPath.contains(path) && !isProvisionalImageSize(path)) {
        return;
    }
    // Ladder / soft samples are not native identity. Probe for the real size;
    // do not write sample dimensions into the logical map.
    const int edge = qMax(image.width(), image.height());
    if (edge <= ThumtooCache::kImageLadderEdge) {
        scheduleImageSizeProbe(path);
        return;
    }
    // Larger than the image ladder max — treat as full native decode.
    rememberImageSize(path, image.size());
}

bool ImageView::isProvisionalImageSize(const QString &path) const
{
    return !path.isEmpty() && m_provisionalSizePaths.contains(path);
}

QSize ImageView::imageSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return QSize(1000, 1000);
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known)) {
        if (!m_imageSizeByPath.contains(path)) {
            rememberImageSize(path, known); // install thumtoo hit into map
        }
        return known;
    }
    // Archives / multipage / embedded PDF: async probe; neutral stand-in.
    scheduleImageSizeProbe(path);
    m_provisionalSizePaths.insert(path);
    QSize standIn(1000, 1000);
    if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
        || PagePath::isPdfImageRef(path)) {
        // Square is only a last resort until soft aspect or probe arrives.
        standIn = QSize(kProvisionalLayoutLongEdge, kProvisionalLayoutLongEdge);
    }
    m_imageSizeByPath.insert(path, standIn);
    return standIn;
}

QSize ImageView::layoutSizeForPath(const QString &path, const QImage &previewHint)
{
    // Prefer definitive logical size (map / thumtoo) — never soft sample dims.
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !isProvisionalImageSize(path)) {
        return known;
    }
    // Soft / LQIP aspect is better than a square archive stand-in while the
    // durable probe is in flight. Magnitude stays at provisional long-edge.
    if (!previewHint.isNull() && isPositiveSize(previewHint.size())) {
        scheduleImageSizeProbe(path);
        m_provisionalSizePaths.insert(path);
        const QSize scaled =
            scaleToLongEdge(previewHint.size(), kProvisionalLayoutLongEdge);
        if (isPositiveSize(scaled)) {
            // Store provisional aspect so pack/layout see the same size without
            // a soft sample on every call.
            m_imageSizeByPath.insert(path, scaled);
            return scaled;
        }
    }
    const auto it = m_imageSizeByPath.constFind(path);
    if (it != m_imageSizeByPath.cend() && isPositiveSize(*it)) {
        return *it;
    }
    return imageSizeForPath(path);
}


void ImageView::primeGalleryGeometryFromCache(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        // Native size from durable index (no probe I/O).
        if (!m_imageSizeByPath.contains(path) || isProvisionalImageSize(path)) {
            if (const QSize cached = ThumtooCache::cachedSize(path);
                isPositiveSize(cached)) {
                rememberImageSize(path, cached);
            }
        }
        // LQIP as soft stand-in until ladder/full arrives (ImageCache is authority).
        if (!ImageCache::has(path)) {
            const QImage lqip = ThumtooCache::cachedLqipImage(path);
            if (!lqip.isNull()) {
                ImageCache::put(path, lqip);
            }
        }
    }
}

void ImageView::scheduleImageSizeProbe(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_sizeProbeScheduled.contains(path)) {
        return;
    }
    // Definitive size already known — provisional stand-ins must still probe.
    if (m_imageSizeByPath.contains(path) && !isProvisionalImageSize(path)) {
        return;
    }
    // Durable cache will never handle this locator — do not spin probes.
    if (ThumtooCache::isUnsupported(path)) {
        return;
    }
    // Prefer thumtoo: scheduleProbe only; Bridge::sizeReady applies the size.
    // No thread-pool Qt/vips/extract size read when the durable client is up.
    if (ThumtooCache::isAvailable()) {
        m_sizeProbeScheduled.insert(path);
        ThumtooCache::scheduleProbe(path);
        return;
    }
    // Builds without thumtoo: native size probe on a worker.
    m_sizeProbeScheduled.insert(path);
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path]() {
        QSize s = ImageLoader::probeSize(path);
        if (!s.isValid() || s.width() <= 0 || s.height() <= 0) {
            s = QSize(1000, 1000);
        }
        if (!guard) {
            return;
        }
        ImageView *view = guard.data();
        if (!view) {
            return;
        }
        QMetaObject::invokeMethod(view, [guard, path, s]() {
            ImageView *const host = guard.data();
            if (!host) {
                return;
            }
            host->m_sizeProbeScheduled.remove(path);
            // Prefer a size already learned from a full decode.
            if (host->m_imageSizeByPath.contains(path)) {
                return;
            }
            host->rememberImageSize(path, s);
            host->applyProbedImageSize(path, s);
        }, Qt::QueuedConnection);
    });
}

void ImageView::applyProbedImageSize(const QString &path, const QSize &size)
{
    if (path.isEmpty() || !size.isValid()) {
        return;
    }
    bool any = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        // Probe is authoritative logical size. Apply even when soft/full sample
        // is present — samples must not block the true size.
        // Probe reports on-disk orientation. Content quarter-turns may swap
        // layout aspect — do not clobber an oriented cell with the raw size.
        QSize layoutSize = size;
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_currentSessionId;
        }
        if (sid != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = m_appearance.get(sid)) {
                if (SessionAppearance::contentSwapsAspect(*app)) {
                    layoutSize = QSize(size.height(), size.width());
                }
            }
        }
        const QSize cur = item->imageSize();
        if (cur == layoutSize) {
            continue;
        }
        item->setIntrinsicSize(layoutSize);
        any = true;
        if (isImageMode() && item == targetItem()) {
            preserveImageViewOnLogicalSizeChange(item, cur, layoutSize);
        }
    }
    if (any && isGalleryMode() && m_layoutMode != LayoutMode::FreeForm) {
        // While the open-time size-resolve gate is active, pack once when all
        // probes settle — not on every sizeReady (avoids thrash + tiny cells).
        if (!m_gallerySizeResolveActive) {
            requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
        }
    } else if (any && viewport()) {
        viewport()->update();
    }
    // Slideshow paints from path→logical, not the underlay item. When the probe
    // lands for a phase path, refresh dest aspect (and atlas if needed).
    if (m_slideshowProgressActive
        && (path == m_ssFromPath || path == m_ssToPath)) {
        if (path == m_ssFromPath && !m_ssFromImage.isNull()) {
            requestDwellAtlasRebuild();
        }
        if (path == m_ssToPath && !m_ssToImage.isNull()) {
            requestToPhaseAtlasRebuild();
        }
        if (viewport()) {
            viewport()->update();
        }
    }
}

bool ImageView::startGallerySizeResolveIfNeeded(const QStringList &paths)
{
    if (m_gallerySizeResolveTimer) {
        m_gallerySizeResolveTimer->stop();
    }
    m_gallerySizeResolvePending.clear();
    m_gallerySizeResolveTotal = 0;
    // Suppress intermediate packs while seeding definitive sizes from cache.
    m_gallerySizeResolveActive = true;

    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (ThumtooCache::isUnsupported(path)) {
            continue;
        }
        if (m_imageSizeByPath.contains(path) && !isProvisionalImageSize(path)) {
            continue;
        }
        if (const QSize cached = ThumtooCache::cachedSize(path); isPositiveSize(cached)) {
            rememberImageSize(path, cached);
            continue;
        }
        m_gallerySizeResolvePending.insert(path);
    }

    m_gallerySizeResolveTotal = m_gallerySizeResolvePending.size();
    if (m_gallerySizeResolvePending.isEmpty()) {
        m_gallerySizeResolveActive = false;
        return false;
    }

    // Copy keys — schedule must not iterate a set we mutate.
    const QList<QString> need = m_gallerySizeResolvePending.values();
    for (const QString &path : need) {
        scheduleImageSizeProbe(path);
    }
    // Safety: never block Gallery forever if a probe hangs.
    if (!m_gallerySizeResolveTimer) {
        m_gallerySizeResolveTimer = new QTimer(this);
        m_gallerySizeResolveTimer->setSingleShot(true);
        connect(m_gallerySizeResolveTimer, &QTimer::timeout, this, [this]() {
            if (!m_gallerySizeResolveActive) {
                return;
            }
            finishGallerySizeResolve();
        });
    }
    m_gallerySizeResolveTimer->start(45000);
    // Pulse HUD even when sizeReady arrives in one burst (Qt coalesces paints).
    if (!m_gallerySizeResolveProgressTimer) {
        m_gallerySizeResolveProgressTimer = new QTimer(this);
        m_gallerySizeResolveProgressTimer->setInterval(50);
        connect(m_gallerySizeResolveProgressTimer, &QTimer::timeout, this,
                &ImageView::updateGallerySizeResolveProgressHud);
    }
    m_gallerySizeResolveProgressTimer->start();
    updateGallerySizeResolveProgressHud();
    emit statusChanged();
    // Let the centre HUD paint before sizeReady callbacks can finish the gate
    // in one burst (multi-file open used to look like an instant Fit with no
    // progress).
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return true;
}

void ImageView::updateGallerySizeResolveProgressHud()
{
    if (!m_gallerySizeResolveActive || m_gallerySizeResolveTotal <= 0) {
        return;
    }
    const int done = qMax(0, m_gallerySizeResolveTotal
                          - m_gallerySizeResolvePending.size());
    setCentreProgress(tr("Resolving sizes…"),
                      tr("%1 / %2").arg(done).arg(m_gallerySizeResolveTotal));
}

void ImageView::noteGallerySizeProbeSettled(const QString &path)
{
    if (!m_gallerySizeResolveActive) {
        return;
    }
    if (!path.isEmpty()) {
        m_gallerySizeResolvePending.remove(path);
    }
    if (!m_gallerySizeResolvePending.isEmpty()) {
        updateGallerySizeResolveProgressHud();
        return;
    }
    finishGallerySizeResolve();
}

void ImageView::finishGallerySizeResolve()
{
    if (m_gallerySizeResolveTimer) {
        m_gallerySizeResolveTimer->stop();
    }
    const bool wasActive = m_gallerySizeResolveActive;
    m_gallerySizeResolveActive = false;
    m_gallerySizeResolvePending.clear();
    m_gallerySizeResolveTotal = 0;
    if (!wasActive) {
        return;
    }
    if (m_gallerySizeResolveProgressTimer) {
        m_gallerySizeResolveProgressTimer->stop();
    }
    clearCentreProgress();
    if (isGalleryMode()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    // Create tiles only now — sizes are definitive (or timed out with stand-in).
    if (m_galleryDeferPopulate) {
        m_galleryDeferPopulate = false;
        ensureGalleryPlaceholders();
    } else {
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(true);
                if (!isProvisionalImageSize(item->path())) {
                    const QSize sz = layoutSizeForPath(item->path());
                    if (isPositiveSize(sz)) {
                        item->setIntrinsicSize(sz);
                    }
                }
            }
        }
    }
    if (isGalleryMode() && !m_items.isEmpty() && m_layoutMode != LayoutMode::FreeForm) {
        applyLayout(GalleryPackReason::EnterGallery);
        updateGalleryDecodeWindow();
        QTimer::singleShot(0, this, [this]() {
            if (isGalleryMode() && !m_items.isEmpty()) {
                updateGalleryDecodeWindow();
            }
        });
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::cancelGallerySizeResolve()
{
    if (m_gallerySizeResolveTimer) {
        m_gallerySizeResolveTimer->stop();
    }
    const bool wasActive = m_gallerySizeResolveActive;
    m_gallerySizeResolveActive = false;
    m_gallerySizeResolvePending.clear();
    m_gallerySizeResolveTotal = 0;
    if (wasActive) {
        if (m_gallerySizeResolveProgressTimer) {
            m_gallerySizeResolveProgressTimer->stop();
        }
        m_galleryDeferPopulate = false;
        if (isGalleryMode() && m_centreProgressTitle.isEmpty()) {
            setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        }
        if (m_centreProgressTitle.startsWith(tr("Resolving sizes"))) {
            clearCentreProgress();
        }
    }
}

void ImageView::setCentreProgress(const QString &title, const QString &detail)
{
    if (title.isEmpty()) {
        clearCentreProgress();
        return;
    }
    if (m_centreProgressTitle == title && m_centreProgressDetail == detail) {
        return;
    }
    m_centreProgressTitle = title;
    m_centreProgressDetail = detail;
    // Need full viewport updates so the overlay repaints on an empty scene.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::clearCentreProgress()
{
    if (m_centreProgressTitle.isEmpty() && m_centreProgressDetail.isEmpty()) {
        return;
    }
    m_centreProgressTitle.clear();
    m_centreProgressDetail.clear();
    if (isGalleryMode() && !m_gallerySizeResolveActive) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::requestDebouncedGalleryPack(GalleryPackReason reason)
{
    m_debouncedPackReason = reason;
    if (!m_layoutDebounceTimer) {
        applyLayout(reason);
        return;
    }
    m_layoutDebounceTimer->start();
}

int ImageView::pendingDecodeCount() const
{
    // Remaining work overview — not concurrent inflight. Counting only inflight
    // flickered 1↔0 as each soft job finished before the next was claimed.
    int pendingAdds = 0;
    for (int n : m_pendingWorkspacePaths) {
        pendingAdds += n;
    }
    int n = pendingAdds + m_pendingRestoreStates.size();

    if (isGalleryMode() || isWorkspaceMode()) {
        // Tiles still without display pixels (waiting for soft / first sample).
        QSet<QString> blankPaths;
        for (ImageItem *item : m_items) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            if (!item->hasDisplayPixels()) {
                blankPaths.insert(item->path());
            }
        }
        n += blankPaths.size();
        // Soft climbs still in flight for tiles that already show a sample.
        for (auto it = m_gallerySoft.cbegin(); it != m_gallerySoft.cend(); ++it) {
            if (blankPaths.contains(it.key())) {
                continue; // already counted
            }
            if (it.value().inflight > 0) {
                ++n;
            }
        }
    } else {
        // Image mode: only true in-flight climbs (single item).
        for (auto it = m_gallerySoft.cbegin(); it != m_gallerySoft.cend(); ++it) {
            if (it.value().inflight > 0) {
                ++n;
            }
        }
    }

    // Slideshow preload queue (inflight + pending neighbours).
    if (m_slideshowProgressActive) {
        n += m_ssRasterInflight.size() + m_ssRasterPending.size();
        const int need = 0; // need edge checked via target below if needed
        Q_UNUSED(need);
        if (!m_ssFromPath.isEmpty()
            && ImageCache::longEdge(m_ssFromImage) > 0
            && ImageCache::longEdge(m_ssFromImage)
                   < (slideshowTargetEdge() * 7) / 10) {
            // Current slide still soft — count as remaining quality work once.
            if (!m_ssRasterInflight.contains(m_ssFromPath)
                && !m_ssRasterPending.contains(m_ssFromPath)) {
                ++n;
            }
        }
    }
    return n;
}















































Qt::AspectRatioMode ImageView::currentFitAspectMode() const
{
    return m_fillMode ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
}



QSize ImageView::imageSize() const
{
    if (ImageItem *item = targetItem()) {
        return item->imageSize();
    }
    if (ImageItem *item = primaryItem()) {
        return item->imageSize();
    }
    return {};
}

int ImageView::itemCount() const
{
    return m_items.size();
}

QStringList ImageView::itemPaths() const
{
    QStringList paths;
    for (ImageItem *item : m_items) {
        paths.append(item->path());
    }
    return paths;
}

QStringList ImageView::selectedPaths() const
{
    QStringList paths;
    if (isImageMode()) {
        if (ImageItem *item = primaryItem()) {
            paths.append(item->path());
        } else if (hasClassicPath()) {
            paths.append(classicPath());
        }
        return paths;
    }
    // Gallery / Workspace: preserve session/canvas order, not click order.
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            paths.append(item->path());
        }
    }
    return paths;
}





void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Rapid edge clicks arrive as double-clicks (second press is not a Press event).
    // Treat them as navigation, same as a single click on the affordance.
    if (isImageMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        const EdgeZone zone = edgeZoneAt(event->pos());
        if (zone == EdgeZone::Previous) {
            emit navigatePreviousRequested();
            event->accept();
            return;
        }
        if (zone == EdgeZone::Next) {
            emit navigateNextRequested();
            event->accept();
            return;
        }
        emit fullscreenToggleRequested();
        event->accept();
        return;
    }

    // Gallery: double-click opens the tile in Image mode (classic file view).
    // Prefer SessionImageId so duplicate paths open the correct session row.
    if (isGalleryMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (item->sessionId() != kInvalidSessionImageId) {
                    emit sessionImageOpenRequested(item->sessionId());
                } else if (item->sessionIndex() >= 0) {
                    emit sessionSlotOpenRequested(item->sessionIndex());
                } else {
                    const QString path = item->path();
                    if (!path.isEmpty()) {
                        emit galleryItemOpenRequested(path);
                    }
                }
                event->accept();
                return;
            }
        }
        event->accept();
        return;
    }

    // Workspace: double-click on chrome starts a handle drag; on the image
    // body opens Image mode (same path as Gallery). Empty space is swallowed
    // so the missing second press does not clear selection via the base class.
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && m_tool == Tool::Select && m_scene) {
        const QPointF scenePos = mapToScene(event->pos());
        // Selected item handles first (chrome is above tiles).
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    selected.append(ii);
                }
            }
        }
        if (selected.size() == 1) {
            ImageItem *item = selected.first();
            if (item->beginHandleInteraction(scenePos, event->modifiers())) {
                m_handleDragItem = item;
                m_dragItem = item;
                m_dragStartState = captureState(item);
                event->accept();
                return;
            }
        } else if (selected.size() > 1) {
            const int gh = groupHandleAt(event->pos(), selected);
            if (gh >= 0 && beginGroupScale(gh, selected)) {
                event->accept();
                return;
            }
        }
        // Image body under cursor → Image mode for *this* session slot
        // (path-only open would always hit the first duplicate in the session).
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    if (ii->sessionId() != kInvalidSessionImageId) {
                        emit sessionImageOpenRequested(ii->sessionId());
                    } else if (ii->sessionIndex() >= 0) {
                        emit sessionSlotOpenRequested(ii->sessionIndex());
                    } else {
                        const QString path = ii->path();
                        if (!path.isEmpty()) {
                            emit galleryItemOpenRequested(path);
                        }
                    }
                    event->accept();
                    return;
                }
            }
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    if (m_mouseInfo.valid) {
        m_mouseInfo = {};
        emit mouseInfoChanged(m_mouseInfo);
    }
    if (m_hoverEdge != EdgeZone::None) {
        m_hoverEdge = EdgeZone::None;
        viewport()->update();
    }
    if (!m_gallery.hoverPath().isEmpty()) {
        m_gallery.clearHoverPath();
        viewport()->update();
    }
    if (m_slideshowSeekbarVisible && !m_slideshowSeekDragging) {
        m_slideshowSeekbarVisible = false;
        if (viewport()) {
            viewport()->update();
        }
    }
    QGraphicsView::leaveEvent(event);
}

