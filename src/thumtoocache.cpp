// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoocache.h"

#include "archivepath.h"

#include <QFileInfo>

#include <cstdlib>
#include <filesystem>
#include <mutex>

#ifdef BILTOO_HAVE_THUMTOO
#include "thumtoo/archive.hpp"
#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/uri.hpp"

#include <memory>
#include <string>
#endif

namespace ThumtooCache {
namespace {

#ifdef BILTOO_HAVE_THUMTOO

std::mutex g_mu;
std::unique_ptr<thumtoo::Client> g_client;
bool g_inited = false;

std::string toThumtooUri(const QString &path)
{
    if (ArchivePath::isArchiveRef(path)) {
        const ArchivePath::Ref ref = ArchivePath::parse(path);
        if (!ref.valid) {
            return {};
        }
        const QFileInfo fi(ref.archivePath);
        const auto abs = fi.absoluteFilePath().toStdString();
        return thumtoo::archive_uri(abs, ref.memberPath.toStdString());
    }
    const QFileInfo fi(path);
    if (!fi.exists()) {
        return {};
    }
    return thumtoo::file_uri_from_path(fi.absoluteFilePath().toStdString());
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
    thumtoo::image_library_init();
    std::filesystem::path root;
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        root = std::filesystem::path(xdg) / "thumtoo";
    } else if (const char *home = std::getenv("HOME"); home && *home) {
        root = std::filesystem::path(home) / ".cache" / "thumtoo";
    } else {
        root = std::filesystem::path(".cache") / "thumtoo";
    }
    g_client = thumtoo::Client::open(root);
    g_inited = true;
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
    std::lock_guard lock(g_mu);
    if (!g_client) {
        return {};
    }
    if (auto sz = g_client->get_size(uri)) {
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
    std::lock_guard lock(g_mu);
    if (!g_client) {
        return;
    }
    g_client->request_size(std::move(uri), {});
#else
    Q_UNUSED(path);
#endif
}

} // namespace ThumtooCache
