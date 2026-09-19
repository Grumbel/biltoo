// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CENTREPROGRESS_H
#define CENTREPROGRESS_H

#include <QString>

/**
 * Centre-viewport progress panel (archive expand, size resolve, sort probes).
 * Suppresses the empty-session invite while title is non-empty.
 */
struct CentreProgress {
    QString title;
    QString detail;

    bool active() const { return !title.isEmpty(); }

    const QString &titleRef() const { return title; }

    const QString &detailRef() const { return detail; }

    bool hasDetail() const { return !detail.isEmpty(); }

    void clear()
    {
        title.clear();
        detail.clear();
    }

    /** @return true when title or detail changed. */
    bool set(const QString &t, const QString &d = QString())
    {
        if (title == t && detail == d) {
            return false;
        }
        title = t;
        detail = d;
        return true;
    }

    /** True when title starts with @p prefix (progress clear / paint gates). */
    bool matchesTitlePrefix(const QString &prefix) const
    {
        return !prefix.isEmpty() && title.startsWith(prefix);
    }
};

#endif // CENTREPROGRESS_H
