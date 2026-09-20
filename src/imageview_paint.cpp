// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "textsearchpolicy.h"
#include "canvaspatterngeometry.h"
#include "viewtransform.h"
#include "hudgeometry.h"
#include "slideshowclocks.h"
#include "textlayergeometry.h"
#include "pageguidegeometry.h"
#include "edgenavpolicy.h"
#include <QElapsedTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include "thumtoocache.h"
#include "pagepath.h"
#include <QFileInfo>
#include <QDebug>
#include <QPixmap>
#include "imageitem.h"

#include <QPainter>
#include <QRadialGradient>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionGraphicsItem>
#include "biltoo_logging.h"
#include <QGraphicsItem>

void ImageView::drawEdgeAffordances(QPainter &painter)
{
    if (m_hoverEdge == EdgeZone::None || !isImageMode()) {
        return;
    }
    if (m_hoverEdge != EdgeZone::GalleryReturn && !m_sessionNav.isImageModeNavEnabled()) {
        return;
    }

    EdgeNavPolicy::Zone zone = EdgeNavPolicy::Zone::None;
    switch (m_hoverEdge) {
    case EdgeZone::Previous:
        zone = EdgeNavPolicy::Zone::Previous;
        break;
    case EdgeZone::Next:
        zone = EdgeNavPolicy::Zone::Next;
        break;
    case EdgeZone::GalleryReturn:
        zone = EdgeNavPolicy::Zone::GalleryReturn;
        break;
    default:
        return;
    }

    const QRect vr = viewport()->rect();
    const EdgeNavPolicy::ChromeLayout layout = EdgeNavPolicy::chromeLayout(
        zone, vr, edgeZoneWidth(), edgeZoneHeight());
    if (layout.fillRect.isEmpty()) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    const int r = EdgeNavPolicy::kDefaultButtonRadius;

    auto drawChevronButton = [&](int cx, int cy, auto buildChevron) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 140));
        painter.drawEllipse(QPoint(cx, cy), r, r);
        painter.setBrush(QColor(255, 255, 255, 230));
        painter.drawEllipse(QPoint(cx, cy), r - 3, r - 3);
        QPainterPath chevron;
        buildChevron(chevron, cx, cy);
        QPen pen(QColor(40, 40, 40), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.strokePath(chevron, pen);
    };

    // Soft radial lobe (~80% of the edge via layout.fillRect). QRadialGradient
    // in a scaled unit circle gives a smooth elliptical falloff without hard
    // linear strips.
    {
        const QRectF fr = layout.fillRect;
        const QColor core(0, 0, 0, 110);
        const QColor mid(0, 0, 0, 55);
        const QColor edge(0, 0, 0, 0);

        auto paintRadialLobe = [&](QPointF centre, qreal rx, qreal ry,
                                   const QRectF &clip) {
            if (rx < 1.0 || ry < 1.0 || clip.isEmpty()) {
                return;
            }
            painter.save();
            painter.setClipRect(clip);
            painter.translate(centre);
            painter.scale(rx, ry);
            QRadialGradient g(QPointF(0, 0), 1.0);
            g.setColorAt(0.00, core);
            g.setColorAt(0.45, mid);
            g.setColorAt(1.00, edge);
            painter.setPen(Qt::NoPen);
            painter.setBrush(g);
            // Unit circle; scale maps it to the ellipse.
            painter.drawEllipse(QRectF(-1.0, -1.0, 2.0, 2.0));
            painter.restore();
        };

        if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
            // Centre on top edge mid; only lower half visible via clip.
            paintRadialLobe(QPointF(fr.center().x(), fr.top()),
                            fr.width() * 0.5, fr.height(),
                            fr);
        } else if (zone == EdgeNavPolicy::Zone::Previous) {
            // Centre on left edge mid; only right half of the ellipse shows.
            paintRadialLobe(QPointF(fr.left(), fr.center().y()),
                            fr.width(), fr.height() * 0.5,
                            fr);
        } else {
            // Next: centre on right edge mid.
            paintRadialLobe(QPointF(fr.right(), fr.center().y()),
                            fr.width(), fr.height() * 0.5,
                            fr);
        }
    }

    const int cx = layout.buttonCenter.x();
    const int cy = layout.buttonCenter.y();
    if (zone == EdgeNavPolicy::Zone::GalleryReturn) {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px - 10, py + 5);
            chevron.lineTo(px, py - 6);
            chevron.lineTo(px + 10, py + 5);
        });
    } else if (zone == EdgeNavPolicy::Zone::Previous) {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px + 5, py - 10);
            chevron.lineTo(px - 6, py);
            chevron.lineTo(px + 5, py + 10);
        });
    } else {
        drawChevronButton(cx, cy, [](QPainterPath &chevron, int px, int py) {
            chevron.moveTo(px - 5, py - 10);
            chevron.lineTo(px + 6, py);
            chevron.lineTo(px - 5, py + 10);
        });
    }
}

void ImageView::paintWorkspaceViewportChrome(QPainter &painter)
{
    if (!m_cropCtrl.session().active() && isWorkspaceMode() && m_scene) {
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                // Only paint chrome for items we still own (guards against a
                // stale selection entry after destroyCanvasItem).
                if (ii->isInteractive() && m_items.contains(ii) && ii->scene() == m_scene) {
                    selected.append(ii);
                }
            }
        }
        std::sort(selected.begin(), selected.end(),
                  [](ImageItem *a, ImageItem *b) { return a->stackZ() < b->stackZ(); });
        if (selected.size() == 1) {
            selected.first()->paintInteractionChrome(&painter);
        } else if (selected.size() > 1) {
            // Multi-select: per-item outline only; group scale handles on the union.
            for (ImageItem *item : selected) {
                item->paintSelectionFrame(&painter);
            }
            paintGroupSelectionChrome(&painter, selected);
        }
        if (m_pageGuide.isInteractive()) {
            paintPageGuideHandles(&painter);
        }
    }

}

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
        paintCropOverlay(painter);
    }
    if (m_attentionCtrl.session().active()) {
        paintAttentionOverlay(painter);
    }
    paintWorkspaceViewportChrome(painter);

    // Letterbox composite fills the viewport during slideshow; edge chevrons
    // and HUD must paint after it or they are covered.
    paintSlideshowLetterboxComposite(painter);
    paintEmptySessionInvite(painter);

    if (!m_cropCtrl.session().active() && !m_attentionCtrl.session().active() && m_hoverEdge != EdgeZone::None && isImageMode()
        && (m_sessionNav.isImageModeNavEnabled() || m_hoverEdge == EdgeZone::GalleryReturn)) {
        drawEdgeAffordances(painter);
    }

    paintHudPanels(painter);
    paintSlideshowSeekbar(painter);
}

void ImageView::paintEvent(QPaintEvent *event)
{
    // All overlays are drawn in drawForeground (single GL-safe paint path).
    if (!m_perf.isEnabled()) {
        QGraphicsView::paintEvent(event);
        return;
    }
    QElapsedTimer t;
    t.start();
    QGraphicsView::paintEvent(event);
    m_perf.notePaintUs(t.nsecsElapsed() / 1000);
}

void ImageView::paintCanvasBackground(QPainter *painter, const QRectF &rect,
                                           qreal viewScale)
{
    if (!painter) {
        return;
    }
    viewScale = ViewTransform::sanitizeViewScale(viewScale);

    auto fillChecker = [&](const QColor &a, const QColor &b) {
        const qreal cell = CanvasPatternGeometry::checkerCellScene(viewScale);
        const qreal x0 = std::floor(rect.left() / cell) * cell;
        const qreal y0 = std::floor(rect.top() / cell) * cell;
        const qreal x1 = std::ceil(rect.right() / cell) * cell;
        const qreal y1 = std::ceil(rect.bottom() / cell) * cell;
        for (qreal y = y0; y < y1; y += cell) {
            for (qreal x = x0; x < x1; x += cell) {
                const int ix = static_cast<int>(std::floor(x / cell));
                const int iy = static_cast<int>(std::floor(y / cell));
                const bool dark = ((ix + iy) & 1) != 0;
                painter->fillRect(QRectF(x, y, cell, cell), dark ? a : b);
            }
        }
    };

    auto fillImageTile = [&](QPixmap &tileCache, const QString &path,
                             auto storeTile) {
        if (tileCache.isNull() && !path.isEmpty()) {
            QPixmap px(path);
            if (!px.isNull()) {
                storeTile(px, path);
            }
        }
        if (tileCache.isNull()) {
            painter->fillRect(rect, m_canvasBg.primaryColor());
            return;
        }
        const QPixmap &tile = tileCache;
        qreal tw = qreal(ViewTransform::atLeast1(tile.width()));
        qreal th = qreal(ViewTransform::atLeast1(tile.height()));
        const qreal lod = CanvasPatternGeometry::tileLodFactor(tw, viewScale);
        const qreal cellW = tw * lod;
        const qreal cellH = th * lod;
        const qreal x0 = std::floor(rect.left() / cellW) * cellW;
        const qreal y0 = std::floor(rect.top() / cellH) * cellH;
        const qreal x1 = std::ceil(rect.right() / cellW) * cellW;
        const qreal y1 = std::ceil(rect.bottom() / cellH) * cellH;
        for (qreal y = y0; y < y1; y += cellH) {
            for (qreal x = x0; x < x1; x += cellW) {
                painter->drawPixmap(QRectF(x, y, cellW, cellH), tile,
                                    QRectF(0, 0, tw, th));
            }
        }
    };

    auto paintMaterial = [&](const WorkspaceBackground &wb, QPixmap &tileCache,
                             auto storeTile) {
        if (wb.mode == WorkspaceBackgroundMode::Solid) {
            painter->fillRect(rect, wb.color.isValid() ? wb.color : m_canvasBg.primaryColor());
        } else if (wb.mode == WorkspaceBackgroundMode::Checkerboard) {
            const QColor a = wb.color.isValid() ? wb.color : m_canvasBg.primaryColor();
            const QColor b = wb.colorAlt.isValid() ? wb.colorAlt : a.lighter(120);
            fillChecker(a, b);
        } else if (wb.mode == WorkspaceBackgroundMode::ImageTile) {
            fillImageTile(tileCache, wb.imagePath, storeTile);
        } else if (wb.mode == WorkspaceBackgroundMode::ContentBlur) {
            // Image mode: cover-scale blur under the sharp item (viewport space).
            // Gallery (no single subject): configured solid color from the dialog.
            QImage src;
            QString path;
            if (isImageMode()) {
                ImageItem *item = primaryItem();
                if (!item && hasClassicPath()) {
                    item = findItemByPath(classicPath());
                }
                if (item) {
                    src = item->displayImage();
                    if (src.isNull()) {
                        src = item->sourceImage();
                    }
                    path = item->path();
                }
                if (path.isEmpty() && hasClassicPath()) {
                    path = classicPath();
                }
            }
            if (!src.isNull() && viewport()) {
                painter->save();
                painter->resetTransform();
                const QRect vr = viewport()->rect();
                const qint64 key = path.isEmpty() ? qint64(0) : qint64(qHash(path));
                m_slideshow.paintZoomBlurUnderlay(painter, src, vr, key);
                painter->restore();
            } else {
                const QColor fill = wb.color.isValid() ? wb.color : m_canvasBg.primaryColor();
                painter->fillRect(rect, fill);
            }
        } else {
            painter->fillRect(rect, m_canvasBg.primaryColor());
        }
    };

    const bool wsOverride = isWorkspaceMode()
        && !m_canvasBg.isWorkspaceAppDefault()
        && !m_canvasBg.isWorkspaceShowDefault();
    const bool viewOverride =
        (isGalleryMode() || isImageMode()) && !m_canvasBg.isViewAppDefault();

    if (wsOverride) {
        paintMaterial(m_canvasBg.workspaceRef(), m_canvasBg.workspaceTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_canvasBg.setWorkspaceTile(px, path);
                      });
    } else if (viewOverride) {
        paintMaterial(m_canvasBg.viewRef(), m_canvasBg.viewTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_canvasBg.setViewTile(px, path);
                      });
    } else {
        if (!m_canvasBg.useChecker(isWorkspaceMode())) {
            painter->fillRect(rect, m_canvasBg.primaryColor());
        } else {
            fillChecker(m_canvasBg.primaryColor(), m_canvasBg.checkerAlt());
        }
    }
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    paintCanvasBackground(painter, rect, transform().m11());

    // Page guide paper (under images): plain white sheet in scene units.
    if (m_pageGuide.isVisible() && isWorkspaceMode()) {
        const QRectF page = pageGuideSceneRect();
        if (page.intersects(rect)) {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::white);
            painter->drawRect(page);
            painter->restore();
        }
    }
}







void ImageView::drawForeground(QPainter *painter, const QRectF &rect)
{
    // Page guide outline above images so the frame stays visible when tiles
    // cover the white sheet (scene coordinates).
    if (m_pageGuide.isVisible() && isWorkspaceMode()) {
        const QRectF page = pageGuideSceneRect();
        if (page.intersects(rect)) {
            painter->save();
            QPen pen(QColor(40, 100, 200, 220));
            pen.setStyle(Qt::DashLine);
            pen.setWidthF(0);
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(page);
            QRectF margin = page.adjusted(page.width() * 0.05, page.height() * 0.05,
                                          -page.width() * 0.05, -page.height() * 0.05);
            QPen marginPen(QColor(40, 100, 200, 120));
            marginPen.setStyle(Qt::DotLine);
            marginPen.setCosmetic(true);
            painter->setPen(marginPen);
            painter->drawRect(margin);
            painter->restore();
        }
    }

    // Text search highlights + optional region outlines (Image mode page docs).
    if (isImageMode() && m_textLayer.hasRegions()
        && (m_textLayer.showsRegions() || m_textLayer.hasSearchMatches())) {
        if (ImageItem *item = primaryItem()) {
            const QSize sz = item->imageSize();
            if (sz.width() > 0 && sz.height() > 0 && m_textLayer.pageBoundsValid()) {
                painter->save();
                // Search hits: filled yellow first (under outlines / selection).
                if (m_textLayer.hasSearchMatches()) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor(255, 220, 40, 110));
                    for (int idxMatch : m_textLayer.searchMatchesRef()) {
                        if (idxMatch < 0 || idxMatch >= m_textLayer.regionCount()) {
                            continue;
                        }
                        const auto &r = m_textLayer.regionAt(idxMatch);
                        const QRectF img = textRegionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        painter->drawPolygon(item->mapToScene(local));
                    }
                }
                // Rubber-band text selection (cyan).
                if (m_textLayer.hasSelection()) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor(60, 160, 255, 100));
                    for (int idxSel : m_textLayer.selectedRegionsRef()) {
                        if (idxSel < 0 || idxSel >= m_textLayer.regionCount()) {
                            continue;
                        }
                        const auto &r = m_textLayer.regionAt(idxSel);
                        const QRectF img = textRegionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        painter->drawPolygon(item->mapToScene(local));
                    }
                }
                if (m_textLayer.showsRegions()) {
                    painter->setBrush(Qt::NoBrush);
                    for (const ThumtooCache::TextRegion &r : m_textLayer.regions()) {
                        const QRectF img = textRegionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        const QPolygonF scenePoly = item->mapToScene(local);
                        if (r.role == ThumtooCache::TextRegion::Role::Link) {
                            QPen pen(QColor(40, 180, 80, 200));
                            pen.setCosmetic(true);
                            pen.setWidthF(0);
                            painter->setPen(pen);
                        } else {
                            QPen pen(QColor(220, 80, 40, 180));
                            pen.setCosmetic(true);
                            pen.setWidthF(0);
                            painter->setPen(pen);
                        }
                        painter->drawPolygon(scenePoly);
                    }
                }
                painter->restore();
            }
        }
    }

    // Viewport-space overlays on the same painter as the scene (required for
    // QOpenGLWidget: a second QPainter(viewport()) after paintEvent whites out).
    if (!painter) {
        return;
    }
    // Gallery selection frames: scene-space overlay so item ItemCoordinateCache
    // is not invalidated on select or scroll (was painted inside ImageItem::paint).
    if (isGalleryMode()) {
        paintGallerySelectionFrames(painter, rect);
    }
    // Bare Gallery: skip HUD/edges/slideshow overlay pass.
    if (isGalleryMode() && !m_hudPrefs.isVisible() && !m_hudFlash.isVisible() && !m_hudFlash.isIdentityPulse()
        && !m_slideshow.hud().isPausedHud() && !gallerySizeResolveActive()
        && m_centreProgress.titleRef().isEmpty()
        && m_hoverEdge == EdgeZone::None && !m_cropCtrl.session().active()
        && !m_slideshow.dwell().isMotionActive() 
        ) {
        return;
    }
    painter->save();
    painter->resetTransform();
    if (viewport()) {
        const qreal dpr = viewport()->devicePixelRatioF();
        if (!qFuzzyCompare(dpr, 1.0)) {
            painter->scale(dpr, dpr);
        }
    }
    paintViewportOverlays(*painter);
    painter->restore();
}


