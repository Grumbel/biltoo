// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERRESOLVE_H
#define TEXTLAYERRESOLVE_H

#include "host/thumtoocache.h"

#include <QString>

/**
 * Which text-layer Store slot to use for Find / overlays.
 * Auto: cached OCR when non-empty, else native (extract if needed).
 * Native / Ocr: that slot only — no silent fallback.
 */
namespace TextLayerResolve {

enum class Prefer {
    Auto = 0,
    Native = 1,
    Ocr = 2,
};

/**
 * Load a page text layer honouring @p prefer.
 * OCR path is cache-only (never runs Tesseract). Native may extract.
 * Safe off the GUI thread for document search workers.
 */
ThumtooCache::PageTextLayer load(const QString &sessionPath, Prefer prefer);

} // namespace TextLayerResolve

#endif // TEXTLAYERRESOLVE_H
