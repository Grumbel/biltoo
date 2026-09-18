// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LAYOUTAPPLYGUARD_H
#define LAYOUTAPPLYGUARD_H

/**
 * Re-entrancy flag while GalleryLayout::pack / sceneRect updates run.
 * resizeEvent must not re-enter pack while active.
 */
struct LayoutApplyGuard {
    bool applying = false;

    void begin() { applying = true; }
    void end() { applying = false; }
    bool active() const { return applying; }
    void clear() { applying = false; }
};

#endif // LAYOUTAPPLYGUARD_H
