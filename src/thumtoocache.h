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

/**
 * Thin biltoo façade over thumtoo::Client (durable size index + ladder).
 * Compile-time optional: without BILTOO_HAVE_THUMTOO every call is a no-op.
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
bool debugTracingEnabled();

/** Drop the client (join thumtoo worker). Safe to call more than once. */
void shutdown();

/**
 * Soft ladder long-edge targets (see thumtoo kMaxSoftLadderEdge = 512).
 * get_pixels / cachedLadderBytes return the largest soft level ≤ request.
 * Durable soft levels stop at kGalleryLadderEdge. Gallery display edges may be
 * higher: loadThumbnail shrink-on-decode at the on-screen ladder step (never
 * native full decode). docs/GALLERY_SOFT.md
 */
constexpr int kLadderEdges[] = {128, 256, 512, 1024, 2048};
constexpr int kFilmstripLadderEdge = 256;
constexpr int kGalleryLadderEdge = 512;  // durable soft max (thumtoo kMaxSoftLadderEdge)
/** FastBatch overview max (thumtoo kBatchMaxEdge) — Q1 JpegShrink / TileSynth. */
constexpr int kBatchOverviewEdge = 1024;
/** Highest ladder step used for display-edge snap (not a soft durable level). */
constexpr int kImageLadderEdge = 2048;

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

/** Largest ladder step strictly below @p edge, or 0 if none (below 128). */
inline int prevLadderEdge(int edge)
{
    int prev = 0;
    for (int e : kLadderEdges) {
        if (e >= edge) {
            break;
        }
        prev = e;
    }
    return prev;
}

/** Cache-only native size for a session path (file or //archive: ref). */
QSize cachedSize(const QString &path);

/**
 * Cache-only LQIP (Handsum/ThumbHash) as a small QImage.
 * Empty if missing from the durable index — does not schedule encode.
 */
QImage cachedLqipImage(const QString &path);

/**
 * Cache-only: thumtoo reported ContentStatus::Unsupported for this locator.
 * Callers should stop scheduling probes/pixels (Failed remains retryable).
 */
bool isUnsupported(const QString &path);

/**
 * Schedule a background size probe (and ladder) when missing.
 * Does not block; does not drain the queue on the GUI thread.
 * No-op when isUnsupported(path).
 */
void scheduleProbe(const QString &path);

/**
 * Cache-only ladder payload (usually JPEG-XL) with long edge <= maxEdge.
 * Empty if thumtoo has no level yet. Caller decodes (e.g. via libvips).
 */
QByteArray cachedLadderBytes(const QString &path, int maxEdge);

/**
 * Ensure ladder level exists for maxEdge (probe/encode in thumtoo worker).
 * On success (GUI thread): Bridge::ladderReady (+ ladderProvenance when known).
 * Uses thumtoo request_raster(SoftOnly) when THUMTOO_API_REQUEST_RASTER is set.
 * No-op when isUnsupported(path).
 */
/** @return false if skipped (already in-flight, settled success, or unsupported). */
bool schedulePixels(const QString &path, int maxEdge);
/** True if SoftOnly for path#edge is queued or actively decoding. */
bool isPixelsPending(const QString &path, int maxEdge);

/**
 * FastBatch overview: request_overview_pixels (≤ kBatchOverviewEdge).
 * Prefer when display edge is above soft max but at or below batch max.
 */
bool scheduleOverviewPixels(const QString &path, int maxEdge);

/** Allow a later schedulePixels for this path/edge after a shortfall delivery. */
void forgetPixelsSettled(const QString &path, int maxEdge);

/**
 * Bump thumtoo interest epoch and drop stale queued decode work (gallery scroll).
 * @return new epoch, or 0 if thumtoo unavailable.
 */
quint64 bumpInterestEpoch();
/** Drop all queued thumtoo jobs (pixels/size/tiles); in-flight may still finish. */
int cancelPendingThumtooWork();

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
 */
quint64 setPrimaryInterest(const QString &path, int edge);

/** Human label for last ladderProvenance on this path (empty if unknown). */
QString lastPixelSourceLabel(const QString &path);

/** Worker pressure: "pending/inflight focus=N epoch=E" or empty. */
QString queueStatsLabel();

/** True when setInterest owns overview scheduling (no scheduleOverviewPixels). */
bool interestOwnsOverview();

/** True while a request_pixels for this path/edge is queued or running. */
bool isPixelsInflight(const QString &path, int maxEdge);


/**
 * Prewarm **sizes** only for a session file list (thumtoo prepare / probe).
 * Does not schedule ladder encode — filmstrip, gallery window, and Image soft
 * preview request pixels on demand so large archives are not all extracted up
 * front. Skips paths marked Unsupported in the durable cache.
 */
void preparePaths(const QStringList &paths);

/**
 * Pre-resolve session paths to thumtoo URIs on a worker thread so the first
 * Gallery setInterest does not pay path→URI conversion on the GUI
 * (GUI_THREAD_AUDIT G6). Safe no-op when thumtoo is unavailable.
 */
void warmUris(const QStringList &paths);

/** True when built with thumtoo and the client opened successfully. */
bool isAvailable();

/**
 * Expand an archive container to biltoo //archive: image refs using thumtoo's
 * durable TOC (cache-first, then refresh_archive_toc). Empty when thumtoo is
 * unavailable or the archive has no image members. Safe to call from a worker
 * thread. Sole archive expand path in biltoo.
 */
QStringList expandArchiveToImageRefs(const QString &archivePath);

/** Expand a PDF into one session path per page (…//page:N, 1-based). */
QStringList expandPdfToPageRefs(const QString &pdfPath);

/** Expand a PDF into embedded Image XObjects (…//pdfimage:N, native resolution). */
QStringList expandPdfToImageRefs(const QString &pdfPath);

/** Expand an EPUB into session paths (…//epub:w,h,fs//page:N) via thumtoo default layout. */
QStringList expandEpubToPageRefs(const QString &epubPath);

/** Expand a DjVu into session paths (…//page:N). */
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
    QRectF bbox;  ///< page space (PDF/EPUB: points Y-up; DjVu: pixels Y-up)
    Role role = Role::Text;
    QString text;
    /** Link target: internal 1-based page (0 = none) and/or URI. */
    int linkPage = 0;
    QString linkUri;
};

struct PageTextLayer {
    int page = 0;
    QString layoutKey;
    QRectF pageBounds;
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

/** Cache-only document outline (TOC). Empty if not stored. */
DocumentOutline cachedDocumentOutline(const QString &sessionOrFilePath);

/** Extract + cache outline for a document (page ref or bare file path). */
DocumentOutline ensureDocumentOutline(const QString &sessionOrFilePath);

/**
 * Map a page-space rect into image-pixel space (top-left origin) using pageBounds.
 * @p pageYUp true for DjVu (origin bottom-left, Y up). False for PDF and EPUB
 * where MuPDF text already matches the top-left Y-down raster.
 */
QRectF pageRectToImageRect(const QRectF &pageRect, const QRectF &pageBounds,
                           const QSize &imageSize, bool pageYUp = true);

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
    bool isIdentity() const;
};

/** sha256:<hex> for a regular local file path; empty if unavailable / non-file. */
QString contentIdForPath(const QString &path);

/** Load durable content appearance for path's content id (if any). */
bool loadContentAppearance(const QString &path, StoredContentAppearance *out);

/** Persist content appearance for path's content id (identity deletes the row). */
void saveContentAppearance(const QString &path, const StoredContentAppearance &app);

/** True when durable state has non-identity content appearance for @p path. */
bool hasContentAppearance(const QString &path);

/** Remove durable content appearance for @p path (identity). */
void clearContentAppearance(const QString &path);

} // namespace ThumtooCache

#endif
