// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoomblurhelpers.h"

#include <QHash>

namespace ZoomBlur {

qint64 key(const QString &path, int vw, int vh)
{
    if (path.isEmpty() || vw < 1 || vh < 1) {
        return 0;
    }
    return qint64(qHash(path)) ^ (qint64(vw) << 16) ^ qint64(vh);
}

bool keyCached(const SlideshowZoomBlurState &st, qint64 key)
{
    for (int i = 0; i < 2; ++i) {
        if (st.sourceKey[i] == key && !st.underlay[i].isNull()) {
            return true;
        }
    }
    return false;
}

bool keyInFlight(const SlideshowZoomBlurState &st, qint64 key)
{
    for (int i = 0; i < 2; ++i) {
        if (st.inFlightGen[i] == st.generation && st.inFlightKey[i] == key) {
            return true;
        }
    }
    return false;
}

int claimFlightSlot(SlideshowZoomBlurState *st, qint64 key)
{
    if (!st) {
        return -1;
    }
    int flightSlot = -1;
    for (int i = 0; i < 2; ++i) {
        if (st->inFlightKey[i] == 0 || st->inFlightGen[i] != st->generation) {
            flightSlot = i;
            break;
        }
    }
    if (flightSlot < 0) {
        return -1;
    }
    st->inFlightGen[flightSlot] = st->generation;
    st->inFlightKey[flightSlot] = key;
    return flightSlot;
}

void pruneOutsidePair(SlideshowZoomBlurState *st, qint64 keepA, qint64 keepB)
{
    if (!st) {
        return;
    }
    for (int i = 0; i < 2; ++i) {
        const qint64 k = st->sourceKey[i];
        if (k != 0 && k != keepA && k != keepB) {
            st->underlay[i] = QPixmap();
            st->sourceKey[i] = 0;
        }
        const qint64 fk = st->inFlightKey[i];
        if (fk != 0 && fk != keepA && fk != keepB
            && st->inFlightGen[i] == st->generation) {
            st->inFlightGen[i] = 0;
            st->inFlightKey[i] = 0;
        }
    }
}

void clearSizedSlots(SlideshowZoomBlurState *st)
{
    if (!st) {
        return;
    }
    st->underlay[0] = QPixmap();
    st->underlay[1] = QPixmap();
    st->sourceKey[0] = 0;
    st->sourceKey[1] = 0;
}

void invalidateQueue(SlideshowZoomBlurState *st)
{
    if (!st) {
        return;
    }
    ++st->generation;
    st->inFlightGen[0] = st->inFlightGen[1] = 0;
    st->inFlightKey[0] = st->inFlightKey[1] = 0;
}

bool installResult(SlideshowZoomBlurState *st, const QPixmap &blurred, qint64 key,
                   quint64 gen)
{
    if (!st || gen != st->generation) {
        return false;
    }
    int slot = -1;
    for (int i = 0; i < 2; ++i) {
        if (st->sourceKey[i] == key) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = st->underlay[0].isNull() ? 0 : 1;
    }
    st->underlay[slot] = blurred;
    st->sourceKey[slot] = key;
    st->lastGood = blurred;
    st->lastGoodKey = key;
    for (int i = 0; i < 2; ++i) {
        if (st->inFlightKey[i] == key) {
            st->inFlightGen[i] = 0;
            st->inFlightKey[i] = 0;
        }
    }
    return true;
}

} // namespace ZoomBlur
