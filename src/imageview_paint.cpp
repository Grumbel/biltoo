// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include <QPaintEvent>

void ImageView::paintEvent(QPaintEvent *event)
{
    // All overlays are drawn in drawForeground (single GL-safe paint path).
    // Perf timing lives on HudChrome (BILTOO_PERF / THUMTOO_DEBUG).
    m_hud.runTimedPaint([this, event]() {
        QGraphicsView::paintEvent(event);
    });
}
