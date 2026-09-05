// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMTOOCACHE_H
#define THUMTOOCACHE_H

#include <QSize>
#include <QString>

/**
 * Thin biltoo façade over thumtoo::Client (durable size index + ladder).
 * Compile-time optional: without BILTOO_HAVE_THUMTOO every call is a no-op.
 */
namespace ThumtooCache {

/** Open the default XDG cache client once (safe to call repeatedly). */
void init();

/** Cache-only native size for a session path (file or //archive: ref). */
QSize cachedSize(const QString &path);

/**
 * Schedule a background size probe (and ladder) when missing.
 * Does not block; does not drain the queue on the GUI thread.
 */
void scheduleProbe(const QString &path);

} // namespace ThumtooCache

#endif
