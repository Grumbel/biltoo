// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"

#include "archivepath.h"
#include "pagepath.h"
#include <cstring>

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QSet>
#include <QThreadPool>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>

#ifdef BILTOO_HAVE_THUMTOO
#include "thumtoo/archive.hpp"
#if defined(BILTOO_HAVE_THUMTOO) && __has_include("thumtoo/pdf.hpp")
#include "thumtoo/pdf.hpp"
#define BILTOO_HAVE_THUMTOO_PDF 1
#endif
#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/status.hpp"
#include "thumtoo/uri.hpp"

#include <condition_variable>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#endif

namespace ThumtooCache {
namespace {

#ifdef BILTOO_HAVE_THUMTOO

std::mutex g_mu;
std::unique_ptr<thumtoo::Client> g_client;
bool g_inited = false;

// Used by scheduleBackgroundRevalidate before the definition below.
thumtoo::Client *clientUnlocked();

// Process-local archive member byte cache (not durable — avoids re-extract in
// the same process when probe + thumb + full decode hit the same member).
constexpr int kMemberLruMaxEntries = 48;
constexpr qint64 kMemberLruMaxBytes = 64 * 1024 * 1024;

struct MemberLru {
    std::mutex mu;
    std::list<QString> order; // front = most recent
    std::unordered_map<std::string, std::pair<QByteArray, std::list<QString>::iterator>> map;
    qint64 totalBytes = 0;
};

MemberLru g_memberLru;

void memberLruPut(const QString &key, const QByteArray &bytes)
{
    if (key.isEmpty() || bytes.isEmpty()) {
        return;
    }
    const std::string k = key.toStdString();
    std::lock_guard lock(g_memberLru.mu);
    auto it = g_memberLru.map.find(k);
    if (it != g_memberLru.map.end()) {
        g_memberLru.totalBytes -= it->second.first.size();
        g_memberLru.order.erase(it->second.second);
        g_memberLru.map.erase(it);
    }
    g_memberLru.order.push_front(key);
    g_memberLru.map.emplace(k, std::make_pair(bytes, g_memberLru.order.begin()));
    g_memberLru.totalBytes += bytes.size();
    while (!g_memberLru.map.empty()
           && (int(g_memberLru.map.size()) > kMemberLruMaxEntries
               || g_memberLru.totalBytes > kMemberLruMaxBytes)) {
        const QString drop = g_memberLru.order.back();
        g_memberLru.order.pop_back();
        auto dit = g_memberLru.map.find(drop.toStdString());
        if (dit != g_memberLru.map.end()) {
            g_memberLru.totalBytes -= dit->second.first.size();
            g_memberLru.map.erase(dit);
        }
    }
}

QByteArray memberLruGet(const QString &key)
{
    if (key.isEmpty()) {
        return {};
    }
    const std::string k = key.toStdString();
    std::lock_guard lock(g_memberLru.mu);
    auto it = g_memberLru.map.find(k);
    if (it == g_memberLru.map.end()) {
        return {};
    }
    g_memberLru.order.erase(it->second.second);
    g_memberLru.order.push_front(key);
    it->second.second = g_memberLru.order.begin();
    return it->second.first;
}

// Coalesce concurrent extracts for one archive container.
struct ArchiveExtractBatch {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::string> pending;
    std::unordered_map<std::string, QByteArray> done;
    int waiters = 0;
    bool running = false;
};

std::mutex g_batchMu;
std::unordered_map<std::string, std::shared_ptr<ArchiveExtractBatch>> g_batches;

std::shared_ptr<ArchiveExtractBatch> batchForArchive(const std::string &archiveAbs)
{
    std::lock_guard lock(g_batchMu);
    auto it = g_batches.find(archiveAbs);
    if (it != g_batches.end()) {
        return it->second;
    }
    auto b = std::make_shared<ArchiveExtractBatch>();
    g_batches.emplace(archiveAbs, b);
    return b;
}

void releaseBatchIfIdle(const std::string &archiveAbs, const std::shared_ptr<ArchiveExtractBatch> &b)
{
    std::lock_guard lock(g_batchMu);
    if (b->waiters == 0 && !b->running && b->pending.empty()) {
        g_batches.erase(archiveAbs);
    }
}

/**
 * Path for URI building without mandatory exists()/canonical().
 * Session paths are almost always absolute — avoid realpath on every hit.
 */
std::string absPathFast(const QString &p)
{
    QFileInfo fi(p);
    if (fi.isAbsolute()) {
        // absoluteFilePath for absolute input is path cleanup, not a full walk.
        return fi.absoluteFilePath().toStdString();
    }
    const QString can = fi.canonicalFilePath();
    const QString abs = can.isEmpty() ? fi.absoluteFilePath() : can;
    return abs.toStdString();
}

/** Legacy name used by expand/register paths that still want a stable abs. */
std::string absPathStd(const QString &p)
{
    return absPathFast(p);
}

std::mutex g_uriMu;
std::unordered_map<std::string, std::string> g_uriBySessionPath;

std::string resolveUriUncached(const QString &path)
{
    if (PagePath::isPageRef(path)) {
        const PagePath::Ref ref = PagePath::parse(path);
        if (!ref.valid) {
            return {};
        }
#if defined(BILTOO_HAVE_THUMTOO_PDF)
        return thumtoo::pdf_page_uri(absPathFast(ref.pdfPath), ref.page);
#else
        // Match thumtoo Location form even when headers predate PDF support.
        return thumtoo::file_uri_from_path(absPathFast(ref.pdfPath))
               + "//page:" + std::to_string(ref.page);
#endif
    }
    if (ArchivePath::isArchiveRef(path)) {
        const ArchivePath::Ref ref = ArchivePath::parse(path);
        if (!ref.valid) {
            return {};
        }
        return thumtoo::archive_uri(absPathFast(ref.archivePath),
                                    ref.memberPath.toStdString());
    }
    if (path.isEmpty()) {
        return {};
    }
    // Do not require exists() — missing files simply miss in the durable index.
    return thumtoo::file_uri_from_path(absPathFast(path));
}

std::string toThumtooUri(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const std::string key = path.toStdString();
    {
        std::lock_guard lock(g_uriMu);
        const auto it = g_uriBySessionPath.find(key);
        if (it != g_uriBySessionPath.end()) {
            return it->second;
        }
    }
    std::string uri = resolveUriUncached(path);
    if (!uri.empty()) {
        std::lock_guard lock(g_uriMu);
        g_uriBySessionPath.emplace(key, uri);
    }
    return uri;
}

// Rate-limit background mtime checks (per thumtoo URI).
std::mutex g_revalMu;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_revalLast;
constexpr auto kRevalidateMinInterval = std::chrono::seconds(2);

std::optional<std::int64_t> fileMtimeFingerprint(const std::filesystem::path &p)
{
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(ft.time_since_epoch().count());
}

std::optional<std::int64_t> fileSizeBytes(const std::filesystem::path &p)
{
    std::error_code ec;
    const auto sz = std::filesystem::file_size(p, ec);
    if (ec) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(sz);
}

/**
 * After a durable-cache hit: compare source fingerprint to locator row later.
 * Does not block the caller. On mismatch, scheduleProbe(path).
 */
void scheduleBackgroundRevalidate(const QString &path, const std::string &uri)
{
    if (path.isEmpty() || uri.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(g_revalMu);
        const auto it = g_revalLast.find(uri);
        if (it != g_revalLast.end() && (now - it->second) < kRevalidateMinInterval) {
            return;
        }
        g_revalLast[uri] = now;
    }
    const QString pathCopy = path;
    const std::string uriCopy = uri;
    QThreadPool::globalInstance()->start([pathCopy, uriCopy]() {
        init();
        thumtoo::Client *c = nullptr;
        {
            std::lock_guard lock(g_mu);
            c = clientUnlocked();
        }
        if (!c) {
            return;
        }
        const auto loc = c->db().find_locator(uriCopy);
        if (!loc || !loc->outer_path || loc->outer_path->empty()) {
            return;
        }
        const std::filesystem::path outer(*loc->outer_path);
        const auto mtime = fileMtimeFingerprint(outer);
        const auto size = fileSizeBytes(outer);
        // No stored fingerprint: nothing to compare; leave cache as-is.
        if (!loc->mtime_ns && !loc->size) {
            return;
        }
        bool mismatch = false;
        if (loc->mtime_ns && mtime && *loc->mtime_ns != *mtime) {
            mismatch = true;
        }
        if (loc->size && size && *loc->size != *size) {
            mismatch = true;
        }
        // Source vanished.
        if (!mtime && !size) {
            mismatch = true;
        }
        if (!mismatch) {
            return;
        }
        // Refresh durable rows (probe worker re-reads source).
        scheduleProbe(pathCopy);
        // Also nudge a gallery-level ladder rebuild when size changes.
        schedulePixels(pathCopy, kGalleryLadderEdge);
    });
}

thumtoo::Client *clientUnlocked()
{
    return g_client.get();
}

thumtoo::Executor qtExecutor()
{
    return thumtoo::Executor{[](std::function<void()> fn) {
        QCoreApplication *app = QCoreApplication::instance();
        if (!app) {
            fn();
            return;
        }
        QMetaObject::invokeMethod(
            app,
            [fn = std::move(fn)]() mutable { fn(); },
            Qt::QueuedConnection);
    }};
}

std::filesystem::path defaultCacheRoot()
{
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "thumtoo";
    }
    if (const char *home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".cache" / "thumtoo";
    }
    return std::filesystem::path(".cache") / "thumtoo";
}

/**
 * Archive member image filter aligned with ImageLoader::imageSuffixes base
 * set (no imageloader include — circular). Broader than thumtoo's short
 * is_likely_image_member_path so mixed HEIC/AVIF/… archives are not truncated.
 */
bool isBiltooImageMemberPath(const QString &memberPath)
{
    if (memberPath.isEmpty() || memberPath.endsWith(QLatin1Char('/'))) {
        return false;
    }
    if (memberPath.contains(QLatin1String("__MACOSX/"))) {
        return false;
    }
    const int slash = memberPath.lastIndexOf(QLatin1Char('/'));
    const QString base = (slash >= 0) ? memberPath.mid(slash + 1) : memberPath;
    if (base.isEmpty() || base.startsWith(QLatin1Char('.'))) {
        return false;
    }
    const QString suffix = QFileInfo(base).suffix().toLower();
    if (suffix.isEmpty()) {
        return false;
    }
    static const QSet<QString> kSuffixes = {
        QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("bmp"),  QStringLiteral("gif"),  QStringLiteral("webp"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("svg"),
        QStringLiteral("xpm"),  QStringLiteral("pbm"),  QStringLiteral("pgm"),
        QStringLiteral("ppm"),  QStringLiteral("ico"),  QStringLiteral("xbm"),
        QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"),
        QStringLiteral("jxl"),  QStringLiteral("jp2"),  QStringLiteral("j2k"),
        QStringLiteral("exr"),  QStringLiteral("hdr"),  QStringLiteral("pic"),
        QStringLiteral("tga"),  QStringLiteral("pcx"),  QStringLiteral("psd"),
        QStringLiteral("dds"),  QStringLiteral("fits"), QStringLiteral("fit"),
        QStringLiteral("vips"), QStringLiteral("xcf"),  QStringLiteral("kra"),
        QStringLiteral("ora"),
    };
    return kSuffixes.contains(suffix);
}

#endif

Bridge *g_bridge = nullptr;

} // namespace

Bridge *bridge()
{
    if (!g_bridge) {
        g_bridge = new Bridge(QCoreApplication::instance());
    }
    return g_bridge;
}

void init()
{
#ifdef BILTOO_HAVE_THUMTOO
    std::lock_guard lock(g_mu);
    if (g_inited) {
        return;
    }
    g_inited = true;
    try {
        thumtoo::image_library_init();
        g_client = thumtoo::Client::open(defaultCacheRoot(), qtExecutor());
    } catch (...) {
        g_client.reset();
    }
#endif
}

void shutdown()
{
#ifdef BILTOO_HAVE_THUMTOO
    std::lock_guard lock(g_mu);
    g_client.reset();
    g_inited = false;
#endif
}

QSize cachedSize(const QString &path)
{
#ifdef BILTOO_HAVE_THUMTOO
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return {};
    }
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return {};
    }
    if (auto sz = c->get_size(uri)) {
        scheduleBackgroundRevalidate(path, uri);
        return QSize(sz->width, sz->height);
    }
#else
    Q_UNUSED(path);
#endif
    return {};
}

bool isUnsupported(const QString &path)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (path.isEmpty()) {
        return false;
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return false;
    }
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return false;
    }
    if (auto meta = c->get_meta(uri)) {
        return meta->status == thumtoo::ContentStatus::Unsupported;
    }
#else
    Q_UNUSED(path);
#endif
    return false;
}

void scheduleProbe(const QString &path)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (isUnsupported(path)) {
        return;
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return;
    }
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return;
    }
    const QString pathCopy = path;
    c->request_size(uri, [pathCopy](std::string, std::optional<thumtoo::Size> sz) {
        if (!sz) {
            return;
        }
        emit bridge()->sizeReady(pathCopy, QSize(sz->width, sz->height));
    });
#else
    Q_UNUSED(path);
#endif
}

QByteArray cachedLadderBytes(const QString &path, int maxEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (maxEdge <= 0) {
        return {};
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return {};
    }
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return {};
    }
    if (auto px = c->get_pixels(uri, maxEdge)) {
        if (px->bytes.empty()) {
            return {};
        }
        scheduleBackgroundRevalidate(path, uri);
        return QByteArray(reinterpret_cast<const char *>(px->bytes.data()),
                          int(px->bytes.size()));
    }
#else
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
#endif
    return {};
}

void schedulePixels(const QString &path, int maxEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (maxEdge <= 0 || isUnsupported(path)) {
        return;
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return;
    }
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return;
    }
    const QString pathCopy = path;
    const int edge = maxEdge;
    c->request_pixels(
        uri, maxEdge,
        [pathCopy, edge](std::string, int, std::optional<thumtoo::PixelLevel> px) {
            if (!px || px->bytes.empty()) {
                return;
            }
            // Decode happens in ThumbnailBar/ImageLoader (vips) on ladderReady.
            emit bridge()->ladderReady(pathCopy, edge);
        });
#else
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
#endif
}

void preparePaths(const QStringList &paths)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (paths.isEmpty()) {
        return;
    }
    init();
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return;
    }
    std::vector<std::filesystem::path> fsPaths;
    fsPaths.reserve(size_t(paths.size()));
    // Keep path strings for callbacks (prepare_paths only returns URIs).
    QStringList plainPaths;
    for (const QString &p : paths) {
        if (isUnsupported(p)) {
            continue;
        }
        if (ArchivePath::isArchiveRef(p)) {
            // Skip: probing every member on open is extract(+hash)×N before any
            // thumb. Visible filmstrip/gallery/Image schedule work on demand.
            continue;
        }
        const QFileInfo fi(p);
        if (!fi.exists()) {
            continue;
        }
        fsPaths.emplace_back(absPathStd(p));
        plainPaths.append(p);
    }
    if (fsPaths.empty()) {
        return;
    }
    // Size probes only; do not encode ladders for the whole session here.
    c->prepare_paths(fsPaths, [plainPaths](std::string uri, std::optional<thumtoo::Size> sz) {
        QString path;
        for (const QString &p : plainPaths) {
            if (toThumtooUri(p) == uri) {
                path = p;
                break;
            }
        }
        if (path.isEmpty()) {
            return;
        }
        if (sz) {
            emit bridge()->sizeReady(path, QSize(sz->width, sz->height));
        }
    });
#else
    Q_UNUSED(paths);
#endif
}

bool isAvailable()
{
#ifdef BILTOO_HAVE_THUMTOO
    init();
    std::lock_guard lock(g_mu);
    return clientUnlocked() != nullptr;
#else
    return false;
#endif
}

QStringList expandArchiveToImageRefs(const QString &archivePath)
{
    QStringList out;
#ifdef BILTOO_HAVE_THUMTOO
    if (archivePath.isEmpty()) {
        return out;
    }
    const QFileInfo fi(archivePath);
    if (!fi.exists() || !fi.isFile()) {
        return out;
    }
    init();
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return out;
    }

    const std::filesystem::path abs = absPathStd(archivePath);
    const std::string archiveUri = thumtoo::archive_uri(abs);

    auto entries = c->get_archive_entries(archiveUri);
    if (entries.empty()) {
        // Source I/O + durable store; callers use this from expand workers.
        entries = c->refresh_archive_toc(abs);
    }
    if (entries.empty()) {
        return out;
    }

    const QString archiveAbs = QString::fromStdString(abs.string());
    out.reserve(int(entries.size()));
    for (const auto &entry : entries) {
        if (thumtoo::is_unsafe_archive_member_path(entry.member_path)) {
            continue;
        }
        const QString member = QString::fromStdString(entry.member_path);
        if (!isBiltooImageMemberPath(member)) {
            continue;
        }
        const QString ref = ArchivePath::makeRef(archiveAbs, member);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#else
    Q_UNUSED(archivePath);
#endif
    return out;
}


QStringList expandPdfToPageRefs(const QString &pdfPath)
{
    QStringList out;
#if defined(BILTOO_HAVE_THUMTOO_PDF)
    if (pdfPath.isEmpty()) {
        return out;
    }
    const std::filesystem::path abs = absPathStd(pdfPath);
    if (!thumtoo::is_likely_pdf_path(abs)) {
        return out;
    }
    const auto count = thumtoo::pdf_page_count(abs);
    if (!count || *count <= 0) {
        return out;
    }
    const QString pdfAbs = QString::fromStdString(abs.string());
    out.reserve(*count);
    for (int page = 1; page <= *count; ++page) {
        const QString ref = PagePath::makeRef(pdfAbs, page);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#else
    Q_UNUSED(pdfPath);
#endif
    return out;
}

QImage rasterizePdfPage(const QString &pdfPath, int page_1based, int maxEdge)
{
#if defined(BILTOO_HAVE_THUMTOO_PDF)
    if (pdfPath.isEmpty() || page_1based < 1) {
        return {};
    }
    const std::filesystem::path abs = absPathStd(pdfPath);
    const int edge = maxEdge > 0 ? maxEdge : 2048;
    auto raster = thumtoo::pdf_rasterize_page(abs, page_1based, edge);
    if (!raster || raster->rgb.empty() || raster->width <= 0 || raster->height <= 0) {
        return {};
    }
    QImage img(raster->width, raster->height, QImage::Format_RGB888);
    for (int y = 0; y < raster->height; ++y) {
        memcpy(img.scanLine(y),
               raster->rgb.data()
                   + static_cast<size_t>(y) * static_cast<size_t>(raster->width) * 3u,
               static_cast<size_t>(raster->width) * 3u);
    }
    return img.copy();
#else
    Q_UNUSED(pdfPath);
    Q_UNUSED(page_1based);
    Q_UNUSED(maxEdge);
    return {};
#endif
}

QByteArray readArchiveMemberBytes(const QString &archiveRefPath)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (!ArchivePath::isArchiveRef(archiveRefPath)) {
        return {};
    }
    const ArchivePath::Ref ref = ArchivePath::parse(archiveRefPath);
    if (!ref.valid) {
        return {};
    }
    // Canonical session path key for LRU (stable across relative spellings).
    const QString cacheKey = ArchivePath::makeRef(
        QString::fromStdString(absPathStd(ref.archivePath)), ref.memberPath);
    if (cacheKey.isEmpty()) {
        return {};
    }
    if (const QByteArray hit = memberLruGet(cacheKey); !hit.isEmpty()) {
        return hit;
    }

    const std::filesystem::path abs = absPathStd(ref.archivePath);
    const std::string archiveKey = abs.string();
    const std::string member = ref.memberPath.toStdString();
    if (member.empty()) {
        return {};
    }

    auto batch = batchForArchive(archiveKey);
    QByteArray result;
    {
        std::unique_lock lock(batch->mu);
        ++batch->waiters;
        // Already extracted by a concurrent batch?
        auto dit = batch->done.find(member);
        if (dit != batch->done.end()) {
            result = dit->second;
            --batch->waiters;
            if (batch->waiters == 0) {
                batch->done.clear();
            }
            lock.unlock();
            releaseBatchIfIdle(archiveKey, batch);
            if (!result.isEmpty()) {
                memberLruPut(cacheKey, result);
            }
            return result;
        }

        batch->pending.push_back(member);
        if (!batch->running) {
            batch->running = true;
            // Snapshot pending under lock, then extract outside the lock.
            std::vector<std::string> toExtract = batch->pending;
            batch->pending.clear();
            lock.unlock();

            auto extracted = thumtoo::extract_archive_members(abs, toExtract);

            lock.lock();
            for (const auto &m : toExtract) {
                QByteArray ba;
                auto eit = extracted.find(m);
                if (eit != extracted.end() && !eit->second.empty()) {
                    ba = QByteArray(reinterpret_cast<const char *>(eit->second.data()),
                                    int(eit->second.size()));
                }
                batch->done[m] = ba;
            }
            // Drain any members queued while we were extracting.
            while (!batch->pending.empty()) {
                std::vector<std::string> more = batch->pending;
                batch->pending.clear();
                lock.unlock();
                auto moreEx = thumtoo::extract_archive_members(abs, more);
                lock.lock();
                for (const auto &m : more) {
                    QByteArray ba;
                    auto eit = moreEx.find(m);
                    if (eit != moreEx.end() && !eit->second.empty()) {
                        ba = QByteArray(reinterpret_cast<const char *>(eit->second.data()),
                                        int(eit->second.size()));
                    }
                    batch->done[m] = ba;
                }
            }
            batch->running = false;
            batch->cv.notify_all();
        } else {
            batch->cv.wait(lock, [&] {
                return batch->done.find(member) != batch->done.end();
            });
        }

        auto fit = batch->done.find(member);
        if (fit != batch->done.end()) {
            result = fit->second;
        }
        --batch->waiters;
        if (batch->waiters == 0) {
            batch->done.clear();
        }
    }
    releaseBatchIfIdle(archiveKey, batch);
    if (!result.isEmpty()) {
        memberLruPut(cacheKey, result);
    }
    return result;
#else
    Q_UNUSED(archiveRefPath);
    return {};
#endif
}

} // namespace ThumtooCache
