// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "util/biltoo_thread.h"
#include "hud/hudmodel.h"
#include "text/textsearchpolicy.h"
#include "view/canvaspatterngeometry.h"
#include "view/viewtransform.h"
#include "hud/hudgeometry.h"
#include "slideshow/slideshowclocks.h"
#include "text/textlayergeometry.h"
#include "workspace/pageguidegeometry.h"
#include "image/edgenavpolicy.h"
#include <QElapsedTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include <QFileInfo>
#include <QDebug>
#include <QPixmap>
#include "imageitem.h"

#include <QPainter>
#include <QRadialGradient>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionGraphicsItem>
#include "util/biltoo_logging.h"
#include <QGraphicsItem>


void ImageView::paintEvent(QPaintEvent *event)
{
    GUI_BUDGET("ImageView::paintEvent");
    // All overlays are drawn in drawForeground (single GL-safe paint path).
    if (!m_hud.perf().isEnabled()) {
        QGraphicsView::paintEvent(event);
        return;
    }
    QElapsedTimer t;
    t.start();
    QGraphicsView::paintEvent(event);
    m_hud.perf().notePaintUs(t.nsecsElapsed() / 1000);
}


// --- Slideshow overlays (was imageview_slideshow_paint.cpp) ---




// --- HUD / empty invite (was imageview_hud_paint.cpp) ---



