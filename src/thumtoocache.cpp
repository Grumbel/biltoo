// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"

#include "archivepath.h"

#include <QCoreApplication>
#include <QFileInfo>
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

#endif

} // namespace

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
    c->request_size(uri, {});
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
    c->request_pixels(uri, maxEdge, {});
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
    for (const QString &p : paths) {
        if (ArchivePath::isArchiveRef(p)) {
            // prepare_paths expands archives itself when given the container;
            // member refs are individual URIs — schedule size/pixels instead.
            scheduleProbe(p);
            schedulePixels(p, 512);
            continue;
        }
        const QFileInfo fi(p);
        if (!fi.exists()) {
            continue;
        }
        fsPaths.emplace_back(absPathStd(p));
    }
    if (!fsPaths.empty()) {
        c->prepare_paths(fsPaths, {});
    }
#else
    Q_UNUSED(paths);
#endif
}

} // namespace ThumtooCache
