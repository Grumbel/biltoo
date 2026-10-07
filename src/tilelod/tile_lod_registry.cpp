// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/tile_lod_registry.hpp"

#include "tilelod/core.hpp"
#include <thumtoo/lod/source_records.hpp>

#include "tilelod/thumtoo_tile_backend.hpp"
#include <thumtoo/lod/tile_scheduler.hpp>
#include "tilelod/tile_scheduler_qt.hpp"

#include <QCoreApplication>

#include <QString>
#include <QByteArray>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace tilelod {

TileLodRegistry& TileLodRegistry::instance()
{
  static TileLodRegistry reg;
  return reg;
}

void TileLodRegistry::touch_locked(SharedPathTiles& entry)
{
  entry.last_used = ++m_clock;
}

std::size_t TileLodRegistry::total_approx_bytes_locked() const
{
  std::size_t n = 0;
  for (auto const& [k, shared] : m_by_path) {
    (void)k;
    if (shared && shared->loader) {
      n += shared->loader->ready_bytes();
    }
  }
  return n;
}

void TileLodRegistry::trim_idle_locked()
{
  auto idle_count = [this]() {
    std::size_t n = 0;
    for (auto const& [key, shared] : m_by_path) {
      (void)key;
      if (shared && shared->refcount == 0) {
        ++n;
      }
    }
    return n;
  };
  const bool over_bytes = total_approx_bytes_locked() > m_global_budget;
  const bool over_count = idle_count() > m_max_idle_paths;
  if (!over_bytes && !over_count) {
    return;
  }
  // Candidates: zero-ref paths, oldest last_used first.
  std::vector<std::pair<std::uint64_t, std::string>> idle;
  idle.reserve(m_by_path.size());
  for (auto const& [key, shared] : m_by_path) {
    if (!shared || shared->refcount > 0) {
      continue;
    }
    idle.emplace_back(shared->last_used, key);
  }
  std::sort(idle.begin(), idle.end(),
            [](auto const& a, auto const& b) { return a.first < b.first; });
  for (auto const& [lu, key] : idle) {
    (void)lu;
    const bool still_over_bytes =
        total_approx_bytes_locked() > m_global_budget;
    const bool still_over_count = idle_count() > m_max_idle_paths;
    if (!still_over_bytes && !still_over_count) {
      break;
    }
    // Dropping the entry destroys ThumtooTileSource (epoch bump) and the
    // TileMemoryCache; in-flight completions become no-ops via session inbox.
    if (const char* td = std::getenv("BILTOO_TILE_DEBUG");
        td && td[0] && td[0] != '0') {
      std::fprintf(stderr, "biltoo/tile-reg: trim idle path=%s\n", key.c_str());
      std::fflush(stderr);
    }
    if (auto it = m_by_path.find(key); it != m_by_path.end() && it->second
        && it->second->loader) {
      it->second->loader->cancel_outstanding();
    }
    m_by_path.erase(key);
  }
}

std::shared_ptr<SharedPathTiles> TileLodRegistry::acquire(QString const& path)
{
  if (path.isEmpty()) {
    return {};
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it != m_by_path.end()) {
    ++it->second->refcount;
    touch_locked(*it->second);
    return it->second;
  }
  // Make room for a new path before inserting (prefer dropping idle first).
  trim_idle_locked();
  // Lazily install the GUI timer driver for the scheduler (any host —
  // app, tests, tools — gets event-driven pumping without extra wiring).
  if (!m_driver_installed && QCoreApplication::instance()) {
    installTileSchedulerQtDriver(QCoreApplication::instance());
    m_driver_installed = true;
  }
  auto shared = std::make_shared<SharedPathTiles>();
  shared->path = path;
  shared->loader = std::make_shared<TileLoader>(
      std::make_shared<ThumtooTileBackend>(path));
  TileScheduler::instance().register_loader(shared->loader);
  shared->refcount = 1;
  touch_locked(*shared);
  m_by_path.emplace(key, shared);
  return shared;
}

void TileLodRegistry::release(QString const& path)
{
  if (path.isEmpty()) {
    return;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end()) {
    return;
  }
  --it->second->refcount;
  if (it->second->refcount < 0) {
    it->second->refcount = 0;
  }
  touch_locked(*it->second);
  if (it->second->refcount > 0) {
    return;
  }
  // Idle: keep Succeeded tiles for A→B→A. Session dtor should already have
  // erased InFlight; clear any residual so we never retain a pure-InFlight shell.
  // No view holds demand any more: the loader cancels its Queued cells on
  // the next pump (or on destruction when erased below).
  bool const has_tiles =
      it->second->loader && it->second->loader->has_ready();
  if (!has_tiles) {
    if (it->second->loader) {
      it->second->loader->cancel_outstanding();
    }
    m_by_path.erase(it);
    return;
  }
  trim_idle_locked();
}

void TileLodRegistry::invalidate(QString const& path)
{
  if (path.isEmpty()) {
    return;
  }
  std::string const key = path.toStdString();
  std::shared_ptr<TileLoader> loader;
  {
    std::lock_guard<std::mutex> lock(m_mu);
    auto it = m_by_path.find(key);
    if (it == m_by_path.end()) {
      return;
    }
    if (it->second) {
      loader = it->second->loader;
      if (it->second->refcount == 0) {
        m_by_path.erase(it);
      }
    } else {
      m_by_path.erase(it);
    }
  }
  // What was learned about the old file no longer holds.
  SourceRecords::instance().forget(key);
  // Outside the lock: invalidate notifies views (repaint hooks).
  if (loader) {
    loader->invalidate("reload");
  }
}

void TileLodRegistry::notify_path_views(QString const& path)
{
  std::shared_ptr<TileLoader> loader;
  {
    std::lock_guard<std::mutex> lock(m_mu);
    auto it = m_by_path.find(path.toStdString());
    if (it == m_by_path.end() || !it->second) {
      return;
    }
    loader = it->second->loader;
  }
  if (loader) {
    loader->notify_views();
  }
}

void TileLodRegistry::invalidateAll()
{
  std::vector<std::shared_ptr<TileLoader>> loaders;
  {
    std::lock_guard<std::mutex> lock(m_mu);
    for (auto it = m_by_path.begin(); it != m_by_path.end();) {
      if (it->second && it->second->loader) {
        loaders.push_back(it->second->loader);
      }
      // Keep entries live controllers still hold: erasing them made a later
      // release() decrement a *new* entry for the same path (refcount drift,
      // two loaders for one path).
      if (!it->second || it->second->refcount == 0) {
        it = m_by_path.erase(it);
      } else {
        ++it;
      }
    }
  }
  SourceRecords::instance().clear();
  for (auto const& l : loaders) {
    l->invalidate("session replace");
  }
}

bool TileLodRegistry::has_succeeded_tiles(QString const& path) const
{
  return path_succeeded_count(path) > 0;
}

std::size_t TileLodRegistry::path_succeeded_count(QString const& path) const
{
  if (path.isEmpty()) {
    return 0;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end() || !it->second || !it->second->loader) {
    return 0;
  }
  return it->second->loader->ready_count();
}

std::size_t TileLodRegistry::idle_path_count() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  std::size_t n = 0;
  for (auto const& [k, shared] : m_by_path) {
    (void)k;
    if (shared && shared->refcount == 0) {
      ++n;
    }
  }
  return n;
}

std::size_t TileLodRegistry::path_approx_bytes(QString const& path) const
{
  if (path.isEmpty()) {
    return 0;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end() || !it->second || !it->second->loader) {
    return 0;
  }
  return it->second->loader->ready_bytes();
}

void TileLodRegistry::touch(QString const& path)
{
  if (path.isEmpty()) {
    return;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end() || !it->second) {
    return;
  }
  touch_locked(*it->second);
}

int TileLodRegistry::path_refcount(QString const& path) const
{
  if (path.isEmpty()) {
    return 0;
  }
  std::string const key = path.toStdString();
  std::lock_guard<std::mutex> lock(m_mu);
  auto it = m_by_path.find(key);
  if (it == m_by_path.end()) {
    return 0;
  }
  return it->second->refcount;
}

std::size_t TileLodRegistry::path_count() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return m_by_path.size();
}

std::size_t TileLodRegistry::total_approx_bytes() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return total_approx_bytes_locked();
}

void TileLodRegistry::set_global_budget_bytes(std::size_t bytes)
{
  std::lock_guard<std::mutex> lock(m_mu);
  m_global_budget = bytes > 0 ? bytes : kDefaultGlobalBudgetBytes;
  trim_idle_locked();
}

std::size_t TileLodRegistry::global_budget_bytes() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return m_global_budget;
}

void TileLodRegistry::set_max_idle_paths(std::size_t n)
{
  std::lock_guard<std::mutex> lock(m_mu);
  m_max_idle_paths = n > 0 ? n : kDefaultMaxIdlePaths;
  trim_idle_locked();
}

std::size_t TileLodRegistry::max_idle_paths() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  return m_max_idle_paths;
}

QString TileLodRegistry::debug_summary() const
{
  std::lock_guard<std::mutex> lock(m_mu);
  std::size_t idle = 0;
  for (auto const& [k, shared] : m_by_path) {
    (void)k;
    if (shared && shared->refcount == 0) {
      ++idle;
    }
  }
  const double mib =
      static_cast<double>(total_approx_bytes_locked()) / (1024.0 * 1024.0);
  TileScheduler::Stats const st = TileScheduler::instance().stats();
  return QStringLiteral("regPaths=%1 idle=%2 ramMiB=%3 maxIdle=%4 sched{loaders=%5 "
                        "queued=%6 waiting=%7 issued=%8}")
      .arg(m_by_path.size())
      .arg(idle)
      .arg(mib, 0, 'f', 1)
      .arg(m_max_idle_paths)
      .arg(st.loaders)
      .arg(st.queued)
      .arg(st.issuable)
      .arg(static_cast<qulonglong>(st.issued_total));
}

void TileLodRegistry::apply_environment_overrides()
{
  bool changed = false;
  if (const char* e = std::getenv("BILTOO_TILE_RAM_MIB");
      e && e[0]) {
    char* end = nullptr;
    const long mib = std::strtol(e, &end, 10);
    if (end != e && mib > 0 && mib < 1024 * 1024) {
      set_global_budget_bytes(static_cast<std::size_t>(mib) * 1024ull * 1024ull);
      changed = true;
    }
  }
  if (const char* e = std::getenv("BILTOO_TILE_MAX_IDLE");
      e && e[0]) {
    char* end = nullptr;
    const long n = std::strtol(e, &end, 10);
    if (end != e && n > 0 && n < 100000) {
      set_max_idle_paths(static_cast<std::size_t>(n));
      changed = true;
    }
  }
  if (changed) {
    if (const char* td = std::getenv("BILTOO_TILE_DEBUG");
        td && td[0] && td[0] != '0') {
      const QByteArray line = debug_summary().toUtf8();
      std::fprintf(stderr, "biltoo/tile-reg: env %s\n", line.constData());
      std::fflush(stderr);
    }
  }
}

}  // namespace tilelod
