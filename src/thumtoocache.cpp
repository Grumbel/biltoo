// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"

#include "archivepath.h"
#include "imagecache.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>

#ifdef BILTOO_HAVE_THUMTOO
#include "thumtoo/archive.hpp"
#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/uri.hpp"

#include <memory>
#include <string>
#include <vector>
#endif

namespace ThumtooCache {
namespace {

#ifdef BILTOO_HAVE_THUMTOO

std::mutex g_mu;
std::unique_ptr<thumtoo::Client> g_client;
bool g_inited = false;

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

QImage decodeLadderBytes(const std::vector<std::uint8_t> &bytes)
{
    if (bytes.empty()) {
        return {};
    }
    const QByteArray ba(reinterpret_cast<const char *>(bytes.data()),
                        int(bytes.size()));
    QImage img;
    if (img.loadFromData(ba)) {
        return img;
    }
    return {};
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
            QImage img = decodeLadderBytes(px->bytes);
            if (img.isNull()) {
                return;
            }
            if (img.width() > edge || img.height() > edge) {
                img = img.scaled(edge, edge, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
            }
            ImageCache::put(pathCopy, img);
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

} // namespace ThumtooCache
