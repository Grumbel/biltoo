// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textlayerresolve.h"

namespace TextLayerResolve {

ThumtooCache::PageTextLayer load(const QString &sessionPath, Prefer prefer)
{
    if (sessionPath.isEmpty()) {
        return {};
    }
    switch (prefer) {
    case Prefer::Native: {
        ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(sessionPath);
        if (layer.regions.isEmpty()) {
            layer = ThumtooCache::ensurePageTextLayer(sessionPath);
        }
        return layer;
    }
    case Prefer::Ocr:
        return ThumtooCache::cachedOcrPageTextLayer(sessionPath);
    case Prefer::Auto:
    default: {
        ThumtooCache::PageTextLayer ocr = ThumtooCache::cachedOcrPageTextLayer(sessionPath);
        if (!ocr.regions.isEmpty()) {
            return ocr;
        }
        ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(sessionPath);
        if (layer.regions.isEmpty()) {
            layer = ThumtooCache::ensurePageTextLayer(sessionPath);
        }
        return layer;
    }
    }
}

} // namespace TextLayerResolve
