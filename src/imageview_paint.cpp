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


void ImageView::paintViewportOverlays(QPainter &painter)
{
    paintTextRubberBandOverlay(painter);

    // Viewport-device-pixel overlays (handles, HUD, slideshow cover). Called from
    // drawForeground with an identity transform so this works on both the
    // raster and QOpenGLWidget viewports — a second QPainter on the GL viewport
    // after QGraphicsView::paintEvent clears the framebuffer (white screen).

    // Workspace chrome in *viewport* device pixels (not scene drawForeground).
    // Painting here keeps handles a constant on-screen size under any view or
    // item scale — the same coordinate space as edge affordances and the HUD.
    if (m_cropCtrl.session().active()) {
        m_cropCtrl.paintCropOverlay(painter);
    }
    if (m_attentionCtrl.session().active()) {
        m_attentionCtrl.paintAttentionOverlay(painter);
    }
    m_workspace.paintViewportChrome(painter);

    // Letterbox composite fills the viewport during slideshow; edge chevrons
    // and HUD must paint after it or they are covered.
    m_slideshow.paintLetterboxComposite(painter);
    m_shell.paintEmptySessionInvite(painter);

    if (!m_cropCtrl.session().active() && !m_attentionCtrl.session().active() && hostHoverEdge() != EdgeZone::None && isImageMode()
        && (m_image.sessionNav().isImageModeNavEnabled() || hostHoverEdge() == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }

    m_shell.paintHudPanels(painter);
    m_slideshow.paintSeekbar(painter);
}

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



