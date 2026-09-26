// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTREGIONSTYLE_H
#define TEXTREGIONSTYLE_H

#include "host/thumtoocache.h"

#include <QColor>
#include <QString>

/**
 * Presentation colours / labels for text-region Role + Kind.
 * Pure functions — usable from paint, Text panel, and future scripting.
 *
 * Kind comes from thumtoo OCR post-pass (geometry + token shape). Native PDF
 * extract currently leaves Kind=Body unless a later pass annotates it.
 * There is no column / true heading classifier yet.
 */
namespace TextRegionStyle {

[[nodiscard]] inline QString kindLabel(ThumtooCache::TextRegion::Kind k)
{
    using K = ThumtooCache::TextRegion::Kind;
    switch (k) {
    case K::Header:
        return QStringLiteral("header");
    case K::Footer:
        return QStringLiteral("footer");
    case K::PageNumber:
        return QStringLiteral("page#");
    case K::Body:
    default:
        return QStringLiteral("body");
    }
}

/** Outline stroke for region boxes (role wins for links). */
[[nodiscard]] inline QColor outlineColor(const ThumtooCache::TextRegion &r)
{
    if (r.role == ThumtooCache::TextRegion::Role::Link) {
        return QColor(40, 180, 80, 220); // green links
    }
    using K = ThumtooCache::TextRegion::Kind;
    switch (r.kind) {
    case K::Header:
        return QColor(180, 80, 220, 220); // violet
    case K::Footer:
        return QColor(80, 140, 220, 220); // blue
    case K::PageNumber:
        return QColor(220, 160, 40, 220); // amber
    case K::Body:
    default:
        return QColor(220, 80, 40, 180); // coral body
    }
}

/** Soft fill under hover / optional legend swatches. */
[[nodiscard]] inline QColor fillColor(const ThumtooCache::TextRegion &r, int alpha = 50)
{
    QColor c = outlineColor(r);
    c.setAlpha(alpha);
    return c;
}

} // namespace TextRegionStyle

#endif
