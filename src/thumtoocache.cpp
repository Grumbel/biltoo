// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"

#include "archivepath.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QSet>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>

#ifdef BILTOO_HAVE_THUMTOO
#include "thumtoo/archive.hpp"
#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
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

std::string absPathStd(const QString &p)
{
    QFileInfo fi(p);
    const QString can = fi.canonicalFilePath();
    const QString abs = can.isEmpty() ? fi.absoluteFilePath() : can;
    return abs.toStdString();
}

std::string toThumtooUri(const QString &path)
{
    if (ArchivePath::isArchiveRef(path)) {
        const ArchivePath::Ref ref = ArchivePath::parse(path);
        if (!ref.valid) {
            return {};
        }
        return thumtoo::archive_uri(absPathStd(ref.archivePath),
                                    ref.memberPath.toStdString());
    }
    const QFileInfo fi(path);
    if (!fi.exists()) {
        return {};
    }
    return thumtoo::file_uri_from_path(absPathStd(path));
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
 * Archive member image filter aligned with ArchiveReader::entryIsImage /
 * ImageLoader::imageSuffixes base set (no imageloader include — circular).
 * Broader than thumtoo::is_likely_image_member_path so mixed HEIC/AVIF/…
 * archives are not truncated when TOC is a cache hit.
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
        return QSize(sz->width, sz->height);
    }
#else
    Q_UNUSED(path);
#endif
    return {};
}

void scheduleProbe(const QString &path)
{
#ifdef BILTOO_HAVE_THUMTOO
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
    if (maxEdge <= 0) {
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
        if (ArchivePath::isArchiveRef(p)) {
            scheduleProbe(p);
            schedulePixels(p, 512);
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
    // After each size probe, also request a preview ladder and notify UI.
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
        schedulePixels(path, 512);
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
