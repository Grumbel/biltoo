// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LAYOUTDEBOUNCE_H
#define LAYOUTDEBOUNCE_H

#include "imageview_types.h"

#include <QElapsedTimer>

/**
 * Pending Gallery pack reason while the debounce QTimer is armed.
 * Timer ownership stays on ImageView.
 */
struct LayoutDebounce {
    /** Quiet-period debounce for steady-state packs (ms). */
    static constexpr int kIntervalMs = 48;
    /**
     * While the Gallery size gate streams sizeReady, restarting the quiet
     * timer would starve pack for the whole probe stream (cells stuck at the
     * origin). Cap the wait so a progressive pack runs at least this often.
     */
    static constexpr int kProgressiveMaxWaitMs = 120;
    /** Quiet interval used during the size gate (ms). */
    static constexpr int kProgressiveIntervalMs = 50;

    GalleryPackReason reason = GalleryPackReason::ContentChange;
    bool pending = false;
    QElapsedTimer progressiveArmed;

    void arm(GalleryPackReason r, bool progressive)
    {
        reason = r;
        if (!pending && progressive) {
            progressiveArmed.start();
        }
        if (!progressive) {
            progressiveArmed.invalidate();
        }
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
        progressiveArmed.invalidate();
        return true;
    }

    void clear()
    {
        pending = false;
        progressiveArmed.invalidate();
    }

    /** True when a progressive arm has been waiting ≥ kProgressiveMaxWaitMs. */
    bool progressiveMaxWaitExceeded() const
    {
        return pending && progressiveArmed.isValid()
            && progressiveArmed.elapsed() >= kProgressiveMaxWaitMs;
    }
};

#endif // LAYOUTDEBOUNCE_H
