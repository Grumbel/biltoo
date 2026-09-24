// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWMODEFLAGS_H
#define VIEWMODEFLAGS_H

#include "imageview_types.h"

class ImageItem;

/**
 * Pure mode → ImageItem interact/selectable flags.
 * ImageView applyItemModeFlags is a thin host wrapper over this policy.
 */
namespace ViewModeFlags {

void applyToItem(ImageItem *item, ViewMode mode);

} // namespace ViewModeFlags

#endif // VIEWMODEFLAGS_H
