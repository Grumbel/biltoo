// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LAYOUTDEBOUNCE_H
#define LAYOUTDEBOUNCE_H

#include "imageview_types.h"

/**
 * Pending Gallery pack reason while the debounce QTimer is armed.
 * Timer ownership stays on ImageView.
 */
struct LayoutDebounce {
    /** Debounce QTimer interval for scheduleApplyLayout (ms). */
    static constexpr int kIntervalMs = 48;

    GalleryPackReason reason = GalleryPackReason::ContentChange;
    bool pending = false;

    void arm(GalleryPackReason r)
    {
        reason = r;
        pending = true;
    }

    /** Take armed reason and clear pending; false if nothing armed. */
    bool take(GalleryPackReason *out)
    {
        if (!pending) {
            return false;
        }
        if (out) {
            *out = reason;
        }
        pending = false;
        return true;
    }

    void clear() { pending = false; }
};

#endif // LAYOUTDEBOUNCE_H
