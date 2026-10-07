// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMTOOCACHE_H
#define THUMTOOCACHE_H

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QRectF>
#include <QVector>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include "tilelod/tile_backend.hpp"
#include "tilelod/tile_types.hpp"

/**
 * Thin biltoo façade over thumtoo::Client (durable size index + ladder).
 * thumtoo is a required dependency (see CMakeLists.txt).
 */
namespace ThumtooCache {

/**
 * Notifies the UI when durable cache rows become ready (Qt Executor → GUI).
 */
class Bridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    /** Native size known for session path (may still lack ladder pixels). */
    void sizeReady(const QString &path, const QSize &size);
    /** Ladder level available; UI should reload soft preview for path. */
    void ladderReady(const QString &path, int maxEdge, const QImage &image);
    /** PixelSource as int (thumtoo::PixelSource); 0 = unknown. */
    void ladderProvenance(const QString &path, int maxEdge, int pixelSource);
    /**
     * First time this process sees durable tiles for path (pyramid ready).
     * ImageView starts tile LOD if already past soft max.
     */
    void durableTilesReady(const QString &path);
};

/** Process-wide notifier (created on first use). */
Bridge *bridge();

/**
 * Open the default XDG cache client once (safe to call repeatedly).
 * Prefer calling after QApplication exists so callbacks can use the Qt loop.
 */
void init();
/** Force THUMTOO_DEBUG-style traces (stderr + ~/.cache/biltoo/thumtoo-debug.log). */
void enableDebugTracing();

/** Drop the client (join thumtoo worker). Safe to call more than once. */
void shutdown();

/**
 * Soft ladder long-edge targets (see thumtoo kMaxSoftLadderEdge = 512).
 * get_pixels / cachedLadderBytes return the largest soft level ≤ request.
 * Soft band clamps at kGalleryLadderEdge (ephemeral / TileSynth). Gallery
 * display edges may be higher: loadThumbnail shrink-on-decode at the on-screen
 * ladder step (never native full decode). docs/GALLERY_SOFT.md
 */
constexpr int kLadderEdges[] = {128, 256, 512, 1024, 2048, 4096, 8192};
constexpr int kFilmstripLadderEdge = 256;
constexpr int kGalleryLadderEdge = 512;  // soft-band clamp (thumtoo kMaxSoftLadderEdge)
/** FastBatch overview max (thumtoo kBatchMaxEdge) — Q1 JpegShrink / TileSynth. */
constexpr int kBatchOverviewEdge = 1024;
/** Highest ladder step used for display-edge snap (interim; tiles later). */
constexpr int kImageLadderEdge = 8192;

/** Smallest ladder step ≥ displayLongEdge (px); max step if larger. */
inline int ceilLadderEdge(int displayLongEdge)
{
    if (displayLongEdge <= 0) {
        return kGalleryLadderEdge;
    }
    for (int e : kLadderEdges) {
        if (e >= displayLongEdge) {
            return e;
        }
    }
    return kLadderEdges[sizeof(kLadderEdges) / sizeof(kLadderEdges[0]) - 1];
}


/**
 * Cache-only native size for a session path (file or //archive: ref).
 * @param scheduleRevalidate  When true, may queue a background mtime/size
 *   check against the source. Default is false: cache hits must stay cheap on
 *   paint/layout/sort. Pass true only for intentional freshness checks.
 */
QSize cachedSize(const QString &path, bool scheduleRevalidate = false);
/** Process memo only — workers/sizeReady call this; GUI must not Store-query. */
void noteCachedSize(const QString &path, const QSize &size);
/** Drop process size memo so tileNativeSize cannot keep a pre-reload WxH. */
void forgetCachedSize(const QString &path);

/**
 * Cache-only file size and mtime from the Store locator (no source I/O).
 * Values are source fingerprint fields stored at last probe (bytes, ns epoch).
 * @return true if at least one of size/mtime is present in the index.
 */
bool cachedFileStat(const QString &path, qint64 *sizeBytes, qint64 *mtimeNs);

/**
 * Compare source file mtime/size to the Store locator fingerprint (worker).
 * @p done is invoked on the GUI thread: true if the source changed (or no
 * locator / unreadable source), false if fingerprints still match.
 * Used by soft F5 so an unchanged file does not force a full re-decode.
 */
void checkSourceChanged(const QString &path, std::function<void(bool changed)> done);

/**
 * Process ImageCache underlay sample when already seeded (LQIP/EMB).
 * GUI: ImageCache only. Worker: if process cache misses, may read Store
 * get_lqip / get_embedded_preview (no generation) and seed ImageCache.
 */
QImage cachedLqipImage(const QString &path);

/**
 * Cache-only EXIF / PDF /Thumb JPEG underlay (thumtoo EmbeddedJpeg kind).
 * Empty if missing — does not open the source. Prefer over LQIP when both exist.
 */
QImage cachedEmbeddedPreviewImage(const QString &path);

/**
 * Gallery underlay miss: load Store EMB/LQIP into ImageCache on a worker, then
 * emit sizeReady so the GUI can tryInstallGalleryUnderlay. No-op when a usable
 * underlay is already cached. Deduped per path. Never opens the source file.
 */
void scheduleStoreUnderlaySeed(const QString &path);

/**
 * Store vs process underlay consistency for one path (debug / recovery).
 * Built off the GUI thread when Store fields are needed.
 */
struct UnderlayConsistency {
    QString path;
    bool processHasUnderlay = false;
    int processUnderlayEdge = 0;
    QString processKind; ///< LQIP / EMB / RASTER / empty
    bool storeHasLqip = false;       ///< ThumbHash/Handsum row (not EmbeddedJpeg)
    bool storeHasEmbedded = false;   ///< EXIF/PDF thumb kind
    bool durableTilesKnown = false;
    bool durableTilesInStore = false; ///< hasDurableTiles() worker truth when probed
    bool sizeMemo = false;
    QSize sizeMemoValue;
    QStringList issues; ///< human-readable inconsistency notes
};

/**
 * Inspect process ImageCache + Store for underlay/LQIP/tiles agreement.
 * Safe from GUI for process-only fields; Store reads run on a worker when
 * @p includeStore is true (callback always on GUI thread).
 */
void checkUnderlayConsistency(const QString &path, bool includeStore,
                              std::function<void(UnderlayConsistency)> done);

/**
 * When durable tiles exist but Store has no LQIP: encode LQIP from free tile
 * data (ensure_lqip) and seed ImageCache if underlay still empty. Deduped.
 * Never opens the source file.
 */
void scheduleEnsureLqipFromTiles(const QString &path);



/**
 * Cache-only: thumtoo reported ContentStatus::Unsupported for this locator.
 * Callers should stop scheduling probes/pixels (Failed remains retryable).
 */
bool isUnsupported(const QString &path);

/**
 * Memo: open/probe reported the source unavailable (library error — no exists()).
 * GUI-safe read. Used to show “cached only” chrome while tiles/LQIP remain.
 */
void noteSourceUnavailable(const QString &path);
void clearSourceUnavailable(const QString &path);
bool isSourceUnavailable(const QString &path);

/**
 * One Store size lookup on a worker (or sync when already off-GUI).
 * Callback runs on the completion thread — marshal to GUI if needed.
 * @p lqip may be null when LQIP is unavailable or already cached elsewhere.
 */
void requestSizeAsync(const QString &path,
                      std::function<void(bool ok, const QSize &size, const QImage &lqip)> callback);

/**
 * Schedule a background size probe into the serial FIFO when missing.
 * Does not block; does not drain the queue on the GUI thread.
 * Emits Bridge::sizeReady (including process-memo hits). No-op when unsupported.
 */
void scheduleProbe(const QString &path);

/**
 * Queue many size probes in one pass (session-order FIFO, bounded parallel
 * Store work). Prefer this for cold Gallery open over per-path scheduleProbe.
 */
void scheduleProbeBatch(const QStringList &paths);

/**
 * True while the size-probe FIFO has queued or in-flight Store size work.
 * Hosts should avoid scheduling tile pyramids until this is false so cold
 * Gallery open keeps workers on size resolution.
 */
bool sizeProbesBusy();

struct ProbeQueueSnapshot {
    int queued = 0;
    int inflight = 0;
    bool busy = false;
};
ProbeQueueSnapshot probeQueueSnapshot();

/**
 * Session Open / Replace: drop the host size-probe FIFO and bump the probe
 * generation so in-flight Store callbacks do not emit sizeReady or refill
 * ImageCache for the previous path set. In-flight slots still complete
 * (decrement counters) but are otherwise ignored.
 */
void cancelSizeProbes();

/** Monotonic epoch bumped by cancelSizeProbes (session replace). */
quint64 sizeProbeGeneration();

/** Phase-1 work status: size-probe queued/running counts + sample URIs. */
/** Live thumtoo activity snapshot (size / archive / soft / tiles). */
struct WorkActivity {
    quint64 sizeQueued = 0;
    quint64 sizeRunning = 0;
    QStringList sizeRunningUris;
    quint64 archiveReadRunning = 0;
    QStringList runningArchiveLabels;
    quint64 softQueued = 0;
    quint64 softRunning = 0;
    QStringList softRunningUris;
    quint64 tileQueued = 0;
    quint64 tileRunning = 0;
    QStringList tileRunningLabels;
};
WorkActivity workActivity();
/** True if any size/archive/soft/tile work is queued or running. */
bool workActivityBusy();

/** thumtoo Client::queue_stats snapshot (zeros when client unavailable). */
struct HostQueuePressure {
    quint64 pending = 0;
    int inflight = 0;
    int focusFullInflight = 0;
    quint64 sizeProbeQueued = 0;
    quint64 sizeProbeRunning = 0;
};
HostQueuePressure hostQueuePressure();
/** @deprecated name — prefer workActivity(). */
using SizeProbeActivity = WorkActivity;
SizeProbeActivity sizeProbeActivity();

/**
 * Cache-only ladder payload (usually JPEG-XL) with long edge <= maxEdge.
 * Empty if thumtoo has no level yet. Caller decodes (e.g. via libvips).
 */
QByteArray cachedLadderBytes(const QString &path, int maxEdge);

/**
 * Soft-band PreferCache (legacy name → scheduleSoftPixels).
 * **Product hosts must not call this** — use scheduleDisplayPixels only when
 * hasDurableTilesKnown, else scheduleTilePyramid (LQIP + tiles).
 * @return false if skipped (already in-flight, settled success, or unsupported).
 */
bool schedulePixels(const QString &path, int maxEdge);
/** True if soft-band PreferCache for path#edge is queued or decoding. */
bool isPixelsPending(const QString &path, int maxEdge);
/** True when PreferCache already finished for this path#edge (hit or miss). */
bool isPixelsSettled(const QString &path, int maxEdge);

/**
 * TileSynth whole-frame from durable tiles only (≤ kImageLadderEdge).
 * Returns false when cold (no durable tiles) — never soft PreferCache encode.
 * LQIP is not produced here; it is a warm side-effect of tile work.
 */
bool scheduleDisplayPixels(const QString &path, int maxEdge);
/** Full / near-native via thumtoo request_full_pixels (≤ ~8192). */
bool scheduleFullPixels(const QString &path, int maxEdge = 0);

/** Allow a later schedulePixels for this path/edge after a shortfall delivery. */
void forgetPixelsSettled(const QString &path, int maxEdge);

/**
 * Bump thumtoo interest epoch and drop stale queued decode work (gallery scroll).
 * @return new epoch, or 0 if thumtoo unavailable.
 */
quint64 bumpInterestEpoch();
/** Drop queued EnsureTiles jobs for this path (thumtoo cancel_uri). */
int cancelTilesForPath(const QString &path);

/**
 * Hard reload: cancel queued work for @p path, clear process durable memos,
 * and forget the path in the durable Store (Client::purge_path — tiles + levels).
 * Store I/O runs on a worker; @p done is invoked on the GUI thread with
 * tiles_deleted (0 when thumtoo unavailable).
 */
void purgePathDurable(const QString &path, std::function<void(qint64 tilesDeleted)> done = {});

/**
 * Replace thumtoo interest snapshot (cancels stale work, schedules overview).
 * pathsNear = visible / high priority; pathsSpeculative = idle overscan.
 * @return new epoch or 0 if unavailable.
 */
/**
 * @param pathsPrimary  FocusFull candidates (tile pyramid); first is highest priority
 * @param pathsNear     Visible / selected secondary
 * @param pathsSpeculative  Idle overscan
 */
quint64 setInterest(const QStringList &pathsNear, const QStringList &pathsSpeculative,
                    int nearEdge, int speculativeEdge,
                    const QStringList &pathsPrimary = {}, int primaryEdge = 0);

/**
 * Image-mode focus: single Primary interest (overview + tile pyramid on thumtoo ≥168).
 * Replaces the whole interest snapshot — avoid during Gallery (use setInterest).
 */
quint64 setPrimaryInterest(const QString &path, int edge);

/**
 * Queue durable tile pyramid build (FocusFull) for @p path without wiping
 * Gallery interest. PreferCache can TileSynth to display edges only after tiles
 * exist; PreferCache alone does not generate them.
 */
bool scheduleTilePyramid(const QString &path);

/**
 * Product underlay climb: PreferCache TileSynth when durable tiles are known;
 * otherwise probe only (never FocusFull). Use scheduleTilePyramid for
 * explicit durable pyramid builds (Image primary / prepare).
 * @return true if TileSynth or pyramid work was queued / already pending.
 */
bool scheduleTileSynthOrPyramid(const QString &path, int maxEdge);

/**
 * Cache-only: at least one durable tile exists for @p path (legacy or Store).
 * Used for tile-band paint/tick and PreferCache shortcuts when a pyramid exists.
 */
bool hasDurableTiles(const QString &path);
/**
 * Memo only: true if hasDurableTiles already discovered yes; false if unknown
 * or negative. Never opens the Store — safe on GUI hot paths.
 */
bool hasDurableTilesKnown(const QString &path);

/** thumtoo URI for a session path ("" when it does not map). Cache-only. */
std::string thumtooUri(const QString &path);

/**
 * Document page behind a session path: MuPDF (PDF, Markdown, plain text
 * //page:N) or DjVu; nullopt for anything else (EPUB, images). GUI-safe
 * (string work only).
 */
struct DocumentPageRef {
    enum class Backend { MuPdf, Djvu };
    Backend backend = Backend::MuPdf;
    std::string uri;   /**< thumtoo page URI (parse on the rendering thread) */
    std::string file;  /**< document file */
    int page = 0;      /**< 1-based */
};
std::optional<DocumentPageRef> documentPageForPath(const QString &path);

/**
 * Worker: Store has_tile for @p path (deduped). Emits durableTilesReady on first yes.
 * GUI-safe. Use when filmstrip/Gallery need warm TileSynth but process memo is unknown.
 */
void scheduleDurableTilesDiscovery(const QString &path);
/**
 * Session open: warm durable-tile memos after the size gate settles.
 * Process sizes are hydrated by scheduleProbeBatch (Store get_size, then
 * request_size only for misses). Underlay still arrives via request_size /
 * tile LQIP into ImageCache. Safe on the GUI: schedules pool work and returns.
 */
void warmSessionOpenMemos(const QStringList &paths);

/**
 * Session Open / archive replace: drop process memos that must not outlive the
 * previous path set (durable-tile yes/no, min_scale, URI cache). Size/LQIP
 * memos stay; warmSessionOpenMemos refills for the new list.
 */
void clearSessionReplaceMemos();

/**
 * Finest durable pyramid scale for path (0 = full res). 0 if unknown / none.
 * Populates via hasDurableTiles discovery; process-memoized with positive hits.
 */
int durableTileMinScale(const QString &path);

/**
 * Soft band PreferCache (TileSynth when tiles exist, else **ephemeral soft
 * encode**). **Deprecated for product underlay** — Gallery/Image/filmstrip/
 * PathRaster soft-band must not call this; use tiles/TileSynth only.
 * Retained for rare non-product callers and thumtoo interop tests.
 */
bool scheduleSoftPixels(const QString &path, int maxEdge);

/** Human label for last ladderProvenance on this path (empty if unknown). */
QString lastPixelSourceLabel(const QString &path);

/** Worker pressure: "pending/inflight focus=N epoch=E" or empty. */
QString queueStatsLabel();

/** HUD: active/queued jobs split into cache retrieval vs file/archive encode. */
QString loadingBreakdownLabel();

/**
 * Prewarm **sizes** only for a session file list (thumtoo prepare / probe).
 * Does not schedule ladder encode — filmstrip, gallery window, and Image soft
 * preview request pixels on demand so large archives are not all extracted up
 * front. Skips paths marked Unsupported in the durable cache.
 */
/** Size-probe plain files missing from the durable index. Skips warm hits and
 *  compound refs; runs off the GUI thread. */
void preparePaths(const QStringList &paths);

/**
 * Session tile-cache summary (Store has_tile / coverage). Must not run on the
 * GUI thread — opens the Store like hasDurableTiles.
 */
struct TilePrepareStats {
    int total = 0;          ///< Paths considered (non-empty)
    int withTiles = 0;      ///< At least one durable tile known
    int missingTiles = 0;   ///< No durable tiles yet
    int unsupported = 0;    ///< Codec/container rejected
};

TilePrepareStats queryTilePrepareStats(const QStringList &paths);

/**
 * Build durable 256² tile pyramids for @p paths (thumtoo-prepare --tiles).
 * Runs off the GUI. @p minScale is the finest level to store (0 = full res;
 * higher = coarser only — less disk, less deep zoom). LQIP is filled
 * opportunistically by thumtoo during tile encode.
 *
 * @p onProgress may be called from a worker (marshal to GUI if needed).
 * @p cancel when non-null and true stops enqueueing further pyramids.
 */
using TilePrepareProgress =
    std::function<void(int done, int total, int ok, int skipped, int failed,
                       int lqipFilled)>;
void prepareTiles(const QStringList &paths, int minScale,
                  TilePrepareProgress onProgress = {},
                  std::atomic<bool> *cancel = nullptr);

/** Per-path Store coverage for Prepare Tile Cache UI (worker-safe). */
struct PathCacheCoverage {
    QString path;
    bool unsupported = false;
    bool hasTiles = false;
    bool hasLqip = false;      ///< Store ThumbHash/Handsum (not EMB)
    bool hasEmbedded = false;  ///< Store EXIF/PDF thumb kind
    /** Finest stored tile scale (0 = full res); -1 if no tiles. */
    int tileMinScale = -1;
};

/** Aggregate + per-path Store coverage for Prepare Tile Cache UI. */
struct CacheCoverageStats {
    int total = 0;
    int withTiles = 0;
    int withLqip = 0;
    int withEmbedded = 0;
    int tilesWithoutLqip = 0;
    int unsupported = 0;
    QVector<PathCacheCoverage> rows;
};
CacheCoverageStats scanCacheCoverage(const QStringList &paths);

/**
 * Pre-resolve session paths to thumtoo URIs on a worker thread so the first
 * Gallery setInterest does not pay path→URI conversion on the GUI
 * (GUI_THREAD_AUDIT G6). Safe no-op when thumtoo is unavailable.
 */
void warmUris(const QStringList &paths);

/**
 * Grid tiles (tilelod / Image zoom). Scale 0 = full res; +1 halves.
 */
struct TileCoord {
    int scale = 0;
    int x = 0;
    int y = 0;
};
/**
 * Cancel interactive cells (no longer demanded by any view). Each cancelled
 * cell still answers with FetchStatus::Cancelled (thumtoo cell contract).
 */
int cancelTileCells(const QString &path, const QVector<TileCoord> &coords);

/** One cell outcome from thumtoo request_tile_cells (rgba8 when Ok). */
struct TileCellResult {
    tilelod::FetchStatus status = tilelod::FetchStatus::Failed;
    std::optional<tilelod::TileBitmap> tile;
    std::string error;
};
using TileCellResultCallback = std::function<void(std::size_t index, TileCellResult)>;
/**
 * Async interactive tile cells via thumtoo request_tile_cells: exactly one
 * callback per index (Ok / Cancelled / Failed / Unavailable + reason), on the
 * GUI thread. Without thumtoo every cell fails immediately with a reason.
 */
void requestTileCells(const QString &path, const QVector<TileCoord> &coords,
                      TileCellResultCallback on_cell);
/** True when built with thumtoo and the client opened successfully. */
bool isAvailable();

/** User-facing load failure: "Could not load “name”: reason".
 *  Never QFileInfo::exists / filesystem probes — NFS blocks for seconds.
 *  Detail comes only from thumtoo::mupdf_last_error() (set by the failing open). */
QString formatLoadErrorMessage(const QString &sessionPathOrError);

/**
 * Expand an archive container to biltoo //archive: image refs using thumtoo's
 * durable TOC (cache-first, then refresh_archive_toc). Empty when thumtoo is
 * unavailable or the archive has no image members. Safe to call from a worker
 * thread. Sole archive expand path in biltoo.
 */
QStringList expandArchiveToImageRefs(const QString &archivePath,
                                     bool *fromStore = nullptr);

/** Expand a PDF into one session path per page (…//page:N, 1-based).
 * Page count is cache-first via thumtoo document_index (≥ 202). */
QStringList expandPdfToPageRefs(const QString &pdfPath);
/** Markdown file → //page:N (MuPDF ≥ 1.28; same pipeline as PDF). */
QStringList expandMarkdownToPageRefs(const QString &mdPath);
/** Plain text (.txt) → //page:N (MuPDF). */
QStringList expandPlainTextToPageRefs(const QString &txtPath);
/** path//text → pages (MuPDF magic txt). */
QStringList expandTextForceToPageRefs(const QString &pathWithTextPipe);

/** Expand a PDF into embedded Image XObjects (…//pdfimage:N, native resolution). */
QStringList expandPdfToImageRefs(const QString &pdfPath);

/** Expand an EPUB into session paths (…//epub:w,h,fs//page:N).
 * Page count cache-first (document_index; layout_key = epub params). */
QStringList expandEpubToPageRefs(const QString &epubPath);

/** Expand a DjVu into session paths (…//page:N). Page count cache-first. */
QStringList expandDjvuToPageRefs(const QString &djvuPath);

/**
 * Rasterize one PDF page to RGB888 QImage (long edge ≈ maxEdge).
 * Empty if thumtoo was built without Poppler or the page fails.
 */
QImage rasterizePdfPage(const QString &pdfPath, int page_1based, int maxEdge);

/**
 * Rasterize a session page ref (PDF / EPUB / DjVu) to RGB888.
 * Prefer this over rasterizePdfPage for generic //page: paths.
 */
QImage rasterizePageRef(const QString &sessionPath, int maxEdge);

/**
 * Extract one //archive: member into memory via thumtoo (size caps shared with
 * the cache worker). Empty for non-archive paths, extract failure, or when
 * thumtoo is not linked. Does not require the durable cache client to be open.
 * Sole archive-member byte path in biltoo.
 */
QByteArray readArchiveMemberBytes(const QString &archiveRefPath);


/** One text or link region in page space (see pageBounds for coordinate system). */
struct TextRegion {
    enum class Role { Text, Link };
    enum class Kind { Body, PageNumber, Header, Footer };
    QRectF bbox;  ///< page space; interpret with PageTextLayer::pageYUp
    Role role = Role::Text;
    Kind kind = Kind::Body;
    QString text;
    /** Link target: internal 1-based page (0 = none) and/or URI. */
    int linkPage = 0;
    QString linkUri;
    /**
     * MuPDF structured-text block index when known (−1 unknown).
     * Regions with the same blockId belong to one paragraph/column island —
     * do not treat them as one horizontal line with other blocks.
     */
    int blockId = -1;
};

enum class TextLayerSource : quint8 {
    Native = 0,
    Ocr = 1,
};

struct PageTextLayer {
    int page = 0;
    QString layoutKey;
    QRectF pageBounds;
    /**
     * When true, region bboxes use bottom-left origin (Y up) inside pageBounds.
     * When false, top-left (Y down). Set by thumtoo (TTL7+); see docs/OCR_COORDINATES.md.
     */
    bool pageYUp = false;  // MuPDF/PDF default; DjVu extractors set true
    TextLayerSource source = TextLayerSource::Native;
    QVector<TextRegion> regions;
};

struct OutlineItem {
    int level = 1;
    QString title;
    int page = 0;  ///< 1-based when known; 0 if URI-only
    QString uri;
};

struct DocumentOutline {
    QVector<OutlineItem> items;
};

/**
 * Cache-only text layer for a session page path (//page: / //epub:…//page:).
 * Empty when thumtoo is off, headers missing, or nothing stored yet.
 */
PageTextLayer cachedPageTextLayer(const QString &sessionPath);

/**
 * Extract (and cache in thumtoo when content_id known) text/link regions.
 * Source I/O — call off the GUI thread for large books when possible.
 * Empty layer if unsupported path or extract failure.
 */
PageTextLayer ensurePageTextLayer(const QString &sessionPath);

/**
 * OCR text layer (dual Store slot). Runs Tesseract when missing or @p force.
 * Empty if OCR unavailable or path not a page/image OCR can handle.
 */
PageTextLayer ensureOcrPageTextLayer(const QString &sessionPath,
                                     bool force = false,
                                     const QString &lang = QString(),
                                     const QRectF &pageCrop = {},
                                     int sourceDpi = 0);

/** Result of an OCR attempt with a specific failure reason for UI. */
struct OcrRunResult {
    enum class Status {
        Ok = 0,           /**< Non-empty regions (or valid empty page bounds only) */
        NoClient,         /**< thumtoo client not open */
        BadUri,           /**< path did not map to a thumtoo URI */
        Unavailable,      /**< Tesseract not linked / ocr_available() false */
        Failed,           /**< Rasterize or recognize returned null */
        EmptyText,        /**< OCR ran but produced no text regions */
    };
    Status status = Status::Failed;
    PageTextLayer layer;
    /**
     * Engine detail captured on the worker thread (thumtoo::ocr_last_error).
     * Must live on the result — not thread_local — so the GUI can show it.
     */
    QString detail;
    /** Localized one-line explanation (uses @c detail when set). */
    QString message() const;
};

/** True when thumtoo reports OCR (Tesseract) is available at runtime. */
bool ocrAvailable();

/**
 * Like ensureOcrPageTextLayer but returns @c OcrRunResult with a concrete status.
 * Prefer this for UI paths that need to explain failure.
 */
OcrRunResult runOcrPageTextLayer(const QString &sessionPath, bool force,
                                 const QString &lang = {},
                                 const QRectF &pageCrop = {},
                                 int sourceDpi = 0);

/**
 * OCR a host-prepared RGB image (appearance path).
 * Region bboxes use pageBounds (0,0)–(width,height) in image pixel space
 * (Y-down). Caller remaps into document page space when needed.
 * @p sourceDpi Tesseract source resolution; 0 → thumtoo estimates (300 for
 * pixel page boxes). Prefer 72 * native_w / pageBounds_width for document pages.
 */
OcrRunResult runOcrRgbImage(const QImage &image, const QString &lang = {},
                            int sourceDpi = 0);

/** Cache-only OCR layer (empty if never OCR'd). */
PageTextLayer cachedOcrPageTextLayer(const QString &sessionPath);

/** Cache-only document outline (TOC). Empty if not stored. */
DocumentOutline cachedDocumentOutline(const QString &sessionOrFilePath);

/** Extract + cache outline for a document (page ref or bare file path). */
DocumentOutline ensureDocumentOutline(const QString &sessionOrFilePath);

/**
 * Map a page-space rect into source image pixels (top-left, Y-down).
 * @p pageYUp must match PageTextLayer::pageYUp (or pageSpaceYUp fallback).
 * True: page origin lower-left (PDF/DjVu/EPUB document space).
 * False: page origin top-left (plain-image OCR space).
 */
QRectF pageRectToImageRect(const QRectF &pageRect, const QRectF &pageBounds,
                           const QSize &imageSize, bool pageYUp = true);
/** Inverse of pageRectToImageRect. */
QRectF imageRectToPageRect(const QRectF &imageRect, const QRectF &pageBounds,
                           const QSize &imageSize, bool pageYUp = true);

/**
 * Default page-space Y orientation when no layer is loaded yet.
 * Document page refs (PDF/DjVu/EPUB) → true; plain paths → false.
 */
bool pageSpaceYUpForPath(const QString &sessionPath);

/**
 * Local content-appearance state (XDG_STATE_HOME/thumtoo) — not the pixel cache.
 * Keyed by content sha256; never writes next to source files.
 * @see thumtoo docs/APPEARANCE.md
 */
struct StoredContentAppearance {
    bool contentHFlip = false;
    bool contentVFlip = false;
    int contentQuarterTurns = 0;
    bool hasCrop = false;
    QRect cropRect;
    QSize cropSourceSize;
    qreal cropRotation = 0.0;
    bool hasGrade = false;
    int gradeBrightness = 0;
    int gradeContrast = 0;
    int gradeSaturation = 0;
    int gradeHue = 0;
    int gradeGamma = 0;
    bool gradeInvert = false;
    /** Orient/crop only (not grade) — for load paths that skip grade-only XDG. */
    bool hasOrientContent() const
    {
        if (contentHFlip || contentVFlip || contentQuarterTurns != 0) {
            return true;
        }
        return hasCrop && !cropRect.isEmpty();
    }
    bool isIdentity() const
    {
        return !hasOrientContent() && !hasGrade;
    }
};

/** Load durable content appearance for path's content id (if any). */
bool loadContentAppearance(const QString &path, StoredContentAppearance *out);

/** Persist content appearance for path's content id (identity deletes the row). */
void saveContentAppearance(const QString &path, const StoredContentAppearance &app);

/** True when durable state has non-identity content appearance for @p path. */
bool hasContentAppearance(const QString &path);

/** Remove durable content appearance for @p path (identity). */
void clearContentAppearance(const QString &path);

/**
 * Durable page annotations keyed by the same locator id as content appearance
 * (XDG_STATE_HOME/biltoo/locator_appearance.sqlite3). Path-level, like orient/flip:
 * duplicates of the same file share marks. JSON is one Annotation::Page object
 * (sid is session-local and rewritten on load).
 */
bool loadLocatorAnnotationJson(const QString &path, QByteArray *jsonOut);
void saveLocatorAnnotationJson(const QString &path, const QByteArray &json);
void clearLocatorAnnotationJson(const QString &path);

} // namespace ThumtooCache

#endif
