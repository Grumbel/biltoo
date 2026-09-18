// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cropsession.h"
#include "imageitem.h"

bool CropSession::locksPath(const QString &path) const
{
    if (!draftSampleFrozen || path.isEmpty()) {
        return false;
    }
    if (!draftPath.isEmpty() && path == draftPath) {
        return true;
    }
    if (targetItem && targetItem->path() == path) {
        return true;
    }
    return false;
}

bool CropSession::locksItem(const ImageItem *item) const
{
    if (!draftSampleFrozen || !item) {
        return false;
    }
    if (targetItem && item == targetItem) {
        return true;
    }
    if (targetId != kInvalidSessionImageId
        && item->sessionId() == targetId) {
        return true;
    }
    if (!item->path().isEmpty() && locksPath(item->path())) {
        return true;
    }
    return false;
}
