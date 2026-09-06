// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THUMTOOCACHE_H
#define THUMTOOCACHE_H

#include <QByteArray>
#include <QSize>
#include <QString>

/**
 * Thin biltoo façade over thumtoo::Client (durable size index + ladder).
 * Compile-time optional: without BILTOO_HAVE_THUMTOO every call is a no-op.
 */
namespace ThumtooCache {

/** Open the default XDG cache client once (safe to call repeatedly). */
void init();

/** Drop the client (join thumtoo worker). Safe to call more than once. */
void shutdown();

/** Cache-only native size for a session path (file or //archive: ref). */
QSize cachedSize(const QString &path);

/**
 * Schedule a background size probe (and ladder) when missing.
 * Does not block; does not drain the queue on the GUI thread.
 */
void scheduleProbe(const QString &path);

/**
 * Cache-only ladder payload (usually JPEG-XL) with long edge <= maxEdge.
 * Empty if thumtoo has no level yet. Caller decodes (e.g. via libvips).
 */
QByteArray cachedLadderBytes(const QString &path, int maxEdge);

/**
 * Ensure ladder level exists for maxEdge (probe/encode in thumtoo worker).
 * Non-blocking; next cachedLadderBytes may succeed.
 */
void schedulePixels(const QString &path, int maxEdge);

} // namespace ThumtooCache

#endif
