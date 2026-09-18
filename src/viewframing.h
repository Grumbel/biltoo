// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWFRAMING_H
#define VIEWFRAMING_H

#include <QtGlobal>

/**
 * Image-mode framing preferences and sticky/preserved navigation scale.
 */
enum class StickyZoomKind { Fit = 0, Fill = 1, Actual = 2 };

struct ViewFraming {
    bool fitMode = true;
    bool fillMode = false;
    bool stickyZoomEnabled = false;
    StickyZoomKind stickyZoomKind = StickyZoomKind::Fit;
    bool haveStickyPanAnchor = false;
    qreal stickyPanNormX = 0.5;
    qreal stickyPanNormY = 0.5;
    /** Free (non-sticky) nav: keep absolute view scale across images. */
    bool havePreservedViewScale = false;
    qreal preservedViewScale = 1.0;

    void clampStickyPanNorms()
    {
        stickyPanNormX = qBound(0.0, stickyPanNormX, 1.0);
        stickyPanNormY = qBound(0.0, stickyPanNormY, 1.0);
    }
};

#endif // VIEWFRAMING_H
