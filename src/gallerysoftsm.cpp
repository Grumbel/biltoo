// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerysoftsm.h"

#include <algorithm>

namespace GallerySoft {

void noteLadderDelivery(State &st, int requestEdge, int gotEdge, int softFloor)
{
    if (gotEdge > 0) {
        st.have = std::max(st.have, gotEdge);
        if (gotEdge > kDefaultLqipCeiling) {
            st.weakSinceMs = 0;
        }
    }
    if (st.inflight > 0
        && (requestEdge >= st.inflight
            || (gotEdge > 0 && requestEdge >= softFloor))) {
        st.inflight = 0;
        st.inflightSinceMs = 0;
    }
}

InstallDecision decideHostInstall(int shownEdge, int hostEdge, bool hasDisplay,
                                  bool hasFullDecoded, int softMax)
{
    InstallDecision d;
    if (hostEdge <= 0) {
        return d;
    }
    if (hasFullDecoded && !isStrictUpgrade(shownEdge, hostEdge)) {
        return d;
    }
    if (hasDisplay && !isStrictUpgrade(shownEdge, hostEdge)) {
        return d;
    }
    // Large host samples must not go through SoftPreview (cell clamp leaves
    // shown << host and pass1 loops forever).
    d.kind = (hostEdge > softMax) ? InstallKind::FullSource : InstallKind::SoftPreview;
    d.meaningful = true;
    return d;
}

} // namespace GallerySoft
