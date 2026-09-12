// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"
#include "imageloader.h"

#include "archivepath.h"
#include "pagepath.h"
#include <cstring>

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>
#include <QFile>
#include <QCryptographicHash>
#include <QHash>
#include <QDateTime>
#include <QRect>
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
#include <span>

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
#if __has_include("thumtoo/appearance.hpp")
#include "thumtoo/appearance.hpp"
#define BILTOO_HAVE_THUMTOO_APPEARANCE 1
#endif
#if __has_include("thumtoo/text.hpp")
#include "thumtoo/text.hpp"
#define BILTOO_HAVE_THUMTOO_TEXT 1
#endif
#if __has_include("thumtoo/lqip.hpp")
#include "thumtoo/lqip.hpp"
#define BILTOO_HAVE_THUMTOO_LQIP 1
#endif

#include <condition_variable>
#include <list>
#include <memory>
#include <string>
#include <span>
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

/** Cap concurrent thumtoo request_pixels — PDF raster+encode is heavy.
 *  Override with BILTOO_THUMTOO_PIXEL_JOBS (1–16). */
constexpr int kMaxConcurrentPixelJobsDefault = 3;
int maxConcurrentPixelJobs()
{
    static int n = []() {
        int v = kMaxConcurrentPixelJobsDefault;
        if (const char *e = std::getenv("BILTOO_THUMTOO_PIXEL_JOBS")) {
            char *end = nullptr;
            const long parsed = std::strtol(e, &end, 10);
            if (end != e && parsed >= 1 && parsed <= 16) {
                v = int(parsed);
            }
        }
        return v;
    }();
    return n;
}
int g_pixelsActive = 0;
struct PendingPixels {
    QString path;
    int maxEdge = 0;
    QString inflightKey;
    std::string uri;
};
std::vector<PendingPixels> g_pixelsQueue;
/** path#edge already finished (hit or miss) this process — no re-queue. */
QSet<QString> g_pixelsSettled;
/** path → last PixelSource int from ladderProvenance. */
QHash<QString, int> g_lastPixelSource;
constexpr int kMaxPixelQueue = 48;

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

QImage cachedLqipImage(const QString &path)
{
#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_LQIP)
    if (path.isEmpty()) {
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
    auto blob = c->get_lqip(uri);
    if (!blob || blob->empty()) {
        return {};
    }
    auto rgba = thumtoo::lqip_decode_rgba(
        std::span<const std::uint8_t>(blob->data(), blob->size()));
    if (!rgba || rgba->width < 1 || rgba->height < 1
        || rgba->rgba.size() < size_t(rgba->width) * size_t(rgba->height) * 4) {
        return {};
    }
    QImage img(rgba->width, rgba->height, QImage::Format_RGBA8888);
    if (img.isNull()) {
        return {};
    }
    const int rowBytes = rgba->width * 4;
    for (int y = 0; y < rgba->height; ++y) {
        memcpy(img.scanLine(y), rgba->rgba.data() + size_t(y) * size_t(rowBytes),
               size_t(rowBytes));
    }
    return img;
#else
    Q_UNUSED(path);
    return {};
#endif
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
#if defined(THUMTOO_API_REQUEST_RASTER) && THUMTOO_API_REQUEST_RASTER
    thumtoo::RasterRequest req;
    req.uri = uri;
    req.max_edge = maxEdge;
    req.frame_idx = 0;
    req.policy = thumtoo::RasterPolicy::PreferCache;
    auto px = c->get_raster(req);
#else
    auto px = c->get_pixels(uri, maxEdge);
#endif
    if (px) {
        if (px->bytes.empty()) {
            return {};
        }
        {
            std::lock_guard lock(g_mu);
            if (static_cast<int>(px->source) != 0) {
                g_lastPixelSource.insert(path, static_cast<int>(px->source));
            }
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
    while (g_pixelsActive < maxConcurrentPixelJobs() && !g_pixelsQueue.empty()) {
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
            thumtooDbg("request_raster DISPATCH path=%s edge=%d active=%d queued=%zu",
                       qPrintable(pathCopy), edge, g_pixelsActive,
                       g_pixelsQueue.size());
        }
        auto onPixels = [pathCopy, edge, inflightKey](
                            std::string, int,
                            std::optional<thumtoo::PixelLevel> px) {
            QImage decoded;
            int pxW = 0;
            int pxH = 0;
            int source = 0;
            if (px && !px->bytes.empty()) {
                pxW = px->width;
                pxH = px->height;
                source = static_cast<int>(px->source);
                const QByteArray ba(
                    reinterpret_cast<const char *>(px->bytes.data()),
                    int(px->bytes.size()));
                // Decode the payload we actually received — do not re-query
                // get_pixels (can still see an older smaller level).
                decoded = ImageLoader::loadThumbnailFromBytes(ba, 0);
            }
            {
                std::lock_guard lock(g_mu);
                g_pixelsInflight.remove(inflightKey);
                // Settle only when pixels meet ~90% of the requested edge.
                const int got = decoded.isNull()
                                    ? 0
                                    : qMax(decoded.width(), decoded.height());
                const bool ok = got >= (edge * 9) / 10;
                if (ok) {
                    g_pixelsSettled.insert(inflightKey);
                }
                g_pixelsActive = qMax(0, g_pixelsActive - 1);
                if (thumtooDebugEnabled()) {
                    thumtooDbg(
                        "request_raster DONE path=%s edge=%d ok=%d src=%d "
                        "level=%dx%d decoded=%dx%d active=%d queued=%zu",
                        qPrintable(pathCopy), edge, ok ? 1 : 0, source, pxW, pxH,
                        decoded.width(), decoded.height(), g_pixelsActive,
                        g_pixelsQueue.size());
                }
                startNextPixelJobsUnlocked();
            }
            {
                std::lock_guard lock(g_mu);
                if (source != 0) {
                    g_lastPixelSource.insert(pathCopy, source);
                }
            }
            emit bridge()->ladderReady(pathCopy, edge, decoded);
            emit bridge()->ladderProvenance(pathCopy, edge, source);
        };
#if defined(THUMTOO_API_REQUEST_RASTER) && THUMTOO_API_REQUEST_RASTER
        thumtoo::RasterRequest req;
        req.uri = uri;
        req.max_edge = edge;
        req.frame_idx = 0;
        req.policy = thumtoo::RasterPolicy::SoftOnly;
        c->request_raster(std::move(req), std::move(onPixels));
#else
        c->request_pixels(uri, edge, std::move(onPixels));
#endif
    }
}
#endif

void forgetPixelsSettled(const QString &path, int maxEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (path.isEmpty() || maxEdge <= 0) {
        return;
    }
    const QString key = path + QLatin1Char('#') + QString::number(maxEdge);
    std::lock_guard lock(g_mu);
    g_pixelsSettled.remove(key);
#else
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
#endif
}

bool isPixelsInflight(const QString &path, int maxEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (path.isEmpty() || maxEdge <= 0) {
        return false;
    }
    const QString key = path + QLatin1Char('#') + QString::number(maxEdge);
    std::lock_guard lock(g_mu);
    return g_pixelsInflight.contains(key);
#else
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
    return false;
#endif
}


QString lastPixelSourceLabel(const QString &path)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (path.isEmpty()) {
        return {};
    }
    int source = 0;
    {
        std::lock_guard lock(g_mu);
        source = g_lastPixelSource.value(path, 0);
    }
    switch (source) {
    case 1:
        return QStringLiteral("jpeg_shrink");
    case 2:
        return QStringLiteral("embedded");
    case 3:
        return QStringLiteral("full");
    case 4:
        return QStringLiteral("tile_synth");
    default:
        return {};
    }
#else
    Q_UNUSED(path);
    return {};
#endif
}

bool schedulePixels(const QString &path, int maxEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
    if (maxEdge <= 0 || isUnsupported(path)) {
        return false;
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return false;
    }
    // Dedup + settle. Soft queue soft-cap: still accept (no silent DROP) so
    // filmstrip/gallery are not starved; concurrency remains limited.
    const QString inflightKey = path + QLatin1Char('#') + QString::number(maxEdge);
    {
        std::lock_guard lock(g_mu);
        if (g_pixelsInflight.contains(inflightKey)
            || g_pixelsSettled.contains(inflightKey)) {
            if (thumtooDebugEnabled()) {
                thumtooDbg("schedulePixels SKIP path=%s edge=%d (inflight/settled)",
                           qPrintable(path), maxEdge);
            }
            return false;
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
    return true;
#else
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
    return false;
#endif
}

bool scheduleOverviewPixels(const QString &path, int maxEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
#if defined(THUMTOO_API_SET_INTEREST) && THUMTOO_API_SET_INTEREST
    // Overview work is owned by setInterest / setPrimaryInterest snapshots.
    // One-off scheduleOverviewPixels would race the interest epoch and
    // double-extract archives. Prefer setInterest for windowed paths.
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
    return false;
#elif defined(THUMTOO_API_OVERVIEW_PIXELS) && THUMTOO_API_OVERVIEW_PIXELS
    if (maxEdge <= 0 || isUnsupported(path)) {
        return false;
    }
    if (maxEdge > kBatchOverviewEdge) {
        maxEdge = kBatchOverviewEdge;
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return false;
    }
    const QString inflightKey =
        path + QLatin1Char('#') + QStringLiteral("ov") + QString::number(maxEdge);
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        if (g_pixelsInflight.contains(inflightKey)
            || g_pixelsSettled.contains(inflightKey)) {
            return false;
        }
        g_pixelsInflight.insert(inflightKey);
        c = clientUnlocked();
        if (!c) {
            g_pixelsInflight.remove(inflightKey);
            return false;
        }
        ++g_pixelsActive;
    }
    const QString pathCopy = path;
    const int edge = maxEdge;
    auto onOverview = [pathCopy, edge, inflightKey](
                          std::string, int,
                          std::optional<thumtoo::PixelLevel> px) {
        QImage decoded;
        int source = 0;
        if (px && !px->bytes.empty()) {
            source = static_cast<int>(px->source);
            const QByteArray ba(
                reinterpret_cast<const char *>(px->bytes.data()),
                int(px->bytes.size()));
            decoded = ImageLoader::loadThumbnailFromBytes(ba, 0);
        }
        {
            std::lock_guard doneLock(g_mu);
            g_pixelsInflight.remove(inflightKey);
            if (source != 0) {
                g_lastPixelSource.insert(pathCopy, source);
            }
            const int got = decoded.isNull()
                                ? 0
                                : qMax(decoded.width(), decoded.height());
            if (got >= (edge * 9) / 10) {
                g_pixelsSettled.insert(inflightKey);
            }
            g_pixelsActive = qMax(0, g_pixelsActive - 1);
            startNextPixelJobsUnlocked();
        }
        emit bridge()->ladderReady(pathCopy, edge, decoded);
        emit bridge()->ladderProvenance(pathCopy, edge, source);
    };
#if defined(THUMTOO_API_REQUEST_RASTER) && THUMTOO_API_REQUEST_RASTER
    thumtoo::RasterRequest req;
    req.uri = uri;
    req.max_edge = edge;
    req.frame_idx = 0;
    req.policy = thumtoo::RasterPolicy::Overview;
    c->request_raster(std::move(req), std::move(onOverview));
#else
    c->request_overview_pixels(uri, edge, std::move(onOverview));
#endif
    return true;
#else
    // Older thumtoo: fall back to soft ladder schedule (clamped inside).
    return schedulePixels(path, qMin(maxEdge, kGalleryLadderEdge));
#endif
#else
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
    return false;
#endif
}

quint64 bumpInterestEpoch()
{
#ifdef BILTOO_HAVE_THUMTOO
#if defined(THUMTOO_API_INTEREST_EPOCH) && THUMTOO_API_INTEREST_EPOCH
    init();
    std::lock_guard lock(g_mu);
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return 0;
    }
    // Drop host-side pixel queue so we do not keep dispatching stale paths.
    g_pixelsQueue.clear();
    return static_cast<quint64>(c->bump_interest_epoch());
#else
    return 0;
#endif
#else
    return 0;
#endif
}

int cancelPendingThumtooWork()
{
#ifdef BILTOO_HAVE_THUMTOO
#if defined(THUMTOO_API_INTEREST_EPOCH) && THUMTOO_API_INTEREST_EPOCH
    init();
    std::lock_guard lock(g_mu);
    g_pixelsQueue.clear();
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return 0;
    }
    return static_cast<int>(c->cancel_pending());
#else
    return 0;
#endif
#else
    return 0;
#endif
}

quint64 setInterest(const QStringList &pathsNear, const QStringList &pathsSpeculative,
                    int nearEdge, int speculativeEdge,
                    const QStringList &pathsPrimary, int primaryEdge)
{
#ifdef BILTOO_HAVE_THUMTOO
#if defined(THUMTOO_API_SET_INTEREST) && THUMTOO_API_SET_INTEREST
    init();
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
        if (c) {
            g_pixelsQueue.clear();
        }
    }
    if (!c) {
        return 0;
    }
    std::vector<thumtoo::InterestItem> items;
    items.reserve(size_t(pathsNear.size() + pathsSpeculative.size()
                          + pathsPrimary.size()));
    auto push = [&](const QStringList &paths, thumtoo::InterestRole role, int edge) {
        for (const QString &p : paths) {
            if (p.isEmpty() || isUnsupported(p)) {
                continue;
            }
            const std::string uri = toThumtooUri(p);
            if (uri.empty()) {
                continue;
            }
            thumtoo::InterestItem it;
            it.uri = uri;
            it.target_long_edge = edge > 0 ? edge : kBatchOverviewEdge;
            it.role = role;
            items.push_back(std::move(it));
        }
    };
    // Primary first in the vector (set_interest also sorts by role).
    push(pathsPrimary, thumtoo::InterestRole::Primary,
         primaryEdge > 0 ? primaryEdge : kBatchOverviewEdge);
    push(pathsNear, thumtoo::InterestRole::Near, nearEdge);
    push(pathsSpeculative, thumtoo::InterestRole::Speculative, speculativeEdge);
    return static_cast<quint64>(c->set_interest(std::move(items)));
#else
    Q_UNUSED(pathsNear);
    Q_UNUSED(pathsSpeculative);
    Q_UNUSED(nearEdge);
    Q_UNUSED(speculativeEdge);
    Q_UNUSED(pathsPrimary);
    Q_UNUSED(primaryEdge);
    return bumpInterestEpoch();
#endif
#else
    Q_UNUSED(pathsNear);
    Q_UNUSED(pathsSpeculative);
    Q_UNUSED(nearEdge);
    Q_UNUSED(speculativeEdge);
    Q_UNUSED(pathsPrimary);
    Q_UNUSED(primaryEdge);
    return 0;
#endif
}

quint64 setPrimaryInterest(const QString &path, int edge)
{
#ifdef BILTOO_HAVE_THUMTOO
#if defined(THUMTOO_API_SET_INTEREST) && THUMTOO_API_SET_INTEREST
    if (path.isEmpty() || isUnsupported(path)) {
        return 0;
    }
    init();
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
        if (c) {
            g_pixelsQueue.clear();
        }
    }
    if (!c) {
        return 0;
    }
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return 0;
    }
    if (edge <= 0) {
        edge = kBatchOverviewEdge;
    }
    if (edge > kBatchOverviewEdge) {
        edge = kBatchOverviewEdge;
    }
    thumtoo::InterestItem it;
    it.uri = uri;
    it.target_long_edge = edge;
    it.role = thumtoo::InterestRole::Primary;
    std::vector<thumtoo::InterestItem> items;
    items.push_back(std::move(it));
    return static_cast<quint64>(c->set_interest(std::move(items)));
#else
    Q_UNUSED(path);
    Q_UNUSED(edge);
    return bumpInterestEpoch();
#endif
#else
    Q_UNUSED(path);
    Q_UNUSED(edge);
    return 0;
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
    // Text layers are UTF-8 (MuPDF codepoints encoded in thumtoo).
    out.text = QString::fromUtf8(r.text.data(), int(r.text.size()));
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
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return {};
    }
    const std::string uri = toThumtooUri(sessionPath);
    if (uri.empty()) {
        return {};
    }
    // Client DB reads are internally synchronized; do not hold g_mu across I/O.
    auto layer = c->get_page_text_layer(uri);
    if (!layer) {
        return {};
    }
    return convertLayer(*layer);
}

PageTextLayer ensurePageTextLayer(const QString &sessionPath)
{
    init();
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        qWarning().noquote()
            << QStringLiteral("[find] ensurePageTextLayer: no thumtoo client path=%1")
                   .arg(sessionPath);
        return {};
    }
    const std::string uri = toThumtooUri(sessionPath);
    if (uri.empty()) {
        qWarning().noquote()
            << QStringLiteral("[find] ensurePageTextLayer: empty URI path=%1")
                   .arg(sessionPath);
        return {};
    }
    // Extract/MuPDF must not run under g_mu — document search runs this on a
    // worker while the GUI may also refresh the current page.
    auto layer = c->ensure_page_text_layer(uri);
    if (!layer) {
        qWarning().noquote()
            << QStringLiteral("[find] ensurePageTextLayer: extract failed path=%1 uri=%2")
                   .arg(sessionPath)
                   .arg(QString::fromStdString(uri));
        return {};
    }
    qWarning().noquote()
        << QStringLiteral("[find] ensurePageTextLayer: ok path=%1 uri=%2 regions=%3")
               .arg(sessionPath)
               .arg(QString::fromStdString(uri))
               .arg(static_cast<int>(layer->regions.size()));
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



bool StoredContentAppearance::isIdentity() const
{
    if (contentHFlip || contentVFlip || contentQuarterTurns != 0) {
        return false;
    }
    if (hasCrop && !cropRect.isEmpty()) {
        return false;
    }
    if (hasGrade) {
        return false;
    }
    return true;
}

#if defined(BILTOO_HAVE_THUMTOO) && defined(BILTOO_HAVE_THUMTOO_APPEARANCE)

namespace {

bool appearanceDebug()
{
    static const bool on = qEnvironmentVariableIsSet("BILTOO_DEBUG_APPEARANCE");
    return on;
}

void appearanceLog(const QString &msg)
{
    // Always use qWarning so it shows without QT_LOGGING_RULES tweaks.
    qWarning().noquote() << QStringLiteral("[appearance]") << msg;
}

thumtoo::AppearanceStore &appearanceStore()
{
    // Non-throwing open; invalid store → load/save become no-ops.
    static thumtoo::AppearanceStore store = []() {
        auto s = thumtoo::AppearanceStore::open();
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_APPEARANCE")) {
            qWarning().noquote() << QStringLiteral("[appearance] store open valid=")
                                 << s.valid()
                                 << QStringLiteral("db=")
                                 << QString::fromStdString(s.db_path().string());
        }
        return s;
    }();
    return store;
}

std::string pathContentId(const QString &path)
{
    if (path.isEmpty()) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("pathContentId reject: empty path"));
        }
        return {};
    }
    // Regular local files only for v1 (no //page: / archive members yet).
    // Allow "file:///..." but reject thumtoo compound URIs (…//page:, //rar:…).
    QString local = path;
    if (local.startsWith(QStringLiteral("file:"))) {
        local = QUrl(local).toLocalFile();
    }
    // PDF/EPUB/DjVu page refs: hash outer file + ":page:N" suffix.
    // Other compound URIs (archives, pdfimage) still unsupported in v1.
    QString fileForHash = local;
    int page1 = 0;
    if (PagePath::isPageRef(local)) {
        const PagePath::Ref ref = PagePath::parse(local);
        if (!ref.valid || ref.page < 1 || ref.pdfPath.isEmpty()) {
            if (appearanceDebug()) {
                appearanceLog(QStringLiteral("pathContentId reject: invalid page ref path=%1")
                                  .arg(path));
            }
            return {};
        }
        fileForHash = ref.pdfPath;
        page1 = ref.page;
    } else if (PagePath::isPdfImageRef(local) || local.contains(QStringLiteral("//"))) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("pathContentId reject: compound/non-page ref path=%1")
                              .arg(path));
        }
        return {};
    }
    const QFileInfo fi(fileForHash);
    if (!fi.exists() || !fi.isFile()) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("pathContentId reject: not a file path=%1 exists=%2 isFile=%3")
                              .arg(fileForHash)
                              .arg(fi.exists())
                              .arg(fi.isFile()));
        }
        return {};
    }
    const QString abs = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath()
                                                         : fi.canonicalFilePath();
    const qint64 size = fi.size();
    const QDateTime mtime = fi.lastModified();
    // Cache key includes page so //page:1 and //page:2 do not collide.
    const QString cacheKey = page1 > 0
        ? (abs + QStringLiteral("#page=") + QString::number(page1))
        : abs;
    // Cache sha256 by path+size+mtime — hashing multi‑MB images on every
    // seed/save would stall the UI thread.
    struct CacheEntry {
        qint64 size = -1;
        QDateTime mtime;
        std::string id;
    };
    static QHash<QString, CacheEntry> cache;
    static std::mutex cacheMu;
    {
        std::lock_guard lock(cacheMu);
        const auto it = cache.constFind(cacheKey);
        if (it != cache.cend() && it->size == size && it->mtime == mtime
            && !it->id.empty()) {
            return it->id;
        }
    }
    std::string id;
    QString hashStage;
    // Prefer thumtoo hasher; path must be UTF-8 (not QString::toStdString locale).
    try {
        const QByteArray utf8 = abs.toUtf8();
        const std::string hex = thumtoo::sha256_file_hex(
            std::filesystem::path(std::string(utf8.constData(),
                                              static_cast<size_t>(utf8.size()))));
        if (!hex.empty()) {
            id = thumtoo::normalize_content_id(hex);
            hashStage = id.empty() ? QStringLiteral("thumtoo-hex-normalize-fail")
                                   : QStringLiteral("thumtoo");
        } else {
            hashStage = QStringLiteral("thumtoo-hex-empty");
        }
    } catch (...) {
        id.clear();
        hashStage = QStringLiteral("thumtoo-exception");
    }
    // Fallback: chunked Qt SHA-256 (do not rely on addData(QIODevice*), which
    // can fail or behave differently across Qt builds).
    if (id.empty()) {
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) {
            hashStage += QStringLiteral("+qt-open-fail:");
            hashStage += f.errorString();
        } else {
            QCryptographicHash h(QCryptographicHash::Sha256);
            QByteArray buf;
            buf.resize(256 * 1024);
            bool ok = true;
            while (!f.atEnd()) {
                const qint64 n = f.read(buf.data(), buf.size());
                if (n < 0) {
                    ok = false;
                    hashStage += QStringLiteral("+qt-read-fail");
                    break;
                }
                if (n == 0) {
                    break;
                }
                h.addData(QByteArray(buf.constData(), int(n)));
            }
            if (ok) {
                const QByteArray dig = h.result().toHex();
                id = thumtoo::normalize_content_id(
                    std::string(dig.constData(), static_cast<size_t>(dig.size())));
                hashStage = id.empty() ? QStringLiteral("qt-normalize-fail")
                                       : QStringLiteral("qt");
            }
        }
    }
    // Page session ref: fold into a plain sha256:<64hex> so AppearanceStore::put
    // (which always normalize_content_id's the key) accepts it even when the
    // linked thumtoo only allows bare file hashes. Material:
    //   utf8( "<file_content_id>:page:<n>" ) → SHA-256 → sha256:<hex>
    if (!id.empty() && page1 > 0) {
        const QByteArray material =
            QByteArray::fromStdString(id) + ":page:" + QByteArray::number(page1);
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(material);
        const QByteArray dig = h.result().toHex();
        const std::string pageId = thumtoo::normalize_content_id(
            std::string(dig.constData(), static_cast<size_t>(dig.size())));
        if (appearanceDebug()) {
            appearanceLog(
                QStringLiteral("pathContentId page-fold file_id=%1 page=%2 → %3")
                    .arg(QString::fromStdString(id))
                    .arg(page1)
                    .arg(QString::fromStdString(pageId)));
        }
        id = pageId;
    }
    if (!id.empty()) {
        std::lock_guard lock(cacheMu);
        CacheEntry e;
        e.size = size;
        e.mtime = mtime;
        e.id = id;
        cache.insert(cacheKey, e);
    }
    if (appearanceDebug()) {
        if (id.empty()) {
            appearanceLog(
                QStringLiteral("pathContentId EMPTY path=%1 abs=%2 size=%3 page=%4 stage=%5")
                    .arg(path, abs)
                    .arg(size)
                    .arg(page1)
                    .arg(hashStage));
        } else {
            appearanceLog(QStringLiteral("pathContentId ok path=%1 id=%2 stage=%3")
                              .arg(path, QString::fromStdString(id), hashStage));
        }
    }
    return id;
}

} // namespace

QString contentIdForPath(const QString &path)
{
    const std::string id = pathContentId(path);
    return id.empty() ? QString() : QString::fromStdString(id);
}

bool loadContentAppearance(const QString &path, StoredContentAppearance *out)
{
    if (!out) {
        return false;
    }
    *out = StoredContentAppearance{};
    const std::string id = pathContentId(path);
    if (id.empty()) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("load SKIP: no content id path=%1").arg(path));
        }
        return false;
    }
    if (!appearanceStore().valid()) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("load SKIP: store invalid"));
        }
        return false;
    }
    const auto got = appearanceStore().get(id);
    if (!got) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("load MISS id=%1 path=%2")
                              .arg(QString::fromStdString(id), path));
        }
        return false;
    }
    if (appearanceDebug()) {
        appearanceLog(QStringLiteral("load HIT id=%1 turns=%2 h=%3 v=%4")
                          .arg(QString::fromStdString(id))
                          .arg(got->content_quarter_turns)
                          .arg(got->content_h_flip)
                          .arg(got->content_v_flip));
    }
    out->contentHFlip = got->content_h_flip;
    out->contentVFlip = got->content_v_flip;
    out->contentQuarterTurns = got->content_quarter_turns;
    out->hasCrop = got->has_crop;
    if (got->has_crop) {
        out->cropRect = QRect(got->crop_x, got->crop_y, got->crop_w, got->crop_h);
        out->cropSourceSize = QSize(got->crop_source_w, got->crop_source_h);
        out->cropRotation = got->crop_rotation;
    }
    if (got->grade_brightness || got->grade_contrast || got->grade_saturation
        || got->grade_hue || got->grade_gamma
#if defined(THUMTOO_APPEARANCE_GRADE_INVERT)
        || got->grade_invert
#endif
    ) {
        out->hasGrade = true;
        out->gradeBrightness = got->grade_brightness.value_or(0);
        // ColorAdjustments uses contrast/saturation 100 = identity. Missing
        // optional fields must not collapse to 0 (which blacks out tiles).
        out->gradeContrast = got->grade_contrast.value_or(100);
        out->gradeSaturation = got->grade_saturation.value_or(100);
        out->gradeHue = got->grade_hue.value_or(0);
        // Stored as percent ×100 of gamma (100 → 1.0).
        out->gradeGamma = got->grade_gamma.value_or(100);
#if defined(THUMTOO_APPEARANCE_GRADE_INVERT)
        out->gradeInvert = got->grade_invert.value_or(0) != 0;
#endif
    }
    return !out->isIdentity();
}

void saveContentAppearance(const QString &path, const StoredContentAppearance &app)
{
    const std::string id = pathContentId(path);
    if (id.empty()) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("save SKIP: no content id for path=%1 (h=%2 v=%3 turns=%4 crop=%5)")
                              .arg(path)
                              .arg(app.contentHFlip)
                              .arg(app.contentVFlip)
                              .arg(app.contentQuarterTurns)
                              .arg(app.hasCrop));
        }
        return;
    }
    if (!appearanceStore().valid()) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("save SKIP: store invalid path=%1").arg(path));
        }
        return;
    }
    thumtoo::ContentAppearance a;
    a.content_h_flip = app.contentHFlip;
    a.content_v_flip = app.contentVFlip;
    a.content_quarter_turns = app.contentQuarterTurns;
    a.has_crop = app.hasCrop && !app.cropRect.isEmpty();
    if (a.has_crop) {
        a.crop_x = app.cropRect.x();
        a.crop_y = app.cropRect.y();
        a.crop_w = app.cropRect.width();
        a.crop_h = app.cropRect.height();
        a.crop_source_w = app.cropSourceSize.width();
        a.crop_source_h = app.cropSourceSize.height();
        a.crop_rotation = app.cropRotation;
    }
    if (app.hasGrade) {
        a.grade_brightness = app.gradeBrightness;
        a.grade_contrast = app.gradeContrast;
        a.grade_saturation = app.gradeSaturation;
        a.grade_hue = app.gradeHue;
        a.grade_gamma = app.gradeGamma;
#if defined(THUMTOO_APPEARANCE_GRADE_INVERT)
        if (app.gradeInvert) {
            a.grade_invert = 1;
        }
#endif
    }
    if (appearanceDebug()) {
        appearanceLog(
            QStringLiteral("save PUT id=%1 h=%2 v=%3 turns=%4 crop=%5 identity=%6 db=%7")
                .arg(QString::fromStdString(id))
                .arg(a.content_h_flip)
                .arg(a.content_v_flip)
                .arg(a.content_quarter_turns)
                .arg(a.has_crop)
                .arg(a.is_identity())
                .arg(QString::fromStdString(appearanceStore().db_path().string())));
    }
    appearanceStore().put(id, a);
    if (appearanceDebug()) {
        const auto got = appearanceStore().get(id);
        appearanceLog(QStringLiteral("save AFTER put row_present=%1 turns=%2")
                          .arg(got.has_value())
                          .arg(got ? got->content_quarter_turns : -1));
    }
}

bool hasContentAppearance(const QString &path)
{
    StoredContentAppearance app;
    return loadContentAppearance(path, &app);
}

void clearContentAppearance(const QString &path)
{
    saveContentAppearance(path, StoredContentAppearance{});
}

#else // no thumtoo appearance

// Compile-time: thumtoo/appearance.hpp not found — all persistence is a no-op.
// If the DB file exists from an older build, this binary will not write to it.

QString contentIdForPath(const QString &)
{
    static const bool once = []() {
        qWarning().noquote()
            << QStringLiteral("[appearance] DISABLED at compile time "
                              "(BILTOO_HAVE_THUMTOO_APPEARANCE not set — "
                              "rebuild biltoo against thumtoo with appearance.hpp)");
        return true;
    }();
    Q_UNUSED(once);
    return {};
}

bool loadContentAppearance(const QString &, StoredContentAppearance *out)
{
    if (out) {
        *out = StoredContentAppearance{};
    }
    return false;
}

void saveContentAppearance(const QString &, const StoredContentAppearance &)
{
}

bool hasContentAppearance(const QString &)
{
    return false;
}

void clearContentAppearance(const QString &)
{
}

#endif


} // namespace ThumtooCache
