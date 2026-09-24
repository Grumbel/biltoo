// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "view/viewmodeflags.h"
#include "imageitem.h"
#include "imageview.h"

void ViewModeFlags::applyToItem(ImageItem *item, int mode)
{
    if (!item) {
        return;
    }
    // Strict separation:
    //   Workspace → movable + handles
    //   Gallery   → selectable only (open on click), no chrome
    //   Image     → static, no selection chrome
    const auto vm = static_cast<ImageView::ViewMode>(mode);
    if (vm == ImageView::ViewMode::Workspace) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    } else if (vm == ImageView::ViewMode::Gallery) {
        item->setGallerySelectable(true);
        item->setScaleHandlesEnabled(false);
    } else {
        item->setGalleryCellSize({});
        item->setInteractive(false);
        item->setScaleHandlesEnabled(false);
    }
}
