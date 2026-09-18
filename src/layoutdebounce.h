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
    GalleryPackReason reason = GalleryPackReason::ContentChange;

    void arm(GalleryPackReason r) { reason = r; }
};

#endif // LAYOUTDEBOUNCE_H
