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
class ImageSizeBook {
public:
    void clear()
    {
        m_byPath.clear();
        m_provisionalPaths.clear();
        m_failedPaths.clear();
        m_probeScheduled.clear();
    }

    bool contains(const QString &path) const
    {
        return !path.isEmpty() && m_byPath.contains(path);
    }

    bool isProvisional(const QString &path) const
    {
        return !path.isEmpty() && m_provisionalPaths.contains(path);
    }

    bool isFailed(const QString &path) const
    {
        return !path.isEmpty() && m_failedPaths.contains(path);
    }

    /**
     * Size probe failed. Record failure only — do **not** invent a stand-in
     * size (no 256² / 1000²). Gallery packs skip failed paths; Image waits
     * for a real SizeReply or stays empty.
     */
    void markFailed(const QString &path)
    {
        if (path.isEmpty()) {
            return;
        }
        m_failedPaths.insert(path);
        m_provisionalPaths.remove(path);
        // Drop any provisional stand-in; leave map empty if never definitive.
        if (!hasDefinitive(path)) {
            m_byPath.remove(path);
        }
    }


    bool hasDefinitive(const QString &path) const
    {
        return !path.isEmpty() && m_byPath.contains(path) && !isProvisional(path);
    }

    /** Map-only lookup (no thumtoo). Empty if absent or non-positive. */
    QSize known(const QString &path) const
    {
        const auto it = m_byPath.constFind(path);
        if (it == m_byPath.cend() || !isPositiveSize(*it)) {
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
        const auto it = m_byPath.constFind(path);
        if (it != m_byPath.cend() && isPositiveSize(*it) && !isProvisional(path)
            && isMuchSmallerArea(size, *it)) {
            return false;
        }
        m_byPath.insert(path, size);
        m_provisionalPaths.remove(path);
        m_failedPaths.remove(path);
        return true;
    }

    /** Stand-in geometry while a probe is outstanding. */
    /** Neutral stand-in while probe is outstanding (plain files). */
    static QSize standInNeutral() { return QSize(1000, 1000); }

    /** Square stand-in for archive/page refs (matches kProvisionalLayoutLongEdge). */
    static QSize standInSquare()
    {
        return QSize(kProvisionalLayoutLongEdge, kProvisionalLayoutLongEdge);
    }

    /** Prefer square for multipage/archive paths; neutral otherwise. */
    static QSize standInForCompoundPath(bool compoundRef)
    {
        return compoundRef ? standInSquare() : standInNeutral();
    }

    void markProvisional(const QString &path, const QSize &standIn)
    {
        if (path.isEmpty() || !isPositiveSize(standIn)) {
            return;
        }
        m_provisionalPaths.insert(path);
        m_byPath.insert(path, standIn);
    }

    void markProbeScheduled(const QString &path)
    {
        if (!path.isEmpty()) {
            m_probeScheduled.insert(path);
        }
    }

    void clearProbeScheduled(const QString &path)
    {
        m_probeScheduled.remove(path);
    }

    bool isProbeScheduled(const QString &path) const
    {
        return !path.isEmpty() && m_probeScheduled.contains(path);
    }

    /** Take known size for @p path; false if absent. Clears provisional flag. */
    bool take(const QString &path, QSize *out)
    {
        if (path.isEmpty() || !out) {
            return false;
        }
        const auto it = m_byPath.find(path);
        if (it == m_byPath.end()) {
            return false;
        }
        *out = *it;
        m_byPath.erase(it);
        m_provisionalPaths.remove(path);
        m_failedPaths.remove(path);
        m_probeScheduled.remove(path);
        return true;
    }

private:
    QHash<QString, QSize> m_byPath;
    QSet<QString> m_provisionalPaths;
    QSet<QString> m_failedPaths;
    QSet<QString> m_probeScheduled;
};

#endif // IMAGESIZEBOOK_H

