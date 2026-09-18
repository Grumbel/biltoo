// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYRELAYOUTSUPPRESS_H
#define GALLERYRELAYOUTSUPPRESS_H

/**
 * Nested suppress counter: Gallery delete must not repack via resizeEvent.
 * Callers push(true) / push(false); active() when count > 0.
 */
struct GalleryRelayoutSuppress {
    int count = 0;

    void push(bool on)
    {
        if (on) {
            ++count;
        } else if (count > 0) {
            --count;
        }
    }

    bool active() const { return count > 0; }
};

#endif // GALLERYRELAYOUTSUPPRESS_H
