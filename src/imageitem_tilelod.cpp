// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageItem tile LOD: plan, tick, graded cache, suppress (Stage 2 demotion prep).

#include "imageitem.h"
#include "display/displayquality.h"
#include "util/biltoo_thread.h"

#include <cstdlib>
#include <cmath>
#include "tilelod/tile_lod_controller.hpp"
#include "tilelod/lod_math.hpp"
#include "tilelod/tile_lod_registry.hpp"
#include "host/thumtoocache.h"
#include "display/imagecache.h"
#include "content/contentxform.h"
#include "color/coloradjust.h"

#include <QCoreApplication>
#include "item/placementlinear.h"
#include "view/viewtransform.h"
#include "imageview.h"
#include "host/pagepath.h"
#include "host/archivepath.h"

#include <QDebug>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPolygon>
#include <QMetaObject>
#include <QTimer>


tilelod::TileLodController *ImageItem::tileLodController() const
{
    if (!m_tileLodAttached || !m_tileLodAttached->controller) {
        return nullptr;
    }
    return m_tileLodAttached->controller.get();
}

tilelod::ItemBag &ImageItem::tileLodBag()
{
    if (m_tileLodAttached) {
        return *m_tileLodAttached;
    }
    // Stage 2: bags live only in DisplayPipelineController. registerItemDisplaySurface
    // / ensureTileBag attach before paint or tick; do not climb back to ImageView.
#ifndef NDEBUG
    qWarning("ImageItem::tileLodBag: no pipeline bag (item not registered)");
    Q_ASSERT(false && "ImageItem::tileLodBag requires pipeline-owned bag");
#endif
    // Last-resort read/write sink for off-scene construction paths; not retained.
    static tilelod::ItemBag s_orphan;
    return s_orphan;
}

const tilelod::ItemBag &ImageItem::tileLodBag() const
{
    if (m_tileLodAttached) {
        return *m_tileLodAttached;
    }
    static const tilelod::ItemBag kEmpty;
    return kEmpty;
}

void ImageItem::attachTileLodBag(tilelod::ItemBag *bag)
{
    if (!bag) {
        return;
    }
    m_tileLodAttached = bag;
}

void ImageItem::detachTileLodBag()
{
    m_tileLodAttached = nullptr;
}

qreal ImageItem::tileDevicePerContent() const
{
    // Tile grid is always *file-native* pixels (setContentSize(native)).
    // Density must be screen pixels per *native* pixel — not per item-local
    // unit. Gallery layouts often leave m_galleryCellSize empty (non-GridCrop
    // pack) while item scene bounds are the cell; using screenScale() alone
    // then treated local→view (~1) as dpc and forced target_scale=0 (full
    // archive decode: Performance report s=0 0,0 with 145 tiles queued).
    qreal dpr = 1.0;
    qreal viewScale = 1.0;
    if (scene() && !scene()->views().isEmpty() && scene()->views().first()) {
        QGraphicsView *view = scene()->views().first();
        if (QWidget *vp = view->viewport()) {
            dpr = vp->devicePixelRatioF();
        }
        const QTransform vt = view->transform();
        qreal sMax = 1.0;
        qreal sMin = 1.0;
        PlacementLinear::singularValues2x2(vt.m11(), vt.m12(), vt.m21(), vt.m22(),
                                           &sMax, &sMin);
        viewScale = ViewTransform::floorScale(sMax);
    }
    if (!(dpr > 0.0)) {
        dpr = 1.0;
    }

    QSize native = tileNativeSize();
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        native = imageSize();
    }
    const qreal contentLong = qMax(native.width(), native.height());
    if (!(contentLong > 1.0)) {
        return screenScale() * dpr;
    }

    // Scene footprint of this item (cell in Gallery, placed bounds in Image).
    qreal sceneLong = 0.0;
    if (!m_galleryCellSize.isEmpty()) {
        sceneLong = qMax(m_galleryCellSize.width(), m_galleryCellSize.height());
    } else {
        const QRectF br = sceneBoundingRect();
        sceneLong = qMax(br.width(), br.height());
    }
    if (!(sceneLong > 0.0)) {
        return screenScale() * dpr;
    }
    const qreal screenLong = sceneLong * viewScale * dpr;
    const qreal dpc = screenLong / contentLong;
    if (dpc > 0.0 && std::isfinite(dpc)) {
        return dpc;
    }
    return screenScale() * dpr;
}

/**
 * Single live content-meta reader for tile plan and chrome marks (Phase 7).
 * Applied ContentXform fingerprint only (survives clearDecodedPixels;
 * identity clearLiveContentMeta drops it).
 */
ContentXform::Value ImageItem::tileContentXform() const
{
    if (m_hasAppliedContentXform) {
        return m_appliedContentXform;
    }
    return {};
}

ContentXform::Value ImageItem::liveContentXformForPaint() const
{
    if (scene() && !scene()->views().isEmpty()) {
        if (auto *iv = qobject_cast<ImageView *>(scene()->views().first())) {
            return iv->itemAppliedContentXform(this);
        }
    }
    return tileContentXform();
}

ColorAdjustments ImageItem::liveColorForPaint() const
{
    if (scene() && !scene()->views().isEmpty()) {
        if (auto *iv = qobject_cast<ImageView *>(scene()->views().first())) {
            return iv->itemLiveColor(this);
        }
    }
    return m_colorAdjust;
}


static quint64 colorAdjustSignature(const ColorAdjustments &g)
{
    // Stable fingerprint for graded-tile cache keys.
    quint64 h = 1469598103934665603ull;
    auto mix = [&](quint64 v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(static_cast<quint64>(static_cast<uint32_t>(g.brightness)));
    mix(static_cast<quint64>(static_cast<uint32_t>(g.contrast)));
    mix(static_cast<quint64>(static_cast<uint32_t>(g.saturation)));
    mix(static_cast<quint64>(static_cast<uint32_t>(g.hue)));
    mix(static_cast<quint64>(qRound(g.gamma * 1000.0)));
    mix(g.invert ? 1ull : 0ull);
    return h;
}

void ImageItem::clearTileGradedCache() const
{
    tileLodBag().gradedCache.clear();
    tileLodBag().gradeSig = 0;
}

QImage ImageItem::resolveGradedTile(tilelod::TileKey const &key,
                                    ColorAdjustments const &grade) const
{
    if (!tileLodBag().controller || !tileLodBag().controller->session()) {
        return {};
    }
    tilelod::CacheEntry const *e = tileLodBag().controller->session()->cache().find(key);
    if (!e || e->state != tilelod::TileState::Succeeded || !e->bitmap.valid()) {
        return {};
    }
    // Cache QImage conversion for identity and graded alike. Identity used to
    // re-copy every rgba8 cell on every paint frame (256²×4 per tile).
    const quint64 sig = colorAdjustSignature(grade);
    if (sig != tileLodBag().gradeSig) {
        tileLodBag().gradedCache.clear();
        tileLodBag().gradeSig = sig;
    }
    // ~96 MiB of rgba cell images; LRU eviction instead of nuke-at-256.
    constexpr int kGradedCacheMaxKiB = 96 * 1024;
    if (tileLodBag().gradedCache.maxCost() < kGradedCacheMaxKiB) {
        tileLodBag().gradedCache.setMaxCost(kGradedCacheMaxKiB);
    }
    // Pack scale,x,y into one key — avoid QString alloc per cell per paint.
    const quint64 ck = (static_cast<quint64>(static_cast<uint32_t>(key.scale)) << 42)
                       | (static_cast<quint64>(static_cast<uint32_t>(key.x)) << 21)
                       | static_cast<quint64>(static_cast<uint32_t>(key.y));
    if (const QImage *hit = tileLodBag().gradedCache.object(ck)) {
        return *hit; // QImage is implicitly shared
    }
    QImage img = tilelod::tile_bitmap_to_qimage(e->bitmap);
    if (img.isNull()) {
        return {};
    }
    if (!grade.isIdentity()) {
        img = applyColorAdjustments(img, grade);
    }
    const int costKiB = ImageCache::rgbaCostKiB(img);
    auto *stored = new QImage(std::move(img));
    tileLodBag().gradedCache.insert(ck, stored, costKiB);
    return *stored;
}

QSize ImageItem::tileNativeSize() const
{
    const QSize cached = ThumtooCache::cachedSize(m_path);
    if (cached.isValid() && cached.width() > 0 && cached.height() > 0) {
        return cached;
    }
    // PDF/EPUB/DjVu page refs and archive members: never use soft/ladder
    // imageSize() as the tile grid. Thumtoo keys cells by authoritative page
    // size; a PreferCache sample (~512 long edge) as "native" yields wrong
    // (x,y) at every scale → Failed denser cells and PARENT-only paint.
    if (PagePath::isPageRef(m_path) || PagePath::isTextForceRef(m_path)
        || PagePath::isPdfImageRef(m_path) || ArchivePath::isArchiveRef(m_path)) {
        return {};
    }
    // Layout imageSize() is *oriented* / post-crop content size. Tile grid is
    // always file-native. Orient or crop makes layout ≠ native (UV / density
    // mismatch → squished tiles in a correct crop box).
    const ContentXform::Value x = liveContentXformForPaint();
    if (ContentXform::normalizeQuarterTurns(x.quarterTurns) != 0
        || x.hFlip || x.vFlip
        || (x.hasCrop && !x.cropRect.isEmpty())) {
        return {};
    }
    // Raster identity xform only: layout size matches native probe.
    // Soft/ladder samples are still possible here — callers that need a hard
    // guarantee should wait for ProcessMemos / sizeReady.
    return imageSize();
}

bool ImageItem::tileLodWanted() const
{
    if (tileLodSuppressed() || m_path.isEmpty() || !ThumtooCache::isAvailable()) {
        return false;
    }
    // Durable tiles are a *speed* optimization (Store hits), not a requirement.
    // Interactive request_tiles encodes on miss (JPEG DCT shrink for scale>0).
    // Requiring hasDurableTiles forced PreferCache/Full whole-frame climbs
    // (often →2048) before any tile cell could run — redundant with the tile path.
    // Prefer thumtoo file-native size. Fall back to imageSize only when content
    // xform is identity (layout size == native). Never use oriented layout as
    // tile-grid native (UV / density mismatch — see tileNativeSize).
    QSize native = tileNativeSize();
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        const ContentXform::Value x = liveContentXformForPaint();
        if (ContentXform::normalizeQuarterTurns(x.quarterTurns) == 0
            && !x.hFlip && !x.vFlip
            && !(x.hasCrop && !x.cropRect.isEmpty())) {
            native = imageSize();
        }
    }
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        return false;
    }
    // Skip only when the *file-native* grid is smaller than one tile cell.
    // Layout/crop size may be < 256 while native tiles still exist.
    if (qMax(native.width(), native.height()) < 256) {
        return false;
    }

    // Gallery packed cell: threshold is *on-screen cell* long edge, not
    // content×(view×item). galleryCellSize is already scene footprint (item
    // scale baked in); only the view transform maps scene → device pixels.
    // Using tileDevicePerContent() (view×item) here under-counted by ~itemScale
    // and blocked Ctrl+wheel inspection tiles (TILE_LOD_RUNTIME.md).
    if (!m_galleryCellSize.isEmpty()) {
        qreal viewScale = 1.0;
        qreal dpr = 1.0;
        if (scene() && !scene()->views().isEmpty() && scene()->views().first()) {
            QGraphicsView *view = scene()->views().first();
            const QTransform vt = view->transform();
            qreal sMax = 1.0;
            qreal sMin = 1.0;
            PlacementLinear::singularValues2x2(vt.m11(), vt.m12(), vt.m21(), vt.m22(), &sMax, &sMin);
            viewScale = ViewTransform::floorScale(sMax);
            if (QWidget *vp = view->viewport()) {
                dpr = vp->devicePixelRatioF();
            }
        }
        if (!(dpr > 0.0)) {
            dpr = 1.0;
        }
        const qreal cellLong = qMax(m_galleryCellSize.width(), m_galleryCellSize.height());
        if (!(cellLong > 0.0)) {
            return false;
        }
        const qreal screenLong = cellLong * viewScale * dpr;
        // Same ~32px screen floor as Image mode. LQIP is underlay only until
        // tiles cover — not a substitute for available 256² cells.
        constexpr qreal kGalleryTileScreenMin = 32.0;
        return screenLong > kGalleryTileScreenMin;
    }

    // Image / Workspace: layout content long edge × device-per-content.
    const QSize isz = imageSize();
    const int displayLong = qMax(isz.width(), isz.height());
    if (displayLong < 1) {
        return false;
    }
    return tilelod::TileLodController::shouldUseTiles(
        tileDevicePerContent(), displayLong);
}

void ImageItem::prepareTileLodPlan()
{
    if (!tileLodWanted()) {
        return;
    }
    if (!m_tileLodAttached) {
        return;
    }
    QSize native = tileNativeSize();
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        const ContentXform::Value x = liveContentXformForPaint();
        if (ContentXform::normalizeQuarterTurns(x.quarterTurns) == 0
            && !x.hFlip && !x.vFlip
            && !(x.hasCrop && !x.cropRect.isEmpty())) {
            native = imageSize();
        }
    }
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        return;
    }
    if (!tileLodBag().controller) {
        tileLodBag().controller = std::make_unique<tilelod::TileLodController>();
        tileLodBag().controller->setPath(m_path);
    } else if (tileLodBag().controller->path() != m_path) {
        tileLodBag().controller->setPath(m_path);
    }
    // Never plan against a controller bound to another path (global cache is
    // path-keyed; wrong bind would paint retained tiles for the wrong file).
    if (!tileLodBag().controller || tileLodBag().controller->path() != m_path) {
        tileLodBag().controller.reset();
        return;
    }
    // Retained path RAM: first plan after rebind must not skip set_viewport
    // (lastDpc/vis still describe the previous file).
    if (tileLodBag().controller->hasRetainedTiles() && tileLodBag().lastUpdateGen == 0
        && tileLodBag().lastDpc > 0.0) {
        tileLodBag().lastDpc = -1.0;
        tileLodBag().lastVisSource = QRectF();
    }
    // Tile grid is always full native (source) size.
    // Gallery: floor min_scale at density so overview never requests scale 0
    // (full JPEG decode). Image/Workspace: min_scale 0 for rasters so density
    // can climb to full-res; document pages (PDF/DjVu/EPUB //page:) may use
    // negative scales for live denser tiles (thumtoo; not stored below -2).
    // SVG: treated as raster/image path today — no negative scale until a
    // document-page URI path exists.
    const bool documentLiveTiles =
        PagePath::isPageRef(m_path) || PagePath::isTextForceRef(m_path);
    const int scaleFloor =
        documentLiveTiles ? tilelod::kDocumentLiveMinScale : 0;
    int minScale = scaleFloor;
    bool galleryLayout = !m_galleryCellSize.isEmpty();
    if (!galleryLayout && scene() && !scene()->views().isEmpty()) {
        if (auto *iv = qobject_cast<ImageView *>(scene()->views().first())) {
            galleryLayout = iv->isGalleryMode();
        }
    }
    if (galleryLayout) {
        // Density (screen dpc) is the request floor — overview stays coarse;
        // inspection zoom lowers dens so interactive encode-on-miss can climb.
        // Do not floor on durableTileMinScale: Store is a speed cache, not a
        // request limit (Image/Workspace use min_scale 0 for the same reason).
        const qreal dpc0 = tileDevicePerContent();
        const int maxS = tilelod::max_scale_for_size(native.width(), native.height());
        const int dens = tilelod::target_scale_for_density(
            static_cast<double>(dpc0), scaleFloor, maxS);
        minScale = dens;
        // Hard floor uses *screen* long edge (not scene). Scene-only floor kept
        // min_scale elevated after Ctrl+wheel zoom — dens asked for finer tiles
        // but the floor still thought the packed cell was tiny.
        const qreal contentLong = qMax(native.width(), native.height());
        qreal sceneLong = 0.0;
        if (!m_galleryCellSize.isEmpty()) {
            sceneLong = qMax(m_galleryCellSize.width(), m_galleryCellSize.height());
        } else {
            const QRectF br = sceneBoundingRect();
            sceneLong = qMax(br.width(), br.height());
        }
        qreal viewScale = 1.0;
        qreal dpr = 1.0;
        if (scene() && !scene()->views().isEmpty() && scene()->views().first()) {
            QGraphicsView *view = scene()->views().first();
            const QTransform vt = view->transform();
            qreal sMax = 1.0;
            qreal sMin = 1.0;
            PlacementLinear::singularValues2x2(vt.m11(), vt.m12(), vt.m21(), vt.m22(),
                                               &sMax, &sMin);
            viewScale = ViewTransform::floorScale(sMax);
            if (QWidget *vp = view->viewport()) {
                dpr = vp->devicePixelRatioF();
            }
        }
        if (!(dpr > 0.0)) {
            dpr = 1.0;
        }
        const qreal screenLong = sceneLong * viewScale * dpr;
        if (contentLong > 512.0 && screenLong > 0.0) {
            if (screenLong * 8.0 < contentLong && minScale < 3) {
                minScale = 3;
            } else if (screenLong * 4.0 < contentLong && minScale < 2) {
                minScale = 2;
            } else if (screenLong * 2.0 < contentLong && minScale < 1) {
                minScale = 1;
            }
        }
    }
    const quint64 genBefore =
        tileLodBag().controller->session() ? tileLodBag().controller->session()->generation() : 0;
    tileLodBag().controller->setContentSize(native.width(), native.height(), minScale);
    // min_scale lower (1230) bumps generation but dpc/vis may be unchanged —
    // clear the skip cache so updateViewport re-plans at the new floor.
    if (tileLodBag().controller->session()
        && tileLodBag().controller->session()->generation() != genBefore) {
        tileLodBag().lastDpc = -1.0;
    }

    const qreal dpc = tileDevicePerContent();
    QRectF visLocal = contentRect();
    if (scene() && !scene()->views().isEmpty() && scene()->views().first()) {
        QGraphicsView *view = scene()->views().first();
        // Polygon map (not only scene AABB): item/placement rotation makes the
        // axis-aligned scene rect a poor proxy for the visible local region.
        const QPolygonF scenePoly =
            view->mapToScene(QPolygon(view->viewport()->rect()));
        const QRectF sceneVis = scenePoly.boundingRect();
        const bool onScreen =
            sceneBoundingRect().intersects(sceneVis)
            || sceneVis.contains(sceneBoundingRect().center());
        const QPolygonF localPoly = mapFromScene(scenePoly);
        const QRectF localVis = localPoly.boundingRect();
        const QRectF hit = localVis.intersected(contentRect());
        if (!hit.isEmpty()) {
            visLocal = hit;
        } else if (onScreen) {
            // Still on screen but AABB miss (rotated placement / float) — full
            // content so we do not drop the plan to empty.
            visLocal = contentRect();
        } else {
            return;
        }
    }
    if (visLocal.isEmpty()) {
        return;
    }
    // Item local → display content (0..layout); then → source for the planner.
    const QRectF visDisplay = visLocal.translated(-offset());
    const ContentXform::Value x = liveContentXformForPaint();
    QRectF visSource = ContentXform::mapDisplayRectToSource(visDisplay, native, x);
    if (visSource.isEmpty()
        || visSource.width() < 1.0 || visSource.height() < 1.0) {
        // Never treat oriented display coords as source — that under/over-culls
        // after quarter-turns. Full native is the safe request set.
        visSource = QRectF(0, 0, native.width(), native.height());
    } else {
        // Float / AABB inverse: pad one content pixel so edge tiles are not
        // clipped out of the plan under orient.
        visSource = visSource.adjusted(-1.0, -1.0, 1.0, 1.0);
        visSource = visSource.intersected(
            QRectF(0, 0, native.width(), native.height()));
    }

    // Quantize content-space visibility to whole pixels. Subpixel jitter from
    // view transforms otherwise changes tile index edges every frame →
    // generation storms, perpetual InFlight denser cells, PARENT-only paint.
    visSource = QRectF(QRect(visSource.toAlignedRect())).intersected(
        QRectF(0, 0, native.width(), native.height()));
    if (visSource.isEmpty()) {
        visSource = QRectF(0, 0, native.width(), native.height());
    }

    // Completions arrive off the GUI; without a wake, pump never runs until the
    // next pan/scroll and new tiles never repaint (ImageView "stuck coarse").
    // Install on *every* prepare — including the static-viewport early return —
    // so a rebound session / slideshow phase controller is never left without a
    // wake while density is unchanged.
    auto installTileWake = [this]() {
        if (!tileLodBag().controller || !tileLodBag().controller->session()) {
            return;
        }
        std::shared_ptr<bool> alive = tileLodBag().alive;
        tileLodBag().controller->session()->set_wake([this, alive]() {
            QTimer::singleShot(0, QCoreApplication::instance(), [this, alive]() {
                if (!alive || !*alive || !tileLodBag().controller) {
                    return;
                }
                const int applied = tileLodBag().controller->tick(12);
                // Pump remaining inbox / progressive climb without a paint storm.
                // singleShot(0) every completion while inflight was regenerating
                // the tile plan every frame (generation spin, denser stuck).
                if (alive && *alive && tileLodBag().controller
                    && !tileLodBag().controller->viewportFullyCovered()) {
                    auto *session = tileLodBag().controller->session();
                    if (session
                        && (session->request_scale_holding()
                            || session->coverage().in_flight > 0
                            || applied > 0)) {
                        QTimer::singleShot(16, QCoreApplication::instance(),
                                           [this, alive]() {
                            if (!alive || !*alive || !tileLodBag().controller) {
                                return;
                            }
                            const int more = tileLodBag().controller->tick(12);
                            if (more > 0) {
                                update();
                            }
                        });
                    }
                }
                if (applied > 0) {
                    update();
                    if (scene()) {
                        for (QGraphicsView *v : scene()->views()) {
                            if (v && v->viewport()) {
                                v->viewport()->update();
                            }
                        }
                    }
                }
            });
        });
    };

    // Skip set_viewport when density and visible region are unchanged — paint
    // runs this every frame while tiles stream in; replanning is pure waste.
    // Progressive climb advances in TileSession::pump/issue (tick path), not here.
    if (tileLodBag().controller->session()
        && tileLodBag().lastDpc > 0.0
        && qAbs(dpc - tileLodBag().lastDpc) < 1e-4
        && qAbs(visSource.x() - tileLodBag().lastVisSource.x()) < 0.5
        && qAbs(visSource.y() - tileLodBag().lastVisSource.y()) < 0.5
        && qAbs(visSource.width() - tileLodBag().lastVisSource.width()) < 0.5
        && qAbs(visSource.height() - tileLodBag().lastVisSource.height()) < 0.5) {
        installTileWake();
        return;
    }
    tileLodBag().lastDpc = dpc;
    tileLodBag().lastVisSource = visSource;
    // Draw-plan Underlay: has_lqip when an EMB/LQIP-band sample exists on the
    // item or in ImageCache (install may lag one frame after Store seed).
    {
        auto embBand = [](const QImage &img) {
            return !img.isNull() && qMax(img.width(), img.height())
                <= DisplayQuality::kEmbeddedUnderlayMaxEdge;
        };
        bool haveSample = embBand(m_source) || embBand(m_preview);
        if (!haveSample && !pixmap().isNull()) {
            haveSample = qMax(pixmap().width(), pixmap().height())
                <= DisplayQuality::kEmbeddedUnderlayMaxEdge;
        }
        if (!haveSample && !m_path.isEmpty()) {
            haveSample = embBand(ImageCache::getUnderlay(m_path));
        }
        tileLodBag().controller->setHasLqip(haveSample);
    }
    // Prefetch margin in content pixels: ~one tile side of *screen* space
    // (256 device px). Enough to absorb small pans without issuing a second
    // ring of cells; not a full off-screen ring (that multiplies issue work).
    const double margin = 256.0 / qMax(1e-6, dpc);
    tileLodBag().controller->updateViewport(visSource, dpc, margin);
    installTileWake();
}

void ImageItem::prepareTileLod()
{
    prepareTileLodPlan();
}

void ImageItem::tickTileLod(int budget)
{
    // Sole per-item tile service entry (called only from TileLoadCoordinator).
    // Leave tile band: do not pump/issue on a stale deep-zoom viewport.
    if (!tileLodWanted()) {
        // Size probe may still be pending — without it we never enter the band.
        if (!m_path.isEmpty() && !ThumtooCache::cachedSize(m_path).isValid()) {
            ThumtooCache::scheduleProbe(m_path);
        }
        if (!m_interactive && cacheMode() == QGraphicsItem::NoCache
            && hasDisplayPixels()) {
            syncGalleryScrollCache();
        }
        return;
    }
    // Gallery tile cells: plan changes often — keep NoCache while in the band
    // so ItemCoordinateCache cannot freeze an LQIP pixmap over live tiles.
    if (!m_interactive && cacheMode() != QGraphicsItem::NoCache) {
        setCacheMode(QGraphicsItem::NoCache);
    }
    prepareTileLod();
    if (!tileLodBag().controller) {
        return;
    }
    const int applied = tileLodBag().controller->tick(budget);
    const std::uint64_t gen =
        tileLodBag().controller->session() ? tileLodBag().controller->session()->generation() : 0;
    // Repaint only when tiles actually landed — gen-only changes every pan
    // queued a singleShot(0) storm (100% CPU, still LQIP).
    if (applied > 0) {
        tileLodBag().lastUpdateGen = gen;
        if (!m_interactive) {
            setCacheMode(QGraphicsItem::NoCache);
            // Keep LQIP pixmap until paint draws tiles over it — clearing
            // caused temporary disappear (blank cells while plan catches up).
        }
        if (!tileLodBag().repaintQueued) {
            tileLodBag().repaintQueued = true;
            QGraphicsScene *sc = scene();
            QObject *ctx = sc ? static_cast<QObject *>(sc)
                              : static_cast<QObject *>(QCoreApplication::instance());
            std::shared_ptr<bool> alive = tileLodBag().alive;
            QTimer::singleShot(0, ctx, [this, sc, alive]() {
                if (!alive || !*alive) {
                    return;
                }
                if (sc && scene() != sc) {
                    tileLodBag().repaintQueued = false;
                    return;
                }
                tileLodBag().repaintQueued = false;
                update();
            });
        }
    } else if (gen != tileLodBag().lastUpdateGen) {
        tileLodBag().lastUpdateGen = gen;
        // Plan-only change (pan): cheap mark, no pixmap clear.
        update();
    } else if (tileLodBag().lastUpdateGen == 0 && tileLodBag().controller->hasRetainedTiles()) {
        // A→B→A: Succeeded tiles already in shared cache — pump applied 0 but
        // paint must run once so retained cells appear without waiting for issue.
        tileLodBag().lastUpdateGen = gen ? gen : 1;
        update();
    }
}

bool ImageItem::tileLodActive() const
{
    // Succeeded tiles in the *shared path cache* count even before this
    // session's first pump — otherwise paint treats the cell as underlay-only
    // and draws EMB while TileLodRegistry already holds the pyramid.
    if (!tileLodBag().controller || !tileLodBag().controller->enabled()) {
        return false;
    }
    return tileLodBag().controller->hasAnyTile()
        || tileLodBag().controller->hasRetainedTiles();
}

bool ImageItem::tileLodHasPathRam() const
{
    if (m_path.isEmpty()) {
        return false;
    }
    if (tileLodBag().controller && tileLodBag().controller->hasRetainedTiles()) {
        return true;
    }
    return tilelod::TileLodRegistry::instance().has_succeeded_tiles(m_path);
}

bool ImageItem::tileLodViewportCovered() const
{
    return tileLodBag().controller && tileLodBag().controller->viewportFullyCovered();
}

bool ImageItem::tileLodSettled() const
{
    return tileLodBag().controller && tileLodBag().controller->viewportSettled();
}

QString ImageItem::tileLodDebugLine() const
{
    const QString name = QFileInfo(m_path).fileName();
    if (!tileLodWanted()) {
        return QStringLiteral("%1 wanted=0 disp=%2")
            .arg(name)
            .arg(displayPixelLongEdge());
    }
    const int pathRam = static_cast<int>(
        tilelod::TileLodRegistry::instance().path_succeeded_count(m_path));
    if (!tileLodBag().controller || !tileLodBag().controller->session()) {
        return QStringLiteral("%1 wanted=1 session=0 pathRam=%2 disp=%3")
            .arg(name)
            .arg(pathRam)
            .arg(displayPixelLongEdge());
    }
    const tilelod::TileSession::DebugSnapshot s =
        tileLodBag().controller->session()->debug_snapshot();
    // plan=E/P/U/H: Exact / Parent / Underlay / Hole counts for visible keys.
    return QStringLiteral(
               "%1 tgt=%2 des=%3 st=%4 min=%5 max=%6 vis=%7 exact=%8 miss=%9 "
               "fail=%10 inflight=%11 cacheOk=%12 s0=%13 plan=%14/%15/%16/%17 "
               "lqip=%18 hold=%19 reached=%20 gen=%21 pathRam=%22 disp=%23")
        .arg(name)
        .arg(s.target_scale)
        .arg(s.desired_scale)
        .arg(s.stable_scale)
        .arg(s.min_scale)
        .arg(s.max_scale)
        .arg(s.visible)
        .arg(s.exact_succeeded)
        .arg(s.missing)
        .arg(s.failed)
        .arg(s.in_flight)
        .arg(s.cache_succeeded)
        .arg(s.scale0_ok)
        .arg(s.plan_exact)
        .arg(s.plan_parent)
        .arg(s.plan_underlay)
        .arg(s.plan_empty)
        .arg(s.has_lqip ? 1 : 0)
        .arg(s.holding ? 1 : 0)
        .arg(s.reached_desired ? 1 : 0)
        .arg(static_cast<qulonglong>(s.generation))
        .arg(pathRam)
        .arg(displayPixelLongEdge());
}
