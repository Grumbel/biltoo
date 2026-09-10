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
#include <QDebug>
#include <QThreadPool>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#ifdef BILTOO_HAVE_THUMTOO
#include "thumtoo/archive.hpp"
#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/status.hpp"
#include "thumtoo/uri.hpp"
// PDF helpers live in pdf.hpp (Poppler). Prefer free functions so biltoo works
// with thumtoo tips that have PDF but not yet Client::pdf_* wrappers.
#if __has_include("thumtoo/pdf.hpp")
#include "thumtoo/pdf.hpp"
#define BILTOO_HAVE_THUMTOO_PDF 1
#endif
#if __has_include("thumtoo/epub.hpp")
#include "thumtoo/epub.hpp"
#define BILTOO_HAVE_THUMTOO_EPUB 1
#endif
#if __has_include("thumtoo/djvu.hpp")
#include "thumtoo/djvu.hpp"
#define BILTOO_HAVE_THUMTOO_DJVU 1
#endif
#if __has_include("thumtoo/expand.hpp")
#include "thumtoo/expand.hpp"
#define BILTOO_HAVE_THUMTOO_EXPAND 1
#endif
#if __has_include("thumtoo/text.hpp")
#include "thumtoo/text.hpp"
#define BILTOO_HAVE_THUMTOO_TEXT 1
#endif

#include <condition_variable>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#endif

namespace ThumtooCache {
namespace {
QSet<QString> g_pixelsInflight;

/** THUMTOO_DEBUG or BILTOO_THUMTOO_DEBUG = non-empty non-0 → stderr traces. */
bool envFlagOn(const char *name)
{
    const char *e = std::getenv(name);
    if (!e || !e[0] || e[0] == '0') {
        return false;
    }
    if (e[0] == 'f' || e[0] == 'F' || e[0] == 'n' || e[0] == 'N') {
        return false;
    }
    return true;
}

bool g_forceDebug = false;

bool thumtooDebugEnabled()
{
    return g_forceDebug || envFlagOn("THUMTOO_DEBUG")
        || envFlagOn("BILTOO_THUMTOO_DEBUG");
}

std::FILE *thumtooDebugFile()
{
    static std::FILE *fp = []() -> std::FILE * {
        const char *xdg = std::getenv("XDG_CACHE_HOME");
        const char *home = std::getenv("HOME");
        std::string path;
        if (xdg && xdg[0]) {
            path = std::string(xdg) + "/biltoo";
        } else if (home && home[0]) {
            path = std::string(home) + "/.cache/biltoo";
        } else {
            path = "/tmp/biltoo-debug";
        }
        std::filesystem::create_directories(path);
        path += "/thumtoo-debug.log";
        std::FILE *f = std::fopen(path.c_str(), "a");
        if (f) {
            std::fprintf(f, "---- biltoo thumtoo debug session ----\n");
            std::fflush(f);
        }
        return f;
    }();
    return fp;
}

void thumtooDbg(const char *fmt, ...)
{
    if (!thumtooDebugEnabled()) {
        return;
    }
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fputs("biltoo/thumtoo: ", stderr);
    std::fputs(buf, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    if (std::FILE *f = thumtooDebugFile()) {
        std::fputs("biltoo/thumtoo: ", f);
        std::fputs(buf, f);
        std::fputc('\n', f);
        std::fflush(f);
    }
}

/** Cap concurrent thumtoo request_pixels — PDF raster+encode is heavy. */
constexpr int kMaxConcurrentPixelJobs = 3;
int g_pixelsActive = 0;
struct PendingPixels {
    QString path;
    int maxEdge = 0;
    QString inflightKey;
    std::string uri;
};
std::vector<PendingPixels> g_pixelsQueue;

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
        if (ref.isEpub()) {
#if defined(BILTOO_HAVE_THUMTOO_EPUB)
            thumtoo::EpubLayout layout =
                thumtoo::parse_epub_layout_params(ref.epubLayoutParams.toStdString());
            return thumtoo::epub_page_uri(absPathFast(ref.pdfPath), ref.page, layout);
#else
            // Layout + page pipes only — needs thumtoo EPUB support at runtime.
            return thumtoo::file_uri_from_path(absPathFast(ref.pdfPath))
                   + "//epub:" + ref.epubLayoutParams.toStdString()
                   + "//page:" + std::to_string(ref.page);
#endif
        }
#if defined(BILTOO_HAVE_THUMTOO_DJVU)
        if (thumtoo::is_likely_djvu_path(absPathFast(ref.pdfPath))) {
            return thumtoo::djvu_page_uri(absPathFast(ref.pdfPath), ref.page);
        }
#endif
#if defined(BILTOO_HAVE_THUMTOO_PDF)
        return thumtoo::pdf_page_uri(absPathFast(ref.pdfPath), ref.page);
#else
        return thumtoo::file_uri_from_path(absPathFast(ref.pdfPath))
               + "//page:" + std::to_string(ref.page);
#endif
    }
    if (PagePath::isPdfImageRef(path)) {
        const QString doc = PagePath::documentFilePath(path);
        const int n = PagePath::pdfImageNumber(path);
        if (doc.isEmpty() || n < 1) {
            return {};
        }
#if defined(BILTOO_HAVE_THUMTOO_PDF)
        return thumtoo::pdf_image_uri(absPathFast(doc), n);
#else
        return thumtoo::file_uri_from_path(absPathFast(doc))
               + "//pdfimage:" + std::to_string(n);
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
    static bool bannered = false;
    if (!bannered && thumtooDebugEnabled()) {
        bannered = true;
        thumtooDbg("ThumtooCache init — debug traces ON (THUMTOO_DEBUG/BILTOO_THUMTOO_DEBUG)");
        // Also go through Qt so it shows if stderr is swallowed by the launcher.
        qWarning("biltoo/thumtoo: debug traces ON");
    }

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

void enableDebugTracing()
{
    g_forceDebug = true;
    ::setenv("THUMTOO_DEBUG", "1", 1);
    thumtooDbg("debug tracing forced on (CLI/API); log file under XDG_CACHE_HOME/biltoo/thumtoo-debug.log");
    qWarning("biltoo/thumtoo: debug tracing ON → ~/.cache/biltoo/thumtoo-debug.log");
}

bool debugTracingEnabled()
{
    return thumtooDebugEnabled();
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
    if (thumtooDebugEnabled()) {
        thumtooDbg("scheduleProbe path=%s", qPrintable(path));
    }
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

#ifdef BILTOO_HAVE_THUMTOO
void startNextPixelJobsUnlocked()
{
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return;
    }
    while (g_pixelsActive < kMaxConcurrentPixelJobs && !g_pixelsQueue.empty()) {
        PendingPixels job = std::move(g_pixelsQueue.front());
        g_pixelsQueue.erase(g_pixelsQueue.begin());
        // May have been completed by another path; skip stale keys.
        if (!g_pixelsInflight.contains(job.inflightKey)) {
            continue;
        }
        ++g_pixelsActive;
        const QString pathCopy = job.path;
        const int edge = job.maxEdge;
        const QString inflightKey = job.inflightKey;
        const std::string uri = job.uri;
        if (thumtooDebugEnabled()) {
            thumtooDbg("request_pixels DISPATCH path=%s edge=%d active=%d queued=%zu",
                       qPrintable(pathCopy), edge, g_pixelsActive,
                       g_pixelsQueue.size());
        }
        c->request_pixels(
            uri, edge,
            [pathCopy, edge, inflightKey](std::string, int,
                                          std::optional<thumtoo::PixelLevel> px) {
                {
                    std::lock_guard lock(g_mu);
                    g_pixelsInflight.remove(inflightKey);
                    g_pixelsActive = qMax(0, g_pixelsActive - 1);
                    startNextPixelJobsUnlocked();
                }
                if (!px || px->bytes.empty()) {
                    emit bridge()->ladderReady(pathCopy, edge);
                    return;
                }
                emit bridge()->ladderReady(pathCopy, edge);
            });
    }
}
#endif

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
    // Dedup + global concurrency cap. PDF page raster+JXL in thumtoo is costly;
    // flooding request_pixels pegged a core even when the GUI was idle.
    const QString inflightKey = path + QLatin1Char('#') + QString::number(maxEdge);
    {
        std::lock_guard lock(g_mu);
        if (g_pixelsInflight.contains(inflightKey)) {
            return;
        }
        g_pixelsInflight.insert(inflightKey);
        g_pixelsQueue.push_back(PendingPixels{path, maxEdge, inflightKey, uri});
        if (thumtooDebugEnabled()) {
            thumtooDbg("schedulePixels queue path=%s edge=%d active=%d queued=%zu",
                       qPrintable(path), maxEdge, g_pixelsActive,
                       g_pixelsQueue.size());
        }
        startNextPixelJobsUnlocked();
    }
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
        if (ArchivePath::isArchiveRef(p) || PagePath::isPageRef(p)
            || PagePath::isPdfImageRef(p) || PagePath::isPdfImagesCollection(p)) {
            // Skip containers / embedded leaves: probing every page/member on
            // open floods the worker; per-tile scheduleProbe handles leaves.
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
#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_PDF)
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

QStringList expandPdfToImageRefs(const QString &pdfPath)
{
    QStringList out;
#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_PDF)
    if (pdfPath.isEmpty()) {
        return out;
    }
    const std::filesystem::path abs = absPathStd(pdfPath);
    if (!thumtoo::is_likely_pdf_path(abs)) {
        return out;
    }
#if defined(BILTOO_HAVE_THUMTOO_EXPAND)
    auto uris = thumtoo::expand_pdf_image_uris(abs, 4096);
    out.reserve(static_cast<int>(uris.size()));
    const QString pdfAbs = QString::fromStdString(abs.string());
    for (const auto &uri : uris) {
        auto parsed = thumtoo::parse_pdf_image_uri(uri);
        if (!parsed) {
            continue;
        }
        const QString ref = PagePath::makePdfImageRef(pdfAbs, parsed->image);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#else
    const auto count = thumtoo::pdf_embedded_image_count(abs);
    if (!count || *count <= 0) {
        return out;
    }
    const QString pdfAbs = QString::fromStdString(abs.string());
    out.reserve(*count);
    for (int i = 1; i <= *count; ++i) {
        const QString ref = PagePath::makePdfImageRef(pdfAbs, i);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#endif
#else
    Q_UNUSED(pdfPath);
#endif
    return out;
}

QStringList expandEpubToPageRefs(const QString &epubPath)
{
    QStringList out;
#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_EPUB)
    if (epubPath.isEmpty()) {
        return out;
    }
    const std::filesystem::path abs = absPathStd(epubPath);
    if (!thumtoo::is_likely_epub_path(abs)) {
        return out;
    }
#if defined(BILTOO_HAVE_THUMTOO_EXPAND)
    auto uris = thumtoo::expand_media_uris(abs, 512);
    out.reserve(static_cast<int>(uris.size()));
    for (const auto &uri : uris) {
        // file://…//epub:…//page:N → session path form
        auto parsed = thumtoo::parse_epub_uri(uri);
        if (!parsed) {
            continue;
        }
        const QString layout = QString::fromStdString(
            thumtoo::format_epub_layout_params(parsed->layout));
        const QString ref = PagePath::makeEpubRef(
            QString::fromStdString(parsed->epub_path.string()), parsed->page, layout);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#else
    const auto layout = thumtoo::default_epub_layout();
    const auto count = thumtoo::epub_page_count(abs, layout);
    if (!count || *count <= 0) {
        return out;
    }
    const QString layoutStr =
        QString::fromStdString(thumtoo::format_epub_layout_params(layout));
    const QString epubAbs = QString::fromStdString(abs.string());
    out.reserve(*count);
    for (int page = 1; page <= *count; ++page) {
        const QString ref = PagePath::makeEpubRef(epubAbs, page, layoutStr);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#endif
#else
    Q_UNUSED(epubPath);
#endif
    return out;
}


QStringList expandDjvuToPageRefs(const QString &djvuPath)
{
    QStringList out;
#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_DJVU)
    if (djvuPath.isEmpty()) {
        return out;
    }
    const std::filesystem::path abs = absPathStd(djvuPath);
    if (!thumtoo::is_likely_djvu_path(abs)) {
        return out;
    }
    const auto count = thumtoo::djvu_page_count(abs);
    if (!count || *count <= 0) {
        return out;
    }
    const QString absQ = QString::fromStdString(abs.string());
    out.reserve(*count);
    for (int page = 1; page <= *count; ++page) {
        const QString ref = PagePath::makeRef(absQ, page);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#else
    Q_UNUSED(djvuPath);
#endif
    return out;
}

QImage rasterizePdfPage(const QString &pdfPath, int page_1based, int maxEdge)
{
#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_PDF)
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


QImage rasterizePageRef(const QString &sessionPath, int maxEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (sessionPath.isEmpty()) {
        return {};
    }
    const PagePath::Ref ref = PagePath::parse(sessionPath);
    if (!ref.valid) {
        return {};
    }
    const int edge = maxEdge > 0 ? maxEdge : 2048;
    const std::filesystem::path abs = absPathStd(ref.pdfPath);

    if (ref.isEpub()) {
#if defined(BILTOO_HAVE_THUMTOO_EPUB)
        thumtoo::EpubLayout layout =
            thumtoo::parse_epub_layout_params(ref.epubLayoutParams.toStdString());
        auto raster = thumtoo::epub_rasterize_page(abs, ref.page, layout, edge);
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
        return {};
#endif
    }

#if defined(BILTOO_HAVE_THUMTOO_DJVU)
    if (thumtoo::is_likely_djvu_path(abs)) {
        auto raster = thumtoo::djvu_rasterize_page(abs, ref.page, edge);
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
    }
#endif

    return rasterizePdfPage(ref.pdfPath, ref.page, edge);
#else
    Q_UNUSED(sessionPath);
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



#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_TEXT)

namespace {

DocumentOutline convertOutline(const thumtoo::DocumentOutline &in)
{
    DocumentOutline out;
    out.items.reserve(static_cast<int>(in.items.size()));
    for (const auto &it : in.items) {
        OutlineItem o;
        o.level = it.level;
        o.title = QString::fromStdString(it.title);
        o.page = it.page_1based;
        o.uri = QString::fromStdString(it.uri);
        out.items.push_back(std::move(o));
    }
    return out;
}

/** Prefer page-ref URI; for outline, base document URI is enough. */
std::string outlineUriForPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    // Page refs work with ensure_document_outline (resolves base file).
    return toThumtooUri(path);
}

} // namespace

DocumentOutline cachedDocumentOutline(const QString &sessionOrFilePath)
{
    init();
    std::lock_guard lock(g_mu);
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return {};
    }
    const std::string uri = outlineUriForPath(sessionOrFilePath);
    if (uri.empty()) {
        return {};
    }
    auto outline = c->get_document_outline(uri);
    if (!outline) {
        return {};
    }
    return convertOutline(*outline);
}

DocumentOutline ensureDocumentOutline(const QString &sessionOrFilePath)
{
    init();
    std::lock_guard lock(g_mu);
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return {};
    }
    const std::string uri = outlineUriForPath(sessionOrFilePath);
    if (uri.empty()) {
        return {};
    }
    auto outline = c->ensure_document_outline(uri);
    if (!outline) {
        return {};
    }
    return convertOutline(*outline);
}

#else

DocumentOutline cachedDocumentOutline(const QString &)
{
    return {};
}
DocumentOutline ensureDocumentOutline(const QString &)
{
    return {};
}

#endif

QRectF pageRectToImageRect(const QRectF &pageRect, const QRectF &pageBounds,
                           const QSize &imageSize, bool pageYUp)
{
    const qreal pw = pageBounds.width();
    const qreal ph = pageBounds.height();
    if (pw <= 0 || ph <= 0 || imageSize.width() <= 0 || imageSize.height() <= 0) {
        return {};
    }
    const qreal nx0 = (pageRect.left() - pageBounds.left()) / pw;
    const qreal nx1 = (pageRect.right() - pageBounds.left()) / pw;
    qreal ny0;
    qreal ny1;
    if (pageYUp) {
        // PDF/DjVu: page Y increases upward; image Y increases downward.
        ny0 = (pageBounds.bottom() - pageRect.bottom()) / ph;
        ny1 = (pageBounds.bottom() - pageRect.top()) / ph;
    } else {
        // EPUB (MuPDF reflow): page Y already increases downward (top-left).
        ny0 = (pageRect.top() - pageBounds.top()) / ph;
        ny1 = (pageRect.bottom() - pageBounds.top()) / ph;
    }
    return QRectF(nx0 * imageSize.width(), ny0 * imageSize.height(),
                  (nx1 - nx0) * imageSize.width(), (ny1 - ny0) * imageSize.height());
}

#ifdef BILTOO_HAVE_THUMTOO
#if defined(BILTOO_HAVE_THUMTOO_TEXT)

namespace {

TextRegion convertRegion(const thumtoo::TextRegion &r)
{
    TextRegion out;
    out.bbox = QRectF(r.bbox.x0, r.bbox.y0, r.bbox.width(), r.bbox.height());
    out.role = (r.role == thumtoo::TextRegionRole::Link) ? TextRegion::Role::Link
                                                         : TextRegion::Role::Text;
    out.text = QString::fromStdString(r.text);
    if (r.target.kind == thumtoo::TextLinkTargetKind::InternalPage) {
        out.linkPage = r.target.page_1based;
    } else if (r.target.kind == thumtoo::TextLinkTargetKind::Uri) {
        out.linkUri = QString::fromStdString(r.target.uri);
    }
    return out;
}

PageTextLayer convertLayer(const thumtoo::PageTextLayer &layer)
{
    PageTextLayer out;
    out.page = layer.page_1based;
    out.layoutKey = QString::fromStdString(layer.layout_key);
    out.pageBounds = QRectF(layer.page_bounds.x0, layer.page_bounds.y0,
                            layer.page_bounds.width(), layer.page_bounds.height());
    out.regions.reserve(static_cast<int>(layer.regions.size()));
    for (const auto &r : layer.regions) {
        out.regions.push_back(convertRegion(r));
    }
    return out;
}

} // namespace

PageTextLayer cachedPageTextLayer(const QString &sessionPath)
{
    init();
    std::lock_guard lock(g_mu);
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return {};
    }
    const std::string uri = toThumtooUri(sessionPath);
    if (uri.empty()) {
        return {};
    }
    auto layer = c->get_page_text_layer(uri);
    if (!layer) {
        return {};
    }
    return convertLayer(*layer);
}

PageTextLayer ensurePageTextLayer(const QString &sessionPath)
{
    init();
    std::lock_guard lock(g_mu);
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return {};
    }
    const std::string uri = toThumtooUri(sessionPath);
    if (uri.empty()) {
        return {};
    }
    auto layer = c->ensure_page_text_layer(uri);
    if (!layer) {
        return {};
    }
    return convertLayer(*layer);
}

#else // BILTOO_HAVE_THUMTOO_TEXT

PageTextLayer cachedPageTextLayer(const QString &)
{
    return {};
}
PageTextLayer ensurePageTextLayer(const QString &)
{
    return {};
}

#endif
#else // no thumtoo

PageTextLayer cachedPageTextLayer(const QString &)
{
    return {};
}
PageTextLayer ensurePageTextLayer(const QString &)
{
    return {};
}

#endif


} // namespace ThumtooCache
