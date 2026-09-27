// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textlayergeometry.h"

#include <QtMath>
#include <algorithm>

namespace TextLayerGeometry {

QVector<int> indicesIntersecting(const QVector<QRectF> &regionRects, const QRectF &rubber)
{
    QVector<int> out;
    if (rubber.isEmpty()) {
        return out;
    }
    out.reserve(regionRects.size());
    for (int i = 0; i < regionRects.size(); ++i) {
        const QRectF &img = regionRects.at(i);
        if (img.isEmpty()) {
            continue;
        }
        if (img.intersects(rubber)) {
            out.push_back(i);
        }
    }
    return out;
}

void sortReadingOrder(QVector<int> *indices, const QVector<QRectF> &regionRects,
                      qreal topTolerance, const QVector<int> *blockIds,
                      bool pageYUp, bool preferSourceOrder)
{
    if (!indices || indices->isEmpty()) {
        return;
    }
    // OCR / engine order: region index is ResultIterator sequence.
    if (preferSourceOrder) {
        std::sort(indices->begin(), indices->end());
        return;
    }
    const bool useBlocks = blockIds && blockIds->size() == regionRects.size();
    auto visualTop = [pageYUp](const QRectF &r) -> qreal {
        // Qt top() is always the lesser Y. In Y-up page space that is the
        // bottom of the page; the visual top edge is bottom().
        return pageYUp ? r.bottom() : r.top();
    };
    std::sort(indices->begin(), indices->end(), [&](int a, int b) {
        if (a < 0 || a >= regionRects.size() || b < 0 || b >= regionRects.size()) {
            return a < b;
        }
        if (useBlocks) {
            const int ba = blockIds->at(a);
            const int bb = blockIds->at(b);
            if (ba >= 0 && bb >= 0 && ba != bb) {
                return ba < bb;
            }
        }
        const QRectF &ra = regionRects.at(a);
        const QRectF &rb = regionRects.at(b);
        const qreal ta = visualTop(ra);
        const qreal tb = visualTop(rb);
        if (qAbs(ta - tb) > topTolerance) {
            // Y-up: larger visualTop is higher on the page → read first.
            return pageYUp ? (ta > tb) : (ta < tb);
        }
        return ra.left() < rb.left();
    });
}

} // namespace TextLayerGeometry
