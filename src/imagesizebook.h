// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGESIZEBOOK_H
#define IMAGESIZEBOOK_H

#include "imageview_types.h"

#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>

/**
 * Session-scoped path → logical (native) pixel size.
 *
 * Soft / ladder sample dimensions must never redefine a known larger size
 * (SIZE.md / DOMAIN). Provisional stand-ins are tracked separately so the
 * size-first Gallery gate can wait for probes.
 *
 * Async schedule and canvas apply remain on ImageView; this bag is pure map
 * state only.
 */
struct ImageSizeBook {
    QHash<QString, QSize> byPath;
    QSet<QString> provisionalPaths;
    QSet<QString> probeScheduled;

    void clear()
    {
        byPath.clear();
        provisionalPaths.clear();
        probeScheduled.clear();
    }

    bool isProvisional(const QString &path) const
    {
        return !path.isEmpty() && provisionalPaths.contains(path);
    }

    bool hasDefinitive(const QString &path) const
    {
        return !path.isEmpty() && byPath.contains(path) && !isProvisional(path);
    }

    /** Map-only lookup (no thumtoo). Empty if absent or non-positive. */
    QSize known(const QString &path) const
    {
        const auto it = byPath.constFind(path);
        if (it == byPath.cend() || !isPositiveSize(*it)) {
            return {};
        }
        return *it;
    }

    /**
     * Install a definitive logical size. Rejects incoming samples that are
     * much smaller than a known non-provisional size (identity rule).
     * Clears provisional for @p path on success.
     * @return true when the map was updated.
     */
    bool noteDefinitive(const QString &path, const QSize &size)
    {
        if (path.isEmpty() || !isPositiveSize(size)) {
            return false;
        }
        const auto it = byPath.constFind(path);
        if (it != byPath.cend() && isPositiveSize(*it) && !isProvisional(path)
            && isMuchSmallerArea(size, *it)) {
            return false;
        }
        byPath.insert(path, size);
        provisionalPaths.remove(path);
        return true;
    }

    /** Stand-in geometry while a probe is outstanding. */
    void markProvisional(const QString &path, const QSize &standIn)
    {
        if (path.isEmpty() || !isPositiveSize(standIn)) {
            return;
        }
        provisionalPaths.insert(path);
        byPath.insert(path, standIn);
    }

    void markProbeScheduled(const QString &path)
    {
        if (!path.isEmpty()) {
            probeScheduled.insert(path);
        }
    }

    void clearProbeScheduled(const QString &path)
    {
        probeScheduled.remove(path);
    }

    bool isProbeScheduled(const QString &path) const
    {
        return !path.isEmpty() && probeScheduled.contains(path);
    }
};

#endif // IMAGESIZEBOOK_H
