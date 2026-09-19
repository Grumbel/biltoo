// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWPORTUPDATEHOLD_H
#define VIEWPORTUPDATEHOLD_H

#include <QWidget>

/**
 * Disable QWidget updates for a critical section; restore and update on scope exit.
 * Used by crop enter/apply so intermediate paints do not flash partial draft state.
 */
struct ViewportUpdateHold {
    QWidget *viewport = nullptr;
    bool held = false;

    explicit ViewportUpdateHold(QWidget *vp)
        : viewport(vp)
        , held(vp && vp->updatesEnabled())
    {
        if (held) {
            viewport->setUpdatesEnabled(false);
        }
    }

    ~ViewportUpdateHold()
    {
        if (held && viewport) {
            viewport->setUpdatesEnabled(true);
            viewport->update();
        }
    }

    ViewportUpdateHold(const ViewportUpdateHold &) = delete;
    ViewportUpdateHold &operator=(const ViewportUpdateHold &) = delete;
};

#endif // VIEWPORTUPDATEHOLD_H
