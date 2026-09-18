// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMHANDLEPOLICY_H
#define ITEMHANDLEPOLICY_H

#include "imageitem.h"

/**
 * Pure classification of ImageItem::Handle values.
 * No item or view state — shared by interaction, cursors, and tooltips.
 */
namespace ItemHandlePolicy {

bool isChromeHandle(ImageItem::Handle h);
bool isRotateHandle(ImageItem::Handle h);
bool isCornerScaleHandle(ImageItem::Handle h);
bool isEdgeScaleHandle(ImageItem::Handle h);
bool isScaleHandle(ImageItem::Handle h);
bool isShearHandle(ImageItem::Handle h);
bool isUprightChromeHandle(ImageItem::Handle h);

} // namespace ItemHandlePolicy

#endif // ITEMHANDLEPOLICY_H
