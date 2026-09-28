// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SPREADSTATE_H
#define SPREADSTATE_H

/**
 * Multi-page reading surface membership + pure layout (docs/SPREAD.md).
 * Header-only: no Qt widgets; safe for tests and session ownership.
 */

#include "imageview_types.h"

#include <QRectF>
#include <QSizeF>
#include <QVector>

#include <algorithm>
#include <cmath>

struct SpreadSlot {
    SessionImageId sessionId = kInvalidSessionImageId;
    QRectF rect; ///< in spread frame (origin top-left of first slot)
};

struct SpreadLayoutResult {
    QVector<SpreadSlot> memberSlots;
    QRectF unionRect;
};

enum class SpreadMembershipPolicy {
    Off = 0,
    FixedN = 1,
    Selection = 2,
    Explicit = 3,
};

enum class SpreadStride {
    BySpread = 0,
    ByPage = 1,
};

enum class SpreadBindingHint {
    StrictPairs = 0,
    CoverAlone = 1,
    AnchorCentre = 2,
};

enum class SpreadDirection {
    Ltr = 0,
    Rtl = 1,
    Vertical = 2,
};

struct SpreadState {
    QVector<SessionImageId> members;
    SessionImageId anchor = kInvalidSessionImageId;
    SpreadMembershipPolicy policy = SpreadMembershipPolicy::Off;
    SpreadStride stride = SpreadStride::BySpread;
    SpreadBindingHint binding = SpreadBindingHint::StrictPairs;
    SpreadDirection direction = SpreadDirection::Ltr;
    int fixedN = 2;

    bool isActive() const
    {
        return policy != SpreadMembershipPolicy::Off && !members.isEmpty();
    }

    void clear()
    {
        members.clear();
        anchor = kInvalidSessionImageId;
        policy = SpreadMembershipPolicy::Off;
        fixedN = 2;
    }
};

/**
 * Horizontal LTR/RTL or vertical stack. pageSizes aligned with member order.
 * heightMatch: match cross-axis size (height for H, width for V) to the minimum.
 */
inline SpreadLayoutResult layoutSpread(const QVector<QSizeF> &pageSizes,
                                       qreal gutter = 8.0,
                                       bool heightMatch = true,
                                       SpreadDirection direction = SpreadDirection::Ltr)
{
    SpreadLayoutResult out;
    if (pageSizes.isEmpty()) {
        return out;
    }
    out.memberSlots.reserve(pageSizes.size());

    if (direction == SpreadDirection::Vertical) {
        // Stack top→bottom; optional width-match to minimum positive width.
        qreal targetW = 0;
        if (heightMatch) { // reuse flag as "match cross-axis"
            for (const QSizeF &sz : pageSizes) {
                if (sz.width() > 1.0) {
                    targetW = targetW <= 0 ? sz.width() : std::min(targetW, sz.width());
                }
            }
        }
        qreal y = 0;
        qreal maxW = 0;
        for (const QSizeF &sz : pageSizes) {
            SpreadSlot slot;
            qreal w = std::max(1.0, sz.width());
            qreal h = std::max(1.0, sz.height());
            if (heightMatch && targetW > 1.0 && w > 1.0) {
                const qreal s = targetW / w;
                h *= s;
                w = targetW;
            }
            slot.rect = QRectF(0, y, w, h);
            out.memberSlots.append(slot);
            y += h + gutter;
            maxW = std::max(maxW, w);
        }
        if (!out.memberSlots.isEmpty()) {
            out.unionRect = QRectF(0, 0, maxW, out.memberSlots.last().rect.bottom());
        }
        return out;
    }

    qreal targetH = 0;
    if (heightMatch) {
        for (const QSizeF &sz : pageSizes) {
            if (sz.height() > 1.0) {
                targetH = targetH <= 0 ? sz.height() : std::min(targetH, sz.height());
            }
        }
    }
    qreal x = 0;
    qreal maxH = 0;
    for (const QSizeF &sz : pageSizes) {
        SpreadSlot slot;
        qreal w = std::max(1.0, sz.width());
        qreal h = std::max(1.0, sz.height());
        if (heightMatch && targetH > 1.0 && h > 1.0) {
            const qreal s = targetH / h;
            w *= s;
            h = targetH;
        }
        slot.rect = QRectF(x, 0, w, h);
        out.memberSlots.append(slot);
        x += w + gutter;
        maxH = std::max(maxH, h);
    }
    if (!out.memberSlots.isEmpty()) {
        const qreal right = out.memberSlots.last().rect.right();
        out.unionRect = QRectF(0, 0, right, maxH);
        if (direction == SpreadDirection::Rtl && out.unionRect.width() > 1.0) {
            const qreal totalW = out.unionRect.width();
            for (SpreadSlot &slot : out.memberSlots) {
                slot.rect.moveLeft(totalW - slot.rect.right());
            }
        }
    }
    return out;
}

/**
 * Build a FixedN membership window from a session id list.
 * StrictPairs: window starts at floor(anchorIndex / n) * n.
 * CoverAlone (n=2): index 0 alone; else same as StrictPairs on (index-1).
 * AnchorCentre: try to keep anchor inside a window of n ending at or after anchor.
 */
inline QVector<SessionImageId> buildFixedNMembers(const QVector<SessionImageId> &sessionIds,
                                                  int anchorIndex,
                                                  int n,
                                                  SpreadBindingHint binding)
{
    QVector<SessionImageId> out;
    const int count = sessionIds.size();
    if (count <= 0 || n <= 0 || anchorIndex < 0 || anchorIndex >= count) {
        return out;
    }
    n = std::max(1, n);
    int start = 0;
    if (n == 1) {
        start = anchorIndex;
    } else if (binding == SpreadBindingHint::CoverAlone && n == 2) {
        if (anchorIndex <= 0) {
            start = 0;
            n = 1;
        } else {
            start = ((anchorIndex - 1) / 2) * 2 + 1;
        }
    } else if (binding == SpreadBindingHint::AnchorCentre) {
        start = anchorIndex - (n / 2);
        if (start < 0) {
            start = 0;
        }
        if (start + n > count) {
            start = std::max(0, count - n);
        }
    } else {
        // StrictPairs
        start = (anchorIndex / n) * n;
    }
    for (int i = start; i < count && out.size() < n; ++i) {
        out.append(sessionIds.at(i));
    }
    return out;
}

/**
 * Advance anchor index under stride; returns new anchor index or -1 if unchanged/OOB.
 */
inline int advanceSpreadAnchor(int anchorIndex, int sessionCount, int memberCount,
                               SpreadStride stride, int direction)
{
    if (sessionCount <= 0 || anchorIndex < 0 || direction == 0) {
        return -1;
    }
    const int step = (stride == SpreadStride::ByPage)
        ? (direction > 0 ? 1 : -1)
        : (direction > 0 ? std::max(1, memberCount) : -std::max(1, memberCount));
    int next = anchorIndex + step;
    if (next < 0) {
        next = 0;
    }
    if (next >= sessionCount) {
        next = sessionCount - 1;
    }
    return next == anchorIndex ? -1 : next;
}

#endif // SPREADSTATE_H
