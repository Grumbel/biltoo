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

    void clear()
    {
        title.clear();
        detail.clear();
    }

    void set(const QString &t, const QString &d = QString())
    {
        title = t;
        detail = d;
    }
};

#endif // CENTREPROGRESS_H
