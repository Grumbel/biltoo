// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionappearance.h"
#include "imageitem.h"

#include <QtMath>

namespace SessionAppearance {

QRect scaleCropRect(const QRect &crop, const QSize &recorded, const QSize &live)
{
    if (crop.isEmpty() || live.width() < 1 || live.height() < 1) {
        return {};
    }
    if (!recorded.isValid() || recorded.width() < 1 || recorded.height() < 1
        || recorded == live) {
        return crop;
    }
    return QRect(
        qRound(crop.x() * double(live.width()) / double(recorded.width())),
        qRound(crop.y() * double(live.height()) / double(recorded.height())),
        qMax(1, qRound(crop.width() * double(live.width()) / double(recorded.width()))),
        qMax(1, qRound(crop.height() * double(live.height()) / double(recorded.height()))));
}

void applyCrop(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item || !state.hasCrop || state.cropRect.isEmpty()) {
        return;
    }
    const QSize sz = item->imageSize();
    if (sz.width() < 1 || sz.height() < 1) {
        return;
    }
    QRect crop = scaleCropRect(state.cropRect, state.cropSourceSize, sz);
    // Legacy: rect only fits orientation-swapped dimensions.
    if (state.cropSourceSize.isEmpty()
        && (crop.right() >= sz.width() || crop.bottom() >= sz.height())) {
        const QSize swapped(sz.height(), sz.width());
        if (swapped.width() > 0 && swapped.height() > 0
            && crop.right() < swapped.width() && crop.bottom() < swapped.height()
            && swapped != sz) {
            crop = scaleCropRect(state.cropRect, swapped, sz);
        }
    }
    if (crop.width() < 1 || crop.height() < 1) {
        return;
    }
    const QPointF off = item->offset();
    // May extend outside the source; cropToLocalRect pads as needed.
    const QRectF local(crop.x() + off.x(), crop.y() + off.y(),
                       crop.width(), crop.height());
    item->cropToLocalRect(local);
}

} // namespace SessionAppearance

const WorkspaceItemState *SessionAppearanceStore::get(SessionImageId id) const
{
    if (id == kInvalidSessionImageId) {
        return nullptr;
    }
    const auto it = m_byId.constFind(id);
    if (it == m_byId.cend()) {
        return nullptr;
    }
    return &(*it);
}

WorkspaceItemState SessionAppearanceStore::value(SessionImageId id) const
{
    if (const WorkspaceItemState *p = get(id)) {
        return *p;
    }
    return {};
}

bool SessionAppearanceStore::contains(SessionImageId id) const
{
    return get(id) != nullptr;
}

void SessionAppearanceStore::set(SessionImageId id, const WorkspaceItemState &state)
{
    if (id == kInvalidSessionImageId) {
        return;
    }
    WorkspaceItemState s = state;
    s.sessionId = id;
    m_byId.insert(id, s);
}

void SessionAppearanceStore::remove(SessionImageId id)
{
    if (id != kInvalidSessionImageId) {
        m_byId.remove(id);
    }
}

void SessionAppearanceStore::clear()
{
    m_byId.clear();
}
