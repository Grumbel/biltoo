// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOTIONSCROLLCHROME_H
#define MOTIONSCROLLCHROME_H

#include <Qt>

/**
 * Scrollbar policies saved while Ken Burns / motion underlay hides bars.
 * ImageView still applies QAbstractScrollArea policies.
 */
struct MotionScrollChrome {
    Qt::ScrollBarPolicy savedH = Qt::ScrollBarAsNeeded;
    Qt::ScrollBarPolicy savedV = Qt::ScrollBarAsNeeded;
    bool saved = false;

    void clear() { saved = false; }

    void capture(Qt::ScrollBarPolicy h, Qt::ScrollBarPolicy v)
    {
        if (!saved) {
            savedH = h;
            savedV = v;
            saved = true;
        }
    }

    /** Return saved policies and clear; no-op if never captured. */
    bool release(Qt::ScrollBarPolicy *h, Qt::ScrollBarPolicy *v)
    {
        if (!saved) {
            return false;
        }
        if (h) {
            *h = savedH;
        }
        if (v) {
            *v = savedV;
        }
        clear();
        return true;
    }
};

#endif // MOTIONSCROLLCHROME_H
