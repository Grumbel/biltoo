// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textregionkind.h"

#include <QChar>
#include <QtMath>

namespace TextRegionKindAnnotate {
namespace {

bool isPageNumberText(const QString &s)
{
    QString t;
    t.reserve(s.size());
    for (QChar c : s) {
        if (!c.isSpace()) {
            t.append(c);
        }
    }
    if (t.isEmpty() || t.size() > 12) {
        return false;
    }
    int digits = 0;
    int roman = 0;
    for (QChar c : t) {
        if (c.isDigit()) {
            ++digits;
        } else if (QStringLiteral("ivxlcdmIVXLCDM").contains(c)) {
            ++roman;
        } else if (c == QLatin1Char('-') || c == QLatin1Char('.')
                   || c == QLatin1Char('/')) {
            continue;
        } else {
            return false;
        }
    }
    return digits > 0 || roman > 0;
}

} // namespace

void annotate(ThumtooCache::PageTextLayer *layer)
{
    if (!layer || !layer->pageBounds.isValid() || layer->regions.isEmpty()) {
        return;
    }
    const QRectF pb = layer->pageBounds;
    const qreal ph = pb.height();
    const qreal pw = pb.width();
    if (ph < 1.0 || pw < 1.0) {
        return;
    }
    const qreal topBand = pb.top() + ph * 0.08;
    const qreal botBand = pb.bottom() - ph * 0.08;
    const qreal cx = pb.center().x();

    for (ThumtooCache::TextRegion &r : layer->regions) {
        if (r.role != ThumtooCache::TextRegion::Role::Text) {
            continue;
        }
        // Re-annotate every time so cache without kinds still improves on load.
        r.kind = ThumtooCache::TextRegion::Kind::Body;
        const QRectF b = r.bbox;
        if (!b.isValid()) {
            continue;
        }
        const qreal midY = b.center().y();
        const qreal midX = b.center().x();
        const bool inTop = midY <= topBand;
        const bool inBot = midY >= botBand;
        if (!inTop && !inBot) {
            continue;
        }
        if (isPageNumberText(r.text)) {
            const qreal edge = qMin(qAbs(midX - pb.left()), qAbs(pb.right() - midX));
            const bool outer = edge < pw * 0.2;
            const bool centered = qAbs(midX - cx) < pw * 0.15;
            if (outer || centered) {
                r.kind = ThumtooCache::TextRegion::Kind::PageNumber;
                continue;
            }
        }
        if (inTop) {
            r.kind = ThumtooCache::TextRegion::Kind::Header;
        } else if (inBot) {
            r.kind = ThumtooCache::TextRegion::Kind::Footer;
        }
    }
}

} // namespace TextRegionKindAnnotate
