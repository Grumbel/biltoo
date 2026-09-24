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

} // namespace ViewModeFlags

#endif // VIEWMODEFLAGS_H
