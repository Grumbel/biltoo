// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWMODEFLAGS_H
#define VIEWMODEFLAGS_H

class ImageItem;

/**
 * Pure mode → ImageItem interact/selectable flags.
 * @p mode is ImageView::ViewMode (Image=0, Gallery, Workspace) without
 * including ImageView.h in this leaf header.
 * ImageView::applyItemModeFlags is a thin host wrapper over this policy.
 */
namespace ViewModeFlags {

void applyToItem(ImageItem *item, int mode);

/**
 * Image leave: capture sticky pan/zoom before destroying the underlay so free
 * zoom survives Gallery/Workspace round-trips. @p previousMode is ViewMode
 * (Image=0, Gallery, Workspace).
 */
[[nodiscard]] inline bool shouldCaptureStickyPanOnLeave(int previousMode,
                                                        bool hasLiveItems)
{
    // ImageView::ViewMode::Image == 0
    return previousMode == 0 && hasLiveItems;
}

} // namespace ViewModeFlags

#endif // VIEWMODEFLAGS_H
