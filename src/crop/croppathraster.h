// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPPATHRASTER_H
#define CROPPATHRASTER_H

#include "display/pathrasterservice.h"

#include <QString>

/**
 * PathRaster ownership during crop (PreferCache / Full climbs).
 *
 * Crop draft pixels must not fight soft/Full ladder upgrades on the same path.
 * ImageView calls suspend at enter, before full-raster request, and on leave.
 * PathRasterService remains the scheduler; this is the crop policy boundary only.
 */
namespace CropPathRaster {

/** Cancel in-flight PreferCache / Full / soft for @p path (no-op if empty). */
inline void suspend(PathRasterService *svc, const QString &path)
{
    if (svc && !path.isEmpty()) {
        svc->cancel(path);
    }
}

} // namespace CropPathRaster

#endif // CROPPATHRASTER_H
