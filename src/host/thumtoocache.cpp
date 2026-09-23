// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host/thumtoocache.h"
#include "host/thumtoo_process_memos.h"
#include "tilelod/thumtoo_tile_source.hpp"
#include "tilelod/tile_painter.hpp"
#include "host/imageloader.h"
#include "display/imagecache.h"
#include "util/biltoo_thread.h"

#include "host/archivepath.h"
#include "host/pagepath.h"
#include <cstring>

#include <QCoreApplication>
#include <QObject>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QUrl>
#include <QFile>
#include <QThread>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QDateTime>
#include <QRect>
#include <QMetaObject>
#include <QSet>
#include <QDebug>
#include <QThreadPool>
#include <QVector>
#include <QPair>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <atomic>
#include <string>
#include <span>

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

#include <sqlite3.h>
#include <condition_variable>
#include <list>
#include <memory>
#include <string>
#include <span>
#include <unordered_map>
#include <vector>


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
constexpr int kMaxConcurrentPixelJobsDefault = 8;
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
/** Concurrent request_full_pixels jobs (Gallery SoftDisplay Fulls visibles). */
int g_fullActive = 0;
constexpr int kMaxConcurrentFullJobs = 4;
struct PendingPixels {
    QString path;
    int maxEdge = 0;
    QString inflightKey;
    std::string uri;
};
std::vector<PendingPixels> g_pixelsQueue;
/** path#edge already finished (hit or miss) this process — no re-queue. */
QSet<QString> g_pixelsSettled;
/** How many times schedule* queued this key (detect soft re-request loops). */
QHash<QString, int> g_pixelsScheduleCount;
constexpr int kMaxPixelScheduleAttempts = 4;
/** Process memo of ContentStatus::Unsupported (get_meta is SQLite — never on GUI). */
QSet<QString> g_unsupportedYes;
QSet<QString> g_unsupportedNo;
/**
 * Negative durable-tile memo TTL (ProcessMemos). Short so mid-session
 * prepare/FocusFull can still flip true after a miss.
 */
constexpr qint64 kDurableTilesNegativeTtlMs = 2500;
/** path → last PixelSource int from ladderProvenance. */
QHash<QString, int> g_lastPixelSource;
/** 0 unknown, 1 cache soft/tiles, 2 file/archive encode (inflight paths). */
QHash<QString, int> g_fetchKind;
int g_recentCacheCompletions = 0;
int g_recentFileCompletions = 0;
QString g_lastInterestKey;
std::atomic<quint64> g_interestJobGen{0};
constexpr int kMaxPixelQueue = 96;


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

void memberLruRemove(const QString &key)
{
    if (key.isEmpty()) {
        return;
    }
    const std::string k = key.toStdString();
    std::lock_guard lock(g_memberLru.mu);
    auto it = g_memberLru.map.find(k);
    if (it == g_memberLru.map.end()) {
        return;
    }
    g_memberLru.totalBytes -= it->second.first.size();
    g_memberLru.order.erase(it->second.second);
    g_memberLru.map.erase(it);
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
    // String/path → URI (and URI cache). Allowed on the GUI for cache-only
    // lookups (cachedSize / isUnsupported). Must not be paired with decode or
    // request_* on the GUI — those paths ASSERT_NOT_GUI_THREAD on workers.
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
        // Store locator size/mtime (Store-only Client).
        // Prefer the filesystem path we scheduled for the outer file check.
        std::filesystem::path outer;
        std::optional<std::int64_t> cached_mtime;
        std::optional<std::int64_t> cached_size;
        if (!pathCopy.isEmpty()) {
            outer = std::filesystem::path(pathCopy.toStdString());
        }
        if (auto loc = c->store().find_locator(uriCopy)) {
            cached_mtime = loc->mtime_ns;
            cached_size = loc->size;
        }
        if (outer.empty()) {
            return;
        }
        const auto mtime = fileMtimeFingerprint(outer);
        const auto size = fileSizeBytes(outer);
        // No stored fingerprint: nothing to compare; leave cache as-is.
        if (!cached_mtime && !cached_size) {
            return;
        }
        bool mismatch = false;
        if (cached_mtime && mtime && *cached_mtime != *mtime) {
            mismatch = true;
        }
        if (cached_size && size && *cached_size != *size) {
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
        // Do not scheduleSoftPixels — Gallery is tiles + LQIP only; soft PreferCache
        // was flooding DEBUG_OVERLAY soft req=512 and GUI budget.
        scheduleProbe(pathCopy);
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

/// User overlays (tags, collections, bookmarks) — must survive cache wipe.
std::filesystem::path defaultDataRoot()
{
    if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "thumtoo";
    }
    if (const char *home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "thumtoo";
    }
    return std::filesystem::path(".local") / "share" / "thumtoo";
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


Bridge *g_bridge = nullptr;

} // namespace

Bridge *bridge()
{
    if (!g_bridge) {
        g_bridge = new Bridge(QCoreApplication::instance());
    }
    return g_bridge;
}

void openClientUnlocked()
{
    // Caller holds g_mu or is the sole opener; sets g_inited before Client::open
    // so concurrent isAvailable() waits on the lock instead of double-open.
    if (g_inited) {
        return;
    }
    g_inited = true;
    try {
        thumtoo::image_library_init();
        // data_root: user.sqlite under XDG_DATA so tags/sets survive cache wipe.
        // Store-only Client (schema ≥100); user.sqlite under data_root.
        g_client = thumtoo::Client::open(defaultCacheRoot(), qtExecutor(), 0,
                                        defaultDataRoot());
    } catch (...) {
        g_client.reset();
    }
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

    {
        std::lock_guard lock(g_mu);
        if (g_inited) {
            return;
        }
    }
    // Client::open touches the durable DB and must not run on the GUI thread
    // (cold cache / slow home-dir disk freezes the whole app at startup, and
    // first open of USB session paths often hit isAvailable on the GUI).
    if (QThread::isMainThread()) {
        static std::atomic<bool> openScheduled{false};
        bool expected = false;
        if (!openScheduled.compare_exchange_strong(expected, true)) {
            return;
        }
        QThreadPool::globalInstance()->start([]() {
            ASSERT_NOT_GUI_THREAD();
            std::lock_guard lock(g_mu);
            openClientUnlocked();
        });
        return;
    }
    std::lock_guard lock(g_mu);
    openClientUnlocked();
}

void shutdown()
{
    // Abort superseded interest jobs and cancel host work *before* destroying
    // the client. aboutToQuit used to g_client.reset() while a pool thread was
    // still inside Client::set_interest → Store::find_locator (NFS/archive
    // paths hold the DB mutex for a long time) → use-after-free / hang on
    // QThreadPool::waitForDone in ~QCoreApplication.
    ++g_interestJobGen;
    {
        std::lock_guard lock(g_mu);
        g_pixelsQueue.clear();
        g_lastInterestKey.clear();
        thumtoo::Client *c = clientUnlocked();
        if (c) {
            (void)c->cancel_pending();
        }
    }
    QThreadPool::globalInstance()->clear();
    // Bounded wait: never block process exit on a stuck NFS locator.
    constexpr int kExitWaitMs = 2500;
    if (!QThreadPool::globalInstance()->waitForDone(kExitWaitMs)) {
        qWarning("biltoo/thumtoo: pool still busy after %d ms on shutdown; "
                 "abandoning Client (process exit)",
                 kExitWaitMs);
        std::lock_guard lock(g_mu);
        // Leak the client object so in-flight set_interest does not UAF.
        (void)g_client.release();
        g_inited = false;
        return;
    }
    {
        std::lock_guard lock(g_mu);
        g_client.reset();
        g_inited = false;
    }
}

void enableDebugTracing()
{
    g_forceDebug = true;
    ::setenv("THUMTOO_DEBUG", "1", 1);
    thumtooDbg("debug tracing forced on (CLI/API); log file under XDG_CACHE_HOME/biltoo/thumtoo-debug.log");
    qWarning("biltoo/thumtoo: debug tracing ON → ~/.cache/biltoo/thumtoo-debug.log");
}

void noteCachedSize(const QString &path, const QSize &size)
{
    ProcessMemos::instance().noteSize(path, size);
}

#if defined(BILTOO_HAVE_THUMTOO_LQIP)
QImage qimageFromLqipBlob(const std::vector<std::uint8_t> &blob);
#endif

/** Prefer EXIF/container JPEG underlay over ThumbHash LQIP; never tiles. */
static void putEmbeddedOrLqipUnderlay(const QString &path,
                                      const thumtoo::SizeReply &reply)
{
    if (path.isEmpty() || ImageCache::has(path)) {
        return;
    }
    if (reply.embedded && !reply.embedded->bytes.empty()) {
        QImage emb;
        if (emb.loadFromData(reply.embedded->bytes.data(),
                             static_cast<int>(reply.embedded->bytes.size()),
                             "JPEG")
            && !emb.isNull()) {
            // EMB = container EXIF or PDF /Thumb — not content tiles, not ThumbHash.
            ImageCache::put(path, emb, QStringLiteral("EMB"));
            return;
        }
    }
#if defined(BILTOO_HAVE_THUMTOO_LQIP)
    if (reply.lqip && !reply.lqip->empty()) {
        const QImage lqip = qimageFromLqipBlob(*reply.lqip);
        if (!lqip.isNull()) {
            ImageCache::put(path, lqip, QStringLiteral("LQIP"));
        }
    }
#endif
}


void requestSizeAsync(const QString &path,
                      std::function<void(bool ok, const QSize &size, const QImage &lqip)> callback)
{
    if (!callback) {
        return;
    }
    if (path.isEmpty()) {
        callback(false, QSize(), QImage());
        return;
    }
    auto run = [path, callback]() {
        ASSERT_NOT_GUI_THREAD();
        if (isUnsupported(path)) {
            callback(false, QSize(), QImage());
            return;
        }
        init();
        const std::string uri = toThumtooUri(path);
        if (uri.empty()) {
            callback(false, QSize(), QImage());
            return;
        }
        thumtoo::Client *c = nullptr;
        {
            std::lock_guard lock(g_mu);
            c = clientUnlocked();
        }
        if (!c) {
            callback(false, QSize(), QImage());
            return;
        }
        c->request_size(uri, [callback, path](std::string, thumtoo::SizeReply reply) {
            if (!reply.size) {
                callback(false, QSize(), QImage());
                return;
            }
            const QSize sz(reply.size->width, reply.size->height);
            // Do not ImageCache::put here — session replace may have cleared the
            // cache; finishProbeSlot installs underlay only when the probe
            // generation is still live.
            QImage underlay;
            if (reply.embedded && !reply.embedded->bytes.empty()) {
                underlay.loadFromData(reply.embedded->bytes.data(),
                                      static_cast<int>(reply.embedded->bytes.size()),
                                      "JPEG");
            }
#if defined(BILTOO_HAVE_THUMTOO_LQIP)
            if (underlay.isNull() && reply.lqip && !reply.lqip->empty()) {
                underlay = qimageFromLqipBlob(*reply.lqip);
            }
#endif
            callback(true, sz, underlay);
        });
    };
    if (QThread::isMainThread()) {
        QThreadPool::globalInstance()->start(run);
    } else {
        run();
    }
}


QSize cachedSize(const QString &path, bool scheduleRevalidate)
{
    if (path.isEmpty()) {
        return {};
    }
    if (const QSize memo = ProcessMemos::instance().size(path); memo.isValid()) {
        return memo;
    }
    // GUI: memo only — Store get_size is SQLite I/O (sizes_warm was ~12ms of this).
    if (QThread::isMainThread()) {
        return {};
    }
    ASSERT_NOT_GUI_THREAD();
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
        const QSize out(sz->width, sz->height);
        noteCachedSize(path, out);
        if (scheduleRevalidate) {
            scheduleBackgroundRevalidate(path, uri);
        }
        return out;
    }
    return {};
}

bool cachedFileStat(const QString &path, qint64 *sizeBytes, qint64 *mtimeNs)
{
    if (sizeBytes) {
        *sizeBytes = -1;
    }
    if (mtimeNs) {
        *mtimeNs = -1;
    }
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
    bool any = false;
    try {
        if (auto loc = c->store().find_locator(uri)) {
            if (loc->size && sizeBytes) {
                *sizeBytes = *loc->size;
                any = true;
            }
            if (loc->mtime_ns && mtimeNs) {
                *mtimeNs = *loc->mtime_ns;
                any = true;
            }
        }
    } catch (...) {
        return false;
    }
    return any;
}


#if defined(BILTOO_HAVE_THUMTOO_LQIP)
QImage qimageFromLqipBlob(const std::vector<std::uint8_t> &blob)
{
    if (blob.empty()) {
        return {};
    }
    auto rgba = thumtoo::lqip_decode_rgba(
        std::span<const std::uint8_t>(blob.data(), blob.size()));
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
}
#endif

QImage cachedLqipImage(const QString &path)
{
#if defined(BILTOO_HAVE_THUMTOO_LQIP)
    if (path.isEmpty()) {
        return {};
    }
    // Store get_lqip is I/O — GUI must use ImageCache (size probe mirrors LQIP).
    if (QThread::isMainThread()) {
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
    return qimageFromLqipBlob(*blob);
#else
    Q_UNUSED(path);
    return {};
#endif
}

QImage cachedEmbeddedPreviewImage(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    if (QThread::isMainThread()) {
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
    auto emb = c->get_embedded_preview(uri);
    if (!emb || emb->bytes.empty()) {
        return {};
    }
    QImage img;
    if (!img.loadFromData(emb->bytes.data(), static_cast<int>(emb->bytes.size()),
                          "JPEG")
        || img.isNull()) {
        return {};
    }
    return img;
}


bool isUnsupported(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    {
        std::lock_guard lock(g_mu);
        if (g_unsupportedYes.contains(path)) {
            return true;
        }
        if (g_unsupportedNo.contains(path)) {
            return false;
        }
    }
    // GUI: memo only — get_meta is SQLite I/O (Gallery open was N×get_meta via
    // scheduleTilePyramid/isUnsupported and blew GUI_BUDGET by hundreds of ms).
    if (QThread::isMainThread()) {
        return false;
    }
    ASSERT_NOT_GUI_THREAD();
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        std::lock_guard lock(g_mu);
        g_unsupportedNo.insert(path);
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
        const bool bad = meta->status == thumtoo::ContentStatus::Unsupported;
        std::lock_guard lock(g_mu);
        if (bad) {
            g_unsupportedYes.insert(path);
        } else {
            g_unsupportedNo.insert(path);
        }
        return bad;
    }
    {
        std::lock_guard lock(g_mu);
        g_unsupportedNo.insert(path);
    }
    return false;
}

QByteArray cachedLadderBytes(const QString &path, int maxEdge)
{
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
    thumtoo::RasterRequest req;
    req.uri = uri;
    req.max_edge = maxEdge;
    req.frame_idx = 0;
    req.policy = thumtoo::RasterPolicy::PreferCache;
    auto px = c->get_raster(req);
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
    return {};
}

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
        // Classify before worker runs: durable soft in host cache → retrieval;
        // otherwise this job will encode from file/archive (or grow the ladder).
        {
            const int have = ImageCache::longEdge(ImageCache::get(pathCopy));
            g_fetchKind.insert(pathCopy, (have >= kFilmstripLadderEdge) ? 1 : 2);
        }
        if (thumtooDebugEnabled()) {
            thumtooDbg("request_raster DISPATCH path=%s edge=%d active=%d queued=%zu",
                       qPrintable(pathCopy), edge, g_pixelsActive,
                       g_pixelsQueue.size());
        }
        auto onPixels = [pathCopy, edge, inflightKey](
                            std::string, int,
                            std::optional<thumtoo::PixelLevel> px) {
            QByteArray ba;
            int pxW = 0;
            int pxH = 0;
            int source = 0;
            if (px && !px->bytes.empty()) {
                pxW = px->width;
                pxH = px->height;
                source = static_cast<int>(px->source);
                ba = QByteArray(
                    reinterpret_cast<const char *>(px->bytes.data()),
                    int(px->bytes.size()));
            }
            auto finish = [pathCopy, edge, inflightKey, ba = std::move(ba),
                           pxW, pxH, source]() mutable {
                ASSERT_NOT_GUI_THREAD();
                QImage decoded;
                if (!ba.isEmpty()) {
                    decoded = ImageLoader::loadThumbnailFromBytes(ba, 0);
                    ImageCache::stampDebugOverlayIfEnabled(
                    &decoded,
                    QStringLiteral("%1 e=%2").arg(QFileInfo(pathCopy).fileName()).arg(edge));
                }
                {
                    std::lock_guard lock(g_mu);
                    g_pixelsInflight.remove(inflightKey);
                    const int got = decoded.isNull()
                                        ? 0
                                        : qMax(decoded.width(), decoded.height());
                    // PreferCache completion is terminal for this request edge —
                    // never require got >= fraction of edge (nondeterministic loops).
                    g_pixelsSettled.insert(inflightKey);
                    g_pixelsActive = qMax(0, g_pixelsActive - 1);
                    if (source != 0) {
                        g_lastPixelSource.insert(pathCopy, source);
                    }
                    {
                        const int kind = g_fetchKind.take(pathCopy);
                        if (kind == 1) {
                            ++g_recentCacheCompletions;
                        } else if (kind == 2) {
                            ++g_recentFileCompletions;
                        }
                    }
                    if (thumtooDebugEnabled()) {
                        thumtooDbg(
                            "request_raster DONE path=%s edge=%d plateau=%d src=%d "
                            "level=%dx%d decoded=%dx%d active=%d queued=%zu",
                            qPrintable(pathCopy), edge,
                            (got > 0 && got < edge) ? 1 : 0, source, pxW,
                            pxH, decoded.width(), decoded.height(),
                            g_pixelsActive, g_pixelsQueue.size());
                    }
                    startNextPixelJobsUnlocked();
                }
                emit bridge()->ladderReady(pathCopy, edge, decoded);
                emit bridge()->ladderProvenance(pathCopy, edge, source);
            };
            if (QThread::isMainThread()) {
                QThreadPool::globalInstance()->start(std::move(finish));
            } else {
                finish();
            }
        };
        thumtoo::RasterRequest req;
        req.uri = uri;
        req.max_edge = edge;
        req.frame_idx = 0;
        req.policy = thumtoo::RasterPolicy::PreferCache;
        c->request_raster(std::move(req), std::move(onPixels));
    }
}

void forgetPixelsSettled(const QString &path, int maxEdge)
{
    if (path.isEmpty() || maxEdge <= 0) {
        return;
    }
    // Soft uses path#edge; overview path#ov{edge}; display path#disp{edge}.
    // Clearing only the soft key left PreferCache stuck after a settled run.
    const QString softKey = path + QLatin1Char('#') + QString::number(maxEdge);
    const QString ovKey =
        path + QLatin1Char('#') + QStringLiteral("ov") + QString::number(maxEdge);
    const QString dispKey =
        path + QLatin1Char('#') + QStringLiteral("disp") + QString::number(maxEdge);
    const QString fullKey =
        path + QLatin1Char('#') + QStringLiteral("full") + QString::number(maxEdge);
    std::lock_guard lock(g_mu);
    g_pixelsSettled.remove(softKey);
    g_pixelsSettled.remove(ovKey);
    g_pixelsSettled.remove(dispKey);
    g_pixelsSettled.remove(fullKey);
}

QString queueStatsLabel()
{
    init();
    int hostActive = 0;
    int hostInflight = 0;
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
        hostActive = g_pixelsActive;
        hostInflight = g_pixelsInflight.size();
    }
    // End-user readable; only shown when THUMTOO_DEBUG is on.
    // Include host PreferCache / soft raster jobs — client queue_stats alone
    // reports idle while biltoo still waits on ladderReady.
    QStringList parts;
    if (c) {
        const auto s = c->queue_stats();
        if (s.pending > 0 || s.inflight > 0) {
            parts << QStringLiteral("%1 waiting · %2 decoding")
                         .arg(qulonglong(s.pending))
                         .arg(s.inflight);
        }
        if (s.focus_full_inflight > 0) {
            parts << QStringLiteral("focus decode");
        }
    }
    if (hostActive > 0 || hostInflight > 0) {
        parts << QStringLiteral("host raster %1/%2")
                     .arg(hostActive)
                     .arg(hostInflight);
    }
    if (parts.isEmpty()) {
        return QStringLiteral("decoder idle");
    }
    return parts.join(QStringLiteral(" · "));
}

WorkActivity workActivity()
{
    WorkActivity out;
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return out;
    }
    const thumtoo::ActivitySnapshot s = c->activity_snapshot();
    out.sizeQueued = static_cast<quint64>(s.size_probe_queued);
    out.sizeRunning = static_cast<quint64>(s.size_probe_running);
    for (const std::string &u : s.running_size_probe_uris) {
        out.sizeRunningUris.append(QString::fromStdString(u));
    }
    out.archiveReadRunning = static_cast<quint64>(s.archive_read_running);
    for (const std::string &lab : s.running_archive_labels) {
        out.runningArchiveLabels.append(QString::fromStdString(lab));
    }
    out.softQueued = static_cast<quint64>(s.soft_queued);
    out.softRunning = static_cast<quint64>(s.soft_running);
    for (const std::string &u : s.running_soft_uris) {
        out.softRunningUris.append(QString::fromStdString(u));
    }
    out.tileQueued = static_cast<quint64>(s.tile_queued);
    out.tileRunning = static_cast<quint64>(s.tile_running);
    for (const std::string &lab : s.running_tile_labels) {
        out.tileRunningLabels.append(QString::fromStdString(lab));
    }
    return out;
}

SizeProbeActivity sizeProbeActivity()
{
    return workActivity();
}

bool workActivityBusy()
{
    const WorkActivity a = workActivity();
    return (a.sizeQueued + a.sizeRunning + a.archiveReadRunning
            + a.softQueued + a.softRunning + a.tileQueued + a.tileRunning) > 0;
}


QString loadingBreakdownLabel()
{
    init();
    int active = 0;
    int queued = 0;
    int cacheJobs = 0;
    int fileJobs = 0;
    int recentCache = 0;
    int recentFile = 0;
    {
        std::lock_guard lock(g_mu);
        active = g_pixelsActive;
        queued = int(g_pixelsQueue.size());
        for (auto it = g_fetchKind.cbegin(); it != g_fetchKind.cend(); ++it) {
            if (it.value() == 1) {
                ++cacheJobs;
            } else if (it.value() == 2) {
                ++fileJobs;
            }
        }
        recentCache = g_recentCacheCompletions;
        recentFile = g_recentFileCompletions;
        // Decay recent counters so the HUD stays current.
        g_recentCacheCompletions = g_recentCacheCompletions * 3 / 4;
        g_recentFileCompletions = g_recentFileCompletions * 3 / 4;
    }
    if (active <= 0 && queued <= 0 && cacheJobs <= 0 && fileJobs <= 0
        && recentCache <= 0 && recentFile <= 0) {
        return {};
    }
    QStringList parts;
    if (active > 0 || queued > 0) {
        parts << QObject::tr("%1 active").arg(active);
        if (queued > 0) {
            parts << QObject::tr("%1 queued").arg(queued);
        }
    }
    if (cacheJobs > 0 || recentCache > 0) {
        parts << QObject::tr("%1 from cache").arg(qMax(cacheJobs, recentCache));
    }
    if (fileJobs > 0 || recentFile > 0) {
        parts << QObject::tr("%1 from file/archive").arg(qMax(fileJobs, recentFile));
    }
    if (parts.isEmpty()) {
        return {};
    }
    return QObject::tr("Loading · %1").arg(parts.join(QStringLiteral(" · ")));
}

QString lastPixelSourceLabel(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    int source = 0;
    {
        std::lock_guard lock(g_mu);
        source = g_lastPixelSource.value(path, 0);
    }
    // How the last ladder/overview bytes were produced (debug-ish). Prefer
    // ImageView quality labels from on-screen pixels for end users.
    switch (source) {
    case 1:
        return QObject::tr("cache (soft ladder)");
    case 2:
        return QObject::tr("cache (embedded)");
    case 3:
        return QObject::tr("file decode");
    case 4:
        return QObject::tr("cache (tiles)");
    default:
        return {};
    }
}

// Legacy SoftOnly entry point — PreferCache soft band (TileSynth when tiles exist).
bool scheduleDisplayPixels(const QString &path, int maxEdge);

bool schedulePixels(const QString &path, int maxEdge)
{
    return scheduleDisplayPixels(path, maxEdge);
}

bool isPixelsPending(const QString &path, int maxEdge)
{
    if (maxEdge <= 0 || path.isEmpty()) {
        return false;
    }
    init();
    // Soft uses path#edge; overview uses path#ov{edge}. Check both.
    const QString softKey = path + QLatin1Char('#') + QString::number(maxEdge);
    const QString ovKey =
        path + QLatin1Char('#') + QStringLiteral("ov") + QString::number(maxEdge);
    const QString dispKey =
        path + QLatin1Char('#') + QStringLiteral("disp") + QString::number(maxEdge);
    std::lock_guard lock(g_mu);
    return g_pixelsInflight.contains(softKey) || g_pixelsInflight.contains(ovKey)
        || g_pixelsInflight.contains(dispKey);
}

bool scheduleDisplayPixels(const QString &path, int maxEdge)
{
    if (maxEdge <= 0 || isUnsupported(path)) {
        return false;
    }
    if (maxEdge > kImageLadderEdge) {
        maxEdge = kImageLadderEdge;
    }
    init();
    const QString inflightKey =
        path + QLatin1Char('#') + QStringLiteral("disp") + QString::number(maxEdge);
    // Warm host cache: already covering this edge → zero work. Anything that
    // still hits PreferCache/encode for a covered path is a bug.
    {
        const int have = ImageCache::longEdge(ImageCache::get(path));
        if (have > 0 && have >= maxEdge) {
            std::lock_guard lock(g_mu);
            g_pixelsSettled.insert(inflightKey);
            thumtooDbg("scheduleDisplay SKIP path=%s edge=%d (ImageCache have=%d)",
                       qPrintable(path), maxEdge, have);
            return false;
        }
    }
    {
        std::lock_guard lock(g_mu);
        if (g_pixelsInflight.contains(inflightKey)) {
            thumtooDbg("scheduleDisplay SKIP path=%s edge=%d (inflight)",
                       qPrintable(path), maxEdge);
            return false;
        }
        if (g_pixelsSettled.contains(inflightKey)) {
            // PreferCache already answered this edge (hit, plateau, or miss).
            thumtooDbg("scheduleDisplay SKIP path=%s edge=%d (settled)",
                       qPrintable(path), maxEdge);
            return false;
        }
        int &attempts = g_pixelsScheduleCount[inflightKey];
        ++attempts;
        if (attempts > kMaxPixelScheduleAttempts) {
            g_pixelsSettled.insert(inflightKey);
            thumtooDbg("scheduleDisplay LOOP-BREAK path=%s edge=%d attempts=%d",
                       qPrintable(path), maxEdge, attempts);
            return false;
        }
        g_pixelsInflight.insert(inflightKey);
        ++g_pixelsActive;
        thumtooDbg("scheduleDisplay queue path=%s edge=%d active=%d attempts=%d",
                   qPrintable(path), maxEdge, g_pixelsActive, attempts);
    }
    const QString pathCopy = path;
    const int edge = maxEdge;
    // URI + request_raster off GUI (archive URI conversion is not free).
    QThreadPool::globalInstance()->start([pathCopy, edge, inflightKey]() {
        ASSERT_NOT_GUI_THREAD();
        thumtoo::Client *c = nullptr;
        {
            std::lock_guard lock(g_mu);
            c = clientUnlocked();
        }
        if (!c) {
            std::lock_guard lock(g_mu);
            g_pixelsInflight.remove(inflightKey);
            g_pixelsActive = qMax(0, g_pixelsActive - 1);
            return;
        }
        const std::string uri = toThumtooUri(pathCopy);
        if (uri.empty()) {
            std::lock_guard lock(g_mu);
            g_pixelsInflight.remove(inflightKey);
            g_pixelsActive = qMax(0, g_pixelsActive - 1);
            return;
        }
        auto onDisplay = [pathCopy, edge, inflightKey](
                             std::string, int,
                             std::optional<thumtoo::PixelLevel> px) {
            QByteArray ba;
            int source = 0;
            if (px && !px->bytes.empty()) {
                source = static_cast<int>(px->source);
                ba = QByteArray(
                    reinterpret_cast<const char *>(px->bytes.data()),
                    int(px->bytes.size()));
            }
            auto finish = [pathCopy, edge, inflightKey, ba = std::move(ba),
                           source]() mutable {
                ASSERT_NOT_GUI_THREAD();
                QImage decoded;
                if (!ba.isEmpty()) {
                    decoded = ImageLoader::loadThumbnailFromBytes(ba, 0);
                    ImageCache::stampDebugOverlayIfEnabled(
                        &decoded,
                        QStringLiteral("%1 e=%2")
                            .arg(QFileInfo(pathCopy).fileName())
                            .arg(edge));
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
                    // PreferCache BestAvailable is terminal for this request edge.
                    // Requiring got≥90% of edge left soft 128 for req=256 unsettled
                    // and filmstrip re-scheduled forever (DEBUG_OVERLAY soft loop).
                    g_pixelsSettled.insert(inflightKey);
                    g_pixelsActive = qMax(0, g_pixelsActive - 1);
                    thumtooDbg(
                        "scheduleDisplay DONE path=%s edge=%d plateau=%d src=%d "
                        "decoded=%dx%d active=%d",
                        qPrintable(pathCopy), edge,
                        (got > 0 && got < edge) ? 1 : 0, source, decoded.width(),
                        decoded.height(), g_pixelsActive);
                    startNextPixelJobsUnlocked();
                }
                emit bridge()->ladderReady(pathCopy, edge, decoded);
                emit bridge()->ladderProvenance(pathCopy, edge, source);
            };
            QThreadPool::globalInstance()->start(std::move(finish));
        };
        thumtoo::RasterRequest req;
        req.uri = uri;
        req.max_edge = edge;
        req.frame_idx = 0;
        req.policy = thumtoo::RasterPolicy::PreferCache;
        c->request_raster(std::move(req), std::move(onDisplay));
    });
    return true;
}


quint64 bumpInterestEpoch()
{
    init();
    std::lock_guard lock(g_mu);
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return 0;
    }
    // Drop host-side pixel queue so we do not keep dispatching stale paths.
    g_pixelsQueue.clear();
    return static_cast<quint64>(c->bump_interest_epoch());
}

int cancelTilesForPath(const QString &path)
{
    if (path.isEmpty()) {
        return 0;
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return 0;
    }
    std::lock_guard lock(g_mu);
    thumtoo::Client *c = clientUnlocked();
    if (!c) {
        return 0;
    }
    return static_cast<int>(c->cancel_uri(uri));
}

void purgePathDurable(const QString &path, std::function<void(qint64 tilesDeleted)> done)
{
    if (path.isEmpty()) {
        if (done) {
            done(0);
        }
        return;
    }
    // Process memos must not claim durable coverage or stale size after forget.
    ProcessMemos::instance().clearDurablePath(path);
    ProcessMemos::instance().clearSize(path);
    memberLruRemove(path);
    (void)cancelTilesForPath(path);
    init();
    const QString pathCopy = path;
    auto doneCopy = std::move(done);
    QThreadPool::globalInstance()->start([pathCopy, doneCopy]() {
        ASSERT_NOT_GUI_THREAD();
        qint64 tiles = 0;
        // Drop cached URI so a later resolve is not tied to a purged locator.
        {
            std::lock_guard lock(g_uriMu);
            g_uriBySessionPath.erase(pathCopy.toStdString());
        }
        thumtoo::Client *c = nullptr;
        {
            std::lock_guard lock(g_mu);
            c = clientUnlocked();
        }
        if (c) {
            // Archive / page / PDF-image session paths are Store-keyed by URI
            // (archive_uri, pdf_page_uri, …), not by a filesystem path.
            // purge_path only matches plain file outer_path — members stayed.
            const std::string uri = toThumtooUri(pathCopy);
            if (!uri.empty()) {
                const auto st = c->purge_uri(uri, /*dry_run=*/false);
                tiles += st.tiles_deleted;
                thumtooDbg("purgePathDurable uri=%s tiles=%lld levels=%lld",
                           uri.c_str(),
                           static_cast<long long>(st.tiles_deleted),
                           static_cast<long long>(st.levels_deleted));
            }
            // Plain files: also purge_path so outer_path locators without a
            // resolved URI still go cold.
            if (!ArchivePath::isArchiveRef(pathCopy) && !PagePath::isPageRef(pathCopy)
                && !PagePath::isPdfImageRef(pathCopy)) {
                const auto st2 = c->purge_path(
                    std::filesystem::path(pathCopy.toStdString()), /*dry_run=*/false);
                tiles += st2.tiles_deleted;
            }
        }
        if (!doneCopy) {
            return;
        }
        QCoreApplication *app = QCoreApplication::instance();
        if (!app) {
            doneCopy(tiles);
            return;
        }
        QMetaObject::invokeMethod(app, [doneCopy, tiles]() { doneCopy(tiles); },
                                  Qt::QueuedConnection);
    });
}

quint64 setInterest(const QStringList &pathsNear, const QStringList &pathsSpeculative,
                    int nearEdge, int speculativeEdge,
                    const QStringList &pathsPrimary, int primaryEdge)
{
    init();
    QString key;
    key.reserve(256);
    key += QString::number(nearEdge);
    key += QLatin1Char('|');
    key += QString::number(speculativeEdge);
    key += QLatin1Char('|');
    key += QString::number(primaryEdge);
    key += QLatin1Char('#');
    for (const QString &p : pathsPrimary) {
        key += p;
        key += QLatin1Char(';');
    }
    key += QLatin1Char('#');
    for (const QString &p : pathsNear) {
        key += p;
        key += QLatin1Char(';');
    }
    key += QLatin1Char('#');
    for (const QString &p : pathsSpeculative) {
        key += p;
        key += QLatin1Char(';');
    }
    {
        std::lock_guard lock(g_mu);
        if (key == g_lastInterestKey) {
            return 0;
        }
        g_lastInterestKey = key;
    }
    // Client::set_interest can take hundreds of ms (cancel/reschedule). Never
    // run it on the GUI — BILTOO_PERF showed interest=300–1000ms while pass1/2
    // were <1ms. Worker + job generation drops superseded scroll updates.
    const quint64 job = ++g_interestJobGen;
    const QStringList nearCopy = pathsNear;
    const QStringList specCopy = pathsSpeculative;
    const QStringList primaryCopy = pathsPrimary;
    const int nearE = nearEdge;
    const int specE = speculativeEdge;
    const int primE = primaryEdge;
    QThreadPool::globalInstance()->start([job, nearCopy, specCopy, primaryCopy, nearE, specE, primE]() {
        if (job != g_interestJobGen.load()) {
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
        std::vector<thumtoo::InterestItem> items;
        items.reserve(size_t(nearCopy.size() + specCopy.size() + primaryCopy.size()));
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
        push(primaryCopy, thumtoo::InterestRole::Primary,
             primE > 0 ? primE : kBatchOverviewEdge);
        push(nearCopy, thumtoo::InterestRole::Near, nearE);
        push(specCopy, thumtoo::InterestRole::Speculative, specE);
        if (job != g_interestJobGen.load()) {
            return;
        }
        (void)c->set_interest(std::move(items));
    });
    return job;
}

quint64 setPrimaryInterest(const QString &path, int edge)
{
    if (path.isEmpty() || isUnsupported(path)) {
        return 0;
    }
    init();
    if (edge <= 0) {
        edge = kBatchOverviewEdge;
    }
    // Primary is FocusFull / tile pyramid — allow up to kImageLadderEdge,
    // not FastBatch overview max (1024). Clamping to 1024 left Gallery stuck at
    // need above overview when primary.
    if (edge > kImageLadderEdge) {
        edge = kImageLadderEdge;
    }
    const QString key = QStringLiteral("P|") + QString::number(edge) + QLatin1Char('|') + path;
    {
        std::lock_guard lock(g_mu);
        if (key == g_lastInterestKey) {
            return 0;
        }
        g_lastInterestKey = key;
    }
    const quint64 job = ++g_interestJobGen;
    const QString pathCopy = path;
    const int edgeCopy = edge;
    QThreadPool::globalInstance()->start([job, pathCopy, edgeCopy]() {
        if (job != g_interestJobGen.load()) {
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
        const std::string uri = toThumtooUri(pathCopy);
        if (uri.empty()) {
            return;
        }
        thumtoo::InterestItem it;
        it.uri = uri;
        it.target_long_edge = edgeCopy;
        it.role = thumtoo::InterestRole::Primary;
        std::vector<thumtoo::InterestItem> items;
        items.push_back(std::move(it));
        if (job != g_interestJobGen.load()) {
            return;
        }
        (void)c->set_interest(std::move(items));
    });
    return job;
}


bool scheduleTilePyramid(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    if (sizeProbesBusy()) {
        return false;
    }
    if (hasDurableTilesKnown(path)) {
        return false;
    }
    // Cap concurrent FocusFull jobs (was unbounded pool starts → all cores busy).
    static std::atomic<int> pyramidInflight{0};
    constexpr int kMaxConcurrentPyramids = 2;
    int cur = pyramidInflight.load();
    while (cur < kMaxConcurrentPyramids) {
        if (pyramidInflight.compare_exchange_weak(cur, cur + 1)) {
            break;
        }
    }
    if (cur >= kMaxConcurrentPyramids) {
        return false;
    }
    init();
    const QString pathCopy = path;
    QThreadPool::globalInstance()->start([pathCopy]() {
        ASSERT_NOT_GUI_THREAD();
        struct SlotGuard {
            std::atomic<int> *counter;
            ~SlotGuard()
            {
                if (counter) {
                    counter->fetch_sub(1);
                }
            }
        } guard{&pyramidInflight};
        if (isUnsupported(pathCopy) || hasDurableTiles(pathCopy)) {
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
        const std::string uri = toThumtooUri(pathCopy);
        if (uri.empty()) {
            return;
        }
        ProcessMemos::instance().clearDurableNo(pathCopy);
        c->request_tile_pyramid(uri, /*min_scale=*/0, /*max_scale=*/-1, {});
        thumtooDbg("scheduleTilePyramid path=%s",
                   qPrintable(QFileInfo(pathCopy).fileName()));
    });
    return true;
}

bool hasDurableTiles(const QString &path)
{
    // Store has_tile — must not run on the GUI (use hasDurableTilesKnown there).
    ASSERT_NOT_GUI_THREAD();
    if (path.isEmpty() || isUnsupported(path)) {
        return false;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    ProcessMemos &memos = ProcessMemos::instance();
    // Positive / negative memos: tileLodWanted/paint/tick call this often.
    if (memos.durableYes(path)) {
        return true;
    }
    if (memos.durableNoActive(path, nowMs)) {
        return false;
    }
    init();
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return false;
    }
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return false;
    }
    // Durable pyramid on Store: origin tiles at scale 0 or 1, or coverage
    // min_scale origin when get_tile_coverage reports stored scales.
    bool yes = false;
    int minScale = 0;
    if (c->has_tile(uri, 0, 0, 0)) {
        yes = true;
        minScale = 0;
    } else if (c->has_tile(uri, 1, 0, 0)) {
        yes = true;
        minScale = 1;
    } else if (auto cov = c->get_tile_coverage(uri)) {
        if (c->has_tile(uri, cov->min_scale, 0, 0)) {
            yes = true;
            minScale = cov->min_scale;
        }
    }
    if (yes) {
        const bool first = !memos.durableYes(path);
        memos.noteDurableYes(path, minScale);
        if (first) {
            // GUI may already be deep-zoomed with the tile timer stopped; wake it.
            const QString pathCopy = path;
            QMetaObject::invokeMethod(
                bridge(),
                [pathCopy]() { emit bridge()->durableTilesReady(pathCopy); },
                Qt::QueuedConnection);
        }
    } else {
        memos.noteDurableNo(path, nowMs + kDurableTilesNegativeTtlMs);
    }
    return yes;
}

bool hasDurableTilesKnown(const QString &path)
{
    return ProcessMemos::instance().durableYes(path);
}

void warmSessionOpenMemos(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    const QStringList copy = paths;
    // Size + LQIP only. Durable has_tile used to run in the same pass and
    // contended with scheduleProbeBatch / request_size on the Store — size
    // resolve felt like "tiles in parallel" and slowed the gate.
    auto workOneSize = [](const QString &p) {
        if (p.isEmpty()) {
            return;
        }
        // Fill process size memo (+ LQIP into ImageCache). Do NOT emit sizeReady
        // per path — concurrent warm + scheduleProbeBatch double-flooded the
        // GUI with hundreds of handlers. Gate progress timer sweeps memos;
        // scheduleProbeBatch delivers memo hits in chunks.
        (void)cachedSize(p, /*scheduleRevalidate=*/false);
        if (!ImageCache::has(p)) {
            // Prefer durable EMB (EXIF / PDF /Thumb) over ThumbHash LQIP.
            const QImage emb = cachedEmbeddedPreviewImage(p);
            if (!emb.isNull()) {
                ImageCache::put(p, emb, QStringLiteral("EMB"));
            }
#if defined(BILTOO_HAVE_THUMTOO_LQIP)
            else {
                const QImage lqip = cachedLqipImage(p);
                if (!lqip.isNull()) {
                    ImageCache::put(p, lqip, QStringLiteral("LQIP"));
                }
            }
#endif
        }
    };
    auto workOneDurable = [](const QString &p) {
        if (p.isEmpty()) {
            return;
        }
        // durableTilesReady fires on first positive hasDurableTiles hit.
        (void)hasDurableTiles(p);
    };
    // Sequential on the global QThreadPool — never spawn std::thread workers.
    // Nested pool wait would deadlock when already on a pool thread; raw
    // threads stacked with per-path scheduleTilePyramid jobs under cold open.
    auto workAll = [copy, workOneSize, workOneDurable]() {
        ASSERT_NOT_GUI_THREAD();
        init();
        // Pass 1: sizes (+ LQIP) so the Gallery size gate can settle quickly.
        for (const QString &p : copy) {
            workOneSize(p);
        }
        // Pass 2: durable has_tile after size probes drain. Separate pool job
        // (do not join here) so this slot frees for request_size workers.
        QThreadPool::globalInstance()->start([copy, workOneDurable]() {
            ASSERT_NOT_GUI_THREAD();
            for (int i = 0; i < 500 && sizeProbesBusy(); ++i) {
                QThread::msleep(20);
            }
            for (const QString &p : copy) {
                workOneDurable(p);
            }
        });
    };
    // Never join Store warm on the GUI thread. Session-replace clears durable
    // memos (1234) so every Open paid a full cold warm wall and froze the UI.
    // finishApplyExpandedLoad already has a sizes_cold probe path; sizeReady /
    // durableTilesReady drive pack as memos land.
    if (QThread::isMainThread()) {
        QThreadPool::globalInstance()->start(workAll);
    } else {
        workAll();
    }
}

void clearSessionReplaceMemos()
{
    ProcessMemos::instance().clearSessionReplaceDurable();
    {
        std::lock_guard lock(g_uriMu);
        g_uriBySessionPath.clear();
    }
}

int durableTileMinScale(const QString &path)
{
    // Memo only — discovery is warmSessionOpenMemos / hasDurableTiles on workers.
    // Paint path calls this on the GUI; must never has_tile here.
    ProcessMemos &memos = ProcessMemos::instance();
    if (!memos.durableYes(path)) {
        return 0;
    }
    return memos.durableMinScale(path);
}

bool scheduleTileSynthOrPyramid(const QString &path, int maxEdge)
{
    if (path.isEmpty() || maxEdge <= 0) {
        return false;
    }
    if (!cachedSize(path).isValid()) {
        scheduleProbe(path);
    }
    if (hasDurableTilesKnown(path)) {
        return scheduleDisplayPixels(path, maxEdge)
            || isPixelsPending(path, maxEdge);
    }
    return scheduleTilePyramid(path);
}

bool scheduleSoftPixels(const QString &path, int maxEdge)
{
    if (maxEdge <= 0) {
        return false;
    }
    // Deprecated product path — redirect to tiles/TileSynth (no soft encode).
    return scheduleTileSynthOrPyramid(path, maxEdge);
}

void preparePaths(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    // Filter on the caller thread (cheap, no source I/O): skip unsupported,
    // compound refs, and paths that already have a durable size. Then run the
    // remaining prepare_paths work off the GUI — is_regular_file + get_meta used
    // to block "Opening N images…" for every warm open.
    QStringList need;
    need.reserve(paths.size());
    for (const QString &p : paths) {
        if (p.isEmpty()) {
            continue;
        }
        // Never isUnsupported on the GUI (Store get_meta). Worker prepare skips
        // unsupported after probe/get_meta.
        if (ArchivePath::isArchiveRef(p) || PagePath::isPageRef(p)
            || PagePath::isPdfImageRef(p) || PagePath::isPdfImagesCollection(p)) {
            continue;
        }
        if (cachedSize(p).isValid()) {
            continue;
        }
        need.append(p);
    }
    if (need.isEmpty()) {
        return;
    }
    // Capture session-work epoch so a superseded prepare_paths cannot refill
    // ImageCache / emit sizeReady after cancelSizeProbes (session replace).
    const quint64 generation = sizeProbeGeneration();
    QThreadPool::globalInstance()->start([need, generation]() {
        if (generation != sizeProbeGeneration()) {
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
        fsPaths.reserve(size_t(need.size()));
        QStringList plainPaths;
        plainPaths.reserve(need.size());
        for (const QString &p : need) {
            // Worker may still hit is_regular_file inside prepare_paths.
            fsPaths.emplace_back(absPathStd(p));
            plainPaths.append(p);
        }
        if (fsPaths.empty()) {
            return;
        }
        c->prepare_paths(fsPaths, [plainPaths, generation](std::string uri, thumtoo::SizeReply reply) {
            if (generation != sizeProbeGeneration()) {
                return;
            }
            QString path;
            for (const QString &p : plainPaths) {
                if (toThumtooUri(p) == uri) {
                    path = p;
                    break;
                }
            }
            if (path.isEmpty() || !reply.size) {
                return;
            }
            putEmbeddedOrLqipUnderlay(path, reply);
            emit bridge()->sizeReady(path, QSize(reply.size->width, reply.size->height));
        });
    });
}

void warmUris(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    // Copy list — caller may mutate session before the worker runs.
    const QStringList copy = paths;
    QThreadPool::globalInstance()->start([copy]() {
        init();
        for (const QString &p : copy) {
            if (p.isEmpty() || isUnsupported(p)) {
                continue;
            }
            (void)toThumtooUri(p);
        }
    });
}

void requestTiles(const QString &path, const QVector<TileCoord> &coords,
                  TileBitmapCellCallback on_cell)
{
    if (!on_cell || coords.isEmpty()) {
        return;
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        for (int i = 0; i < coords.size(); ++i) {
            on_cell(static_cast<std::size_t>(i), std::nullopt);
        }
        return;
    }
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        for (int i = 0; i < coords.size(); ++i) {
            on_cell(static_cast<std::size_t>(i), std::nullopt);
        }
        return;
    }
    std::vector<thumtoo::Client::TileCoord> tc;
    tc.reserve(static_cast<size_t>(coords.size()));
    for (const TileCoord &t : coords) {
        tc.push_back({t.scale, t.x, t.y});
    }
    // Decode once to rgba8 TileBitmap — no QImage intermediate for the RAM cache.
    c->request_tiles(uri, std::move(tc),
                     [on_cell](std::size_t index, std::optional<thumtoo::TileBlob> tile) {
                         if (!tile) {
                             on_cell(index, std::nullopt);
                             return;
                         }
                         on_cell(index, tilelod::decode_tile_payload(
                                            tile->width, tile->height, tile->codec,
                                            tile->bytes));
                     });
}

bool isAvailable()
{
    init();
    std::lock_guard lock(g_mu);
    return clientUnlocked() != nullptr;
}

QStringList expandArchiveToImageRefs(const QString &archivePath, bool *fromStore)
{
    QStringList out;
    if (fromStore) {
        *fromStore = false;
    }
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
    // Prefer durable Store TOC (instant on warm cache). Re-read the archive only
    // when the index has no members. Do not force-refresh RAR/CBR on every open.
    if (!entries.empty()) {
        if (fromStore) {
            *fromStore = true;
        }
    } else {
        entries = c->refresh_archive_toc(abs);
        if (fromStore) {
            *fromStore = false;
        }
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
    init();
    std::optional<int> count;
    {
        std::lock_guard lock(g_mu);
        thumtoo::Client *c = clientUnlocked();
        if (c) {
            count = c->document_page_count(abs, thumtoo::Client::DocumentKind::Pdf);
        }
        if (!count) {
            count = thumtoo::pdf_page_count(abs);
        }
    }
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
#if defined(BILTOO_HAVE_THUMTOO_PDF)
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
#if defined(BILTOO_HAVE_THUMTOO_EPUB)
    if (epubPath.isEmpty()) {
        return out;
    }
    const std::filesystem::path abs = absPathStd(epubPath);
    if (!thumtoo::is_likely_epub_path(abs)) {
        return out;
    }
    // Cache-first page count (thumtoo ≥ 202 document_index); fall back to
    // expand_media_uris / source epub_page_count on older trees.
    const auto layout = thumtoo::default_epub_layout();
    init();
    std::optional<int> count;
    {
        std::lock_guard lock(g_mu);
        thumtoo::Client *c = clientUnlocked();
        if (c) {
            count = c->document_page_count(abs, thumtoo::Client::DocumentKind::Epub,
                                           &layout);
        }
    }
    if (count && *count > 0) {
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
        return out;
    }
#if defined(BILTOO_HAVE_THUMTOO_EXPAND)
    auto uris = thumtoo::expand_media_uris(abs, 512);
    out.reserve(static_cast<int>(uris.size()));
    for (const auto &uri : uris) {
        auto parsed = thumtoo::parse_epub_uri(uri);
        if (!parsed) {
            continue;
        }
        const QString layoutStr = QString::fromStdString(
            thumtoo::format_epub_layout_params(parsed->layout));
        const QString ref = PagePath::makeEpubRef(
            QString::fromStdString(parsed->epub_path.string()), parsed->page, layoutStr);
        if (!ref.isEmpty()) {
            out.append(ref);
        }
    }
#else
    count = thumtoo::epub_page_count(abs, layout);
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
#if defined(BILTOO_HAVE_THUMTOO_DJVU)
    if (djvuPath.isEmpty()) {
        return out;
    }
    const std::filesystem::path abs = absPathStd(djvuPath);
    if (!thumtoo::is_likely_djvu_path(abs)) {
        return out;
    }
    init();
    std::optional<int> count;
    {
        std::lock_guard lock(g_mu);
        thumtoo::Client *c = clientUnlocked();
        if (c) {
            count = c->document_page_count(abs, thumtoo::Client::DocumentKind::Djvu);
        }
        if (!count) {
            count = thumtoo::djvu_page_count(abs);
        }
    }
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


QImage rasterizePageRef(const QString &sessionPath, int maxEdge)
{
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
}

QByteArray readArchiveMemberBytes(const QString &archiveRefPath)
{
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
}



#if defined(BILTOO_HAVE_THUMTOO_TEXT)

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



#if defined(BILTOO_HAVE_THUMTOO_APPEARANCE)

namespace {

bool appearanceDebug()
{
    static const bool on = qEnvironmentVariableIsSet("BILTOO_DEBUG_APPEARANCE");
    return on;
}

void appearanceLog(const QString &msg)
{
    qWarning().noquote() << QStringLiteral("[appearance]") << msg;
}

/** Biltoo-owned appearance DB keyed by thumtoo locator.id (not content hashes). */
sqlite3 *appearanceDb()
{
    static sqlite3 *db = []() -> sqlite3 * {
        QString root;
        if (const char *xdg = std::getenv("XDG_STATE_HOME"); xdg && xdg[0]) {
            root = QString::fromLocal8Bit(xdg) + QStringLiteral("/biltoo");
        } else if (const char *home = std::getenv("HOME"); home && home[0]) {
            root = QString::fromLocal8Bit(home)
                + QStringLiteral("/.local/state/biltoo");
        } else {
            root = QStringLiteral(".local/state/biltoo");
        }
        QDir().mkpath(root);
        const QString path = root + QStringLiteral("/locator_appearance.sqlite3");
        sqlite3 *out = nullptr;
        if (sqlite3_open(path.toUtf8().constData(), &out) != SQLITE_OK) {
            if (out) {
                sqlite3_close(out);
            }
            return nullptr;
        }
        sqlite3_exec(out, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(out, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);
        const char *ddl =
            "CREATE TABLE IF NOT EXISTS locator_appearance ("
            "  locator_id INTEGER PRIMARY KEY,"
            "  updated_unix INTEGER NOT NULL,"
            "  content_h_flip INTEGER NOT NULL DEFAULT 0,"
            "  content_v_flip INTEGER NOT NULL DEFAULT 0,"
            "  content_quarter_turns INTEGER NOT NULL DEFAULT 0,"
            "  has_crop INTEGER NOT NULL DEFAULT 0,"
            "  crop_x INTEGER,"
            "  crop_y INTEGER,"
            "  crop_w INTEGER,"
            "  crop_h INTEGER,"
            "  crop_source_w INTEGER,"
            "  crop_source_h INTEGER,"
            "  crop_rotation REAL NOT NULL DEFAULT 0,"
            "  grade_brightness INTEGER,"
            "  grade_contrast INTEGER,"
            "  grade_saturation INTEGER,"
            "  grade_hue INTEGER,"
            "  grade_gamma INTEGER,"
            "  grade_invert INTEGER"
            ");";
        if (sqlite3_exec(out, ddl, nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_close(out);
            return nullptr;
        }
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("db open %1").arg(path));
        }
        return out;
    }();
    return db;
}

/**
 * Resolve session path → thumtoo Store locator.id.
 * No file hashing: identity is the locator row that already represents the URI.
 */
std::optional<std::int64_t> locatorIdForPath(const QString &path)
{
    if (path.isEmpty()) {
        return std::nullopt;
    }
    // Session path → URI string cache (cheap).
    static QHash<QString, std::int64_t> idCache;
    static std::mutex idMu;
    {
        std::lock_guard lock(idMu);
        const auto it = idCache.constFind(path);
        if (it != idCache.cend() && it.value() > 0) {
            return it.value();
        }
    }
    init();
    const std::string uri = toThumtooUri(path);
    if (uri.empty()) {
        return std::nullopt;
    }
    thumtoo::Client *c = nullptr;
    {
        std::lock_guard lock(g_mu);
        c = clientUnlocked();
    }
    if (!c) {
        return std::nullopt;
    }
    std::optional<std::int64_t> id;
    try {
        if (auto loc = c->store().find_locator(uri)) {
            if (loc->id > 0) {
                id = loc->id;
            }
        }
    } catch (...) {
        return std::nullopt;
    }
    if (id) {
        std::lock_guard lock(idMu);
        idCache.insert(path, *id);
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("locator id=%1 path=%2")
                              .arg(*id)
                              .arg(path));
        }
    }
    return id;
}

} // namespace

bool loadContentAppearance(const QString &path, StoredContentAppearance *out)
{
    if (!out) {
        return false;
    }
    *out = StoredContentAppearance{};
    const auto lid = locatorIdForPath(path);
    if (!lid) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("load SKIP: no locator path=%1").arg(path));
        }
        return false;
    }
    sqlite3 *db = appearanceDb();
    if (!db) {
        return false;
    }
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(
            db,
            "SELECT content_h_flip, content_v_flip, content_quarter_turns,"
            " has_crop, crop_x, crop_y, crop_w, crop_h,"
            " crop_source_w, crop_source_h, crop_rotation,"
            " grade_brightness, grade_contrast, grade_saturation,"
            " grade_hue, grade_gamma, grade_invert"
            " FROM locator_appearance WHERE locator_id = ?1;",
            -1, &st, nullptr)
        != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, *lid);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("load MISS locator=%1 path=%2")
                              .arg(*lid)
                              .arg(path));
        }
        return false;
    }
    out->contentHFlip = sqlite3_column_int(st, 0) != 0;
    out->contentVFlip = sqlite3_column_int(st, 1) != 0;
    out->contentQuarterTurns = sqlite3_column_int(st, 2);
    out->hasCrop = sqlite3_column_int(st, 3) != 0;
    if (out->hasCrop) {
        out->cropRect = QRect(sqlite3_column_int(st, 4), sqlite3_column_int(st, 5),
                              sqlite3_column_int(st, 6), sqlite3_column_int(st, 7));
        out->cropSourceSize =
            QSize(sqlite3_column_int(st, 8), sqlite3_column_int(st, 9));
        out->cropRotation = sqlite3_column_double(st, 10);
    }
    const bool anyGrade = sqlite3_column_type(st, 11) != SQLITE_NULL
        || sqlite3_column_type(st, 12) != SQLITE_NULL
        || sqlite3_column_type(st, 13) != SQLITE_NULL
        || sqlite3_column_type(st, 14) != SQLITE_NULL
        || sqlite3_column_type(st, 15) != SQLITE_NULL
        || sqlite3_column_type(st, 16) != SQLITE_NULL;
    if (anyGrade) {
        out->hasGrade = true;
        out->gradeBrightness =
            sqlite3_column_type(st, 11) != SQLITE_NULL ? sqlite3_column_int(st, 11) : 0;
        out->gradeContrast =
            sqlite3_column_type(st, 12) != SQLITE_NULL ? sqlite3_column_int(st, 12) : 100;
        out->gradeSaturation =
            sqlite3_column_type(st, 13) != SQLITE_NULL ? sqlite3_column_int(st, 13) : 100;
        out->gradeHue =
            sqlite3_column_type(st, 14) != SQLITE_NULL ? sqlite3_column_int(st, 14) : 0;
        out->gradeGamma =
            sqlite3_column_type(st, 15) != SQLITE_NULL ? sqlite3_column_int(st, 15) : 100;
        out->gradeInvert =
            sqlite3_column_type(st, 16) != SQLITE_NULL && sqlite3_column_int(st, 16) != 0;
    }
    sqlite3_finalize(st);
    if (appearanceDebug()) {
        appearanceLog(QStringLiteral("load HIT locator=%1 path=%2").arg(*lid).arg(path));
    }
    return true;
}

void saveContentAppearance(const QString &path, const StoredContentAppearance &app)
{
    const auto lid = locatorIdForPath(path);
    if (!lid) {
        if (appearanceDebug()) {
            appearanceLog(QStringLiteral("save SKIP: no locator path=%1").arg(path));
        }
        return;
    }
    sqlite3 *db = appearanceDb();
    if (!db) {
        return;
    }
    if (app.isIdentity()) {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(
                db, "DELETE FROM locator_appearance WHERE locator_id = ?1;", -1, &st,
                nullptr)
            == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, *lid);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        return;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(
            db,
            "INSERT INTO locator_appearance("
            " locator_id, updated_unix, content_h_flip, content_v_flip,"
            " content_quarter_turns, has_crop, crop_x, crop_y, crop_w, crop_h,"
            " crop_source_w, crop_source_h, crop_rotation,"
            " grade_brightness, grade_contrast, grade_saturation, grade_hue,"
            " grade_gamma, grade_invert)"
            " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19)"
            " ON CONFLICT(locator_id) DO UPDATE SET"
            " updated_unix=excluded.updated_unix,"
            " content_h_flip=excluded.content_h_flip,"
            " content_v_flip=excluded.content_v_flip,"
            " content_quarter_turns=excluded.content_quarter_turns,"
            " has_crop=excluded.has_crop,"
            " crop_x=excluded.crop_x, crop_y=excluded.crop_y,"
            " crop_w=excluded.crop_w, crop_h=excluded.crop_h,"
            " crop_source_w=excluded.crop_source_w,"
            " crop_source_h=excluded.crop_source_h,"
            " crop_rotation=excluded.crop_rotation,"
            " grade_brightness=excluded.grade_brightness,"
            " grade_contrast=excluded.grade_contrast,"
            " grade_saturation=excluded.grade_saturation,"
            " grade_hue=excluded.grade_hue,"
            " grade_gamma=excluded.grade_gamma,"
            " grade_invert=excluded.grade_invert;",
            -1, &st, nullptr)
        != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int64(st, 1, *lid);
    sqlite3_bind_int64(st, 2, now);
    sqlite3_bind_int(st, 3, app.contentHFlip ? 1 : 0);
    sqlite3_bind_int(st, 4, app.contentVFlip ? 1 : 0);
    sqlite3_bind_int(st, 5, app.contentQuarterTurns);
    const bool crop = app.hasCrop && !app.cropRect.isEmpty();
    sqlite3_bind_int(st, 6, crop ? 1 : 0);
    if (crop) {
        sqlite3_bind_int(st, 7, app.cropRect.x());
        sqlite3_bind_int(st, 8, app.cropRect.y());
        sqlite3_bind_int(st, 9, app.cropRect.width());
        sqlite3_bind_int(st, 10, app.cropRect.height());
        sqlite3_bind_int(st, 11, app.cropSourceSize.width());
        sqlite3_bind_int(st, 12, app.cropSourceSize.height());
        sqlite3_bind_double(st, 13, app.cropRotation);
    } else {
        for (int i = 7; i <= 12; ++i) {
            sqlite3_bind_null(st, i);
        }
        sqlite3_bind_double(st, 13, 0.0);
    }
    if (app.hasGrade) {
        sqlite3_bind_int(st, 14, app.gradeBrightness);
        sqlite3_bind_int(st, 15, app.gradeContrast);
        sqlite3_bind_int(st, 16, app.gradeSaturation);
        sqlite3_bind_int(st, 17, app.gradeHue);
        sqlite3_bind_int(st, 18, app.gradeGamma);
        sqlite3_bind_int(st, 19, app.gradeInvert ? 1 : 0);
    } else {
        for (int i = 14; i <= 19; ++i) {
            sqlite3_bind_null(st, i);
        }
    }
    sqlite3_step(st);
    sqlite3_finalize(st);
    if (appearanceDebug()) {
        appearanceLog(QStringLiteral("save PUT locator=%1 path=%2").arg(*lid).arg(path));
    }
}

bool hasContentAppearance(const QString &path)
{
    StoredContentAppearance app;
    if (!loadContentAppearance(path, &app)) {
        return false;
    }
    return !app.isIdentity();
}

void clearContentAppearance(const QString &path)
{
    const auto lid = locatorIdForPath(path);
    if (!lid) {
        return;
    }
    sqlite3 *db = appearanceDb();
    if (!db) {
        return;
    }
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(
            db, "DELETE FROM locator_appearance WHERE locator_id = ?1;", -1, &st,
            nullptr)
        == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, *lid);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
}

#else

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




bool scheduleFullPixels(const QString &path, int maxEdge)
{
    if (path.isEmpty() || isUnsupported(path)) {
        return false;
    }
    init();
    if (maxEdge <= 0) {
        maxEdge = 8192;
    }
    const QString inflightKey =
        path + QLatin1Char('#') + QStringLiteral("full") + QString::number(maxEdge);
    {
        std::lock_guard lock(g_mu);
        if (g_pixelsInflight.contains(inflightKey)) {
            thumtooDbg("scheduleFull SKIP path=%s edge=%d (inflight)",
                       qPrintable(path), maxEdge);
            return true; // already in flight — count as accepted
        }
        if (g_pixelsSettled.contains(inflightKey)) {
            // Full was attempted once for this path#edge — always terminal.
            // Soft-tier RETRY looped when thumtoo returned mis-tagged Full at
            // 512px for PDF pages (request_full early-out on Source::Full).
            const int have = ImageCache::longEdge(ImageCache::get(path));
            thumtooDbg("scheduleFull SKIP path=%s edge=%d (settled have=%d)",
                       qPrintable(path), maxEdge, have);
            return true;
        }
        if (g_fullActive >= kMaxConcurrentFullJobs) {
            thumtooDbg("scheduleFull DEFER path=%s edge=%d fullActive=%d",
                       qPrintable(path), maxEdge, g_fullActive);
            return false;
        }
        g_pixelsInflight.insert(inflightKey);
        ++g_pixelsActive;
        ++g_fullActive;
        thumtooDbg("scheduleFull queue path=%s edge=%d active=%d fullActive=%d",
                   qPrintable(path), maxEdge, g_pixelsActive, g_fullActive);
    }
    // URI resolve + request_full_pixels off the GUI (archive URI can be slow).
    const QString pathCopy = path;
    const int edge = maxEdge;
    QThreadPool::globalInstance()->start([pathCopy, edge, inflightKey]() {
        ASSERT_NOT_GUI_THREAD();
        thumtoo::Client *c = nullptr;
        {
            std::lock_guard lock(g_mu);
            c = clientUnlocked();
        }
        if (!c) {
            std::lock_guard lock(g_mu);
            g_pixelsInflight.remove(inflightKey);
            g_pixelsActive = qMax(0, g_pixelsActive - 1);
            g_fullActive = qMax(0, g_fullActive - 1);
            return;
        }
        const std::string uri = toThumtooUri(pathCopy);
        if (uri.empty()) {
            std::lock_guard lock(g_mu);
            g_pixelsInflight.remove(inflightKey);
            g_pixelsActive = qMax(0, g_pixelsActive - 1);
            g_fullActive = qMax(0, g_fullActive - 1);
            return;
        }
        auto onFull = [pathCopy, edge, inflightKey](
                          std::string, int,
                          std::optional<thumtoo::PixelLevel> px) {
        QByteArray ba;
        int source = 0;
        if (px && !px->bytes.empty()) {
            source = static_cast<int>(px->source);
            ba = QByteArray(
                reinterpret_cast<const char *>(px->bytes.data()),
                int(px->bytes.size()));
        }
        auto finish = [pathCopy, edge, inflightKey, ba = std::move(ba),
                       source]() mutable {
            ASSERT_NOT_GUI_THREAD();
            QImage decoded;
            if (!ba.isEmpty()) {
                decoded = ImageLoader::loadThumbnailFromBytes(ba, 0);
                ImageCache::stampDebugOverlayIfEnabled(
                    &decoded,
                    QStringLiteral("%1 FULL e=%2")
                        .arg(QFileInfo(pathCopy).fileName())
                        .arg(edge));
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
                // Always settle: one full attempt per path#edge. Shortfall is
                // terminal (e.g. archive jpeg_shrink capped at overview 1024) —
                // re-queueing the same edge spun ladderReady → tryInstall REJECT
                // → scheduleFull forever.
                g_pixelsSettled.insert(inflightKey);
                g_pixelsActive = qMax(0, g_pixelsActive - 1);
                g_fullActive = qMax(0, g_fullActive - 1);
                thumtooDbg(
                    "scheduleFull DONE path=%s edge=%d ok=%d src=%d decoded=%dx%d "
                    "shortfall=%d active=%d fullActive=%d",
                    qPrintable(pathCopy), edge, decoded.isNull() ? 0 : 1, source,
                    decoded.width(), decoded.height(),
                    (got > 0 && got < edge) ? 1 : 0, g_pixelsActive,
                    g_fullActive);
                startNextPixelJobsUnlocked();
            }
            if (!decoded.isNull()) {
                ImageCache::put(pathCopy, decoded);
            }
            emit bridge()->ladderReady(pathCopy, edge, decoded);
            emit bridge()->ladderProvenance(pathCopy, edge, source);
        };
        QThreadPool::globalInstance()->start(finish, 0);
    };
        c->request_full_pixels(uri, edge, std::move(onFull));
    });
    return true;
}

} // namespace ThumtooCache
