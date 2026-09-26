// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTREGIONKIND_H
#define TEXTREGIONKIND_H

#include "host/thumtoocache.h"

/**
 * Lightweight layout labels (Header / Footer / PageNumber / Body).
 * Same geometry heuristics as thumtoo OCR post-pass — applied on the host so
 * native PDF layers (and older OCR cache) get kinds for colouring, not only
 * freshly OCR'd pages. Not a full layout engine (no columns / true headings).
 */
namespace TextRegionKindAnnotate {

/** In-place: label regions from pageBounds + bbox + short numeric tokens. */
void annotate(ThumtooCache::PageTextLayer *layer);

} // namespace TextRegionKindAnnotate

#endif
