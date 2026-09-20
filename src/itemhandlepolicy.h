// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMHANDLEPOLICY_H
#define ITEMHANDLEPOLICY_H

#include "itemhandle.h"

#include <QString>

/**
 * Pure classification of ItemHandle values.
 * No item or view state — shared by interaction, cursors, and tooltips.
 */
namespace ItemHandlePolicy {

bool isChromeHandle(ItemHandle h);
bool isRotateHandle(ItemHandle h);
bool isCornerScaleHandle(ItemHandle h);
bool isEdgeScaleHandle(ItemHandle h);
bool isScaleHandle(ItemHandle h);
bool isShearHandle(ItemHandle h);
bool isUprightChromeHandle(ItemHandle h);

/** Localised tooltip for a handle (context "ImageItem"). */
QString toolTip(ItemHandle h);

} // namespace ItemHandlePolicy

#endif // ITEMHANDLEPOLICY_H
