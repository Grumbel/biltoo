// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMTOOCACHE_H
#define THUMTOOCACHE_H

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

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
 * Ladder long-edge targets (subset of thumtoo::kLadderEdges). Prefer the
 * largest cached level ≤ request; do not upscale in the client.
 */
constexpr int kFilmstripLadderEdge = 256;
constexpr int kGalleryLadderEdge = 512;
constexpr int kImageLadderEdge = 1024;

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

/**
 * Extract one //archive: member into memory via thumtoo (size caps shared with
 * the cache worker). Empty for non-archive paths, extract failure, or when
 * thumtoo is not linked. Does not require the durable cache client to be open.
 * Sole archive-member byte path in biltoo.
 */
QByteArray readArchiveMemberBytes(const QString &archiveRefPath);

} // namespace ThumtooCache

#endif
