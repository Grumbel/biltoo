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
    void ladderReady(const QString &path, int maxEdge);
};

/** Process-wide notifier (created on first use). */
Bridge *bridge();

/**
 * Open the default XDG cache client once (safe to call repeatedly).
 * Prefer calling after QApplication exists so callbacks can use the Qt loop.
 */
void init();

/** Drop the client (join thumtoo worker). Safe to call more than once. */
void shutdown();

/**
 * Ladder long-edge targets (match thumtoo::kLadderEdges).
 * get_pixels / cachedLadderBytes return the largest level with edge ≤ request.
 * Request the ceiling step so Gallery can scale a slightly larger thumb down.
 */
constexpr int kLadderEdges[] = {128, 256, 512, 1024, 2048};
constexpr int kFilmstripLadderEdge = 256;
constexpr int kGalleryLadderEdge = 512;  // fallback when cell size unknown
constexpr int kImageLadderEdge = 1024;

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

/** Cache-only native size for a session path (file or //archive: ref). */
QSize cachedSize(const QString &path);

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
 * On success (GUI thread): Bridge::ladderReady (decode via ImageLoader).
 * No-op when isUnsupported(path).
 */
void schedulePixels(const QString &path, int maxEdge);

/**
 * Prewarm **sizes** only for a session file list (thumtoo prepare / probe).
 * Does not schedule ladder encode — filmstrip, gallery window, and Image soft
 * preview request pixels on demand so large archives are not all extracted up
 * front. Skips paths marked Unsupported in the durable cache.
 */
void preparePaths(const QStringList &paths);

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
 * Map a page-space rect into image-pixel space (top-left origin) using pageBounds.
 * @p pageYUp true for PDF/DjVu (origin bottom-left, Y up); false for EPUB
 * (origin top-left, Y down — same as the raster).
 */
QRectF pageRectToImageRect(const QRectF &pageRect, const QRectF &pageBounds,
                           const QSize &imageSize, bool pageYUp = true);

} // namespace ThumtooCache

#endif
