// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Link stub for unit tests that compile sessionappearance.cpp without the
 * full thumtoo host (SQLite / client). Content-appearance XDG ops are no-ops.
 */

#include "host/thumtoocache.h"

namespace ThumtooCache {

bool loadContentAppearance(const QString &, StoredContentAppearance *out)
{
    if (out) {
        *out = StoredContentAppearance{};
    }
    return false;
}

void saveContentAppearance(const QString &, const StoredContentAppearance &)
{
}

bool hasContentAppearance(const QString &)
{
    return false;
}

void clearContentAppearance(const QString &)
{
}

} // namespace ThumtooCache
