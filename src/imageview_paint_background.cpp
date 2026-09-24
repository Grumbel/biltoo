// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "util/biltoo_thread.h"
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
#include "view/canvasbackground.h"

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
            painter->fillRect(rect, m_shell.canvasBg().primaryColor());
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
            painter->fillRect(rect, wb.color.isValid() ? wb.color : m_shell.canvasBg().primaryColor());
        } else if (wb.mode == WorkspaceBackgroundMode::Checkerboard) {
            const QColor a = wb.color.isValid() ? wb.color : m_shell.canvasBg().primaryColor();
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
                if (!item) {
                    const SessionImageId sid = m_session.identity().currentIdValue();
                    if (sid != kInvalidSessionImageId) {
                        item = findItemBySessionId(sid);
                    }
                }
                if (!item && m_image.hasClassicPath()) {
                    item = findItemForPath(m_image.classicPath());
                }
                if (item) {
                    src = item->displayImage();
                    if (src.isNull()) {
                        src = item->sourceImage();
                    }
                    path = item->path();
                }
                if (path.isEmpty() && m_image.hasClassicPath()) {
                    path = m_image.classicPath();
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
                const QColor fill = wb.color.isValid() ? wb.color : m_shell.canvasBg().primaryColor();
                painter->fillRect(rect, fill);
            }
        } else {
            painter->fillRect(rect, m_shell.canvasBg().primaryColor());
        }
    };

    const bool wsOverride = isWorkspaceMode()
        && !m_shell.canvasBg().isWorkspaceAppDefault()
        && !m_shell.canvasBg().isWorkspaceShowDefault();
    const bool viewOverride =
        (isGalleryMode() || isImageMode()) && !m_shell.canvasBg().isViewAppDefault();

    if (wsOverride) {
        paintMaterial(m_shell.canvasBg().workspaceRef(), m_shell.canvasBg().workspaceTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_shell.canvasBg().setWorkspaceTile(px, path);
                      });
    } else if (viewOverride) {
        paintMaterial(m_shell.canvasBg().viewRef(), m_shell.canvasBg().viewTile,
                      [this](const QPixmap &px, const QString &path) {
                          m_shell.canvasBg().setViewTile(px, path);
                      });
    } else {
        if (!m_shell.canvasBg().useChecker(isWorkspaceMode())) {
            painter->fillRect(rect, m_shell.canvasBg().primaryColor());
        } else {
            fillChecker(m_shell.canvasBg().primaryColor(), m_shell.canvasBg().checkerAlt());
        }
    }
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    GUI_BUDGET("ImageView::drawBackground");
    paintCanvasBackground(painter, rect, transform().m11());

    // Virtualized Gallery: packed cell frames under items (slots with no live
    // ImageItem yet, or while soft pixels are still loading).
    if (isGalleryMode() && painter) {
        m_gallery.paintVirtualPlaceholders(painter, rect);
    }

    // Page guide paper (under images): plain white sheet in scene units.
    if (m_workspace.pageGuideSession().isVisible() && isWorkspaceMode()) {
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
    GUI_BUDGET("ImageView::drawForeground");
    // Page guide outline above images so the frame stays visible when tiles
    // cover the white sheet (scene coordinates).
    if (m_workspace.pageGuideSession().isVisible() && isWorkspaceMode()) {
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
    if (isImageMode() && m_textCtrl.session().hasRegions()
        && (m_textCtrl.session().showsRegions() || m_textCtrl.session().hasSearchMatches())) {
        if (ImageItem *item = primaryItem()) {
            const QSize sz = item->imageSize();
            if (sz.width() > 0 && sz.height() > 0 && m_textCtrl.session().pageBoundsValid()) {
                painter->save();
                // Search hits: filled yellow first (under outlines / selection).
                if (m_textCtrl.session().hasSearchMatches()) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor(255, 220, 40, 110));
                    for (int idxMatch : m_textCtrl.session().searchMatchesRef()) {
                        if (idxMatch < 0 || idxMatch >= m_textCtrl.session().regionCount()) {
                            continue;
                        }
                        const auto &r = m_textCtrl.session().regionAt(idxMatch);
                        const QRectF img = m_textCtrl.regionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        painter->drawPolygon(item->mapToScene(local));
                    }
                }
                // Rubber-band text selection (cyan).
                if (m_textCtrl.session().hasSelection()) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor(60, 160, 255, 100));
                    for (int idxSel : m_textCtrl.session().selectedRegionsRef()) {
                        if (idxSel < 0 || idxSel >= m_textCtrl.session().regionCount()) {
                            continue;
                        }
                        const auto &r = m_textCtrl.session().regionAt(idxSel);
                        const QRectF img = m_textCtrl.regionImageRect(r);
                        if (img.isEmpty()) {
                            continue;
                        }
                        const QRectF local = img.translated(item->offset());
                        painter->drawPolygon(item->mapToScene(local));
                    }
                }
                if (m_textCtrl.session().showsRegions()) {
                    painter->setBrush(Qt::NoBrush);
                    for (const ThumtooCache::TextRegion &r : m_textCtrl.session().regions()) {
                        const QRectF img = m_textCtrl.regionImageRect(r);
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
    if (isGalleryMode() && !m_hud.appearance().isVisible() && !m_hud.flash().isVisible() && !m_hud.flash().isIdentityPulse()
        && !m_slideshow.hud().isPausedHud() && !hostGallerySizeResolve().active()
        && m_hud.centreProgress().titleRef().isEmpty()
        && hostHoverEdge() == EdgeZone::None && !m_cropCtrl.session().active()
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

// --- Background settings (was imageview_background.cpp) ---

void ImageView::setBackgroundColor(const QColor &color)
{
    if (!m_shell.canvasBg().setColor(color)) {
        return;
    }
    setBackgroundBrush(QBrush(m_shell.canvasBg().primaryColor()));
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setBackgroundColorAlt(const QColor &color)
{
    if (!m_shell.canvasBg().setColorAlt(color)) {
        return;
    }
    viewport()->update();
}


void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    if (!m_shell.canvasBg().setPattern(pattern)) {
        return;
    }
    viewport()->update();
}


void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    if (!m_shell.canvasBg().setCheckerWorkspaceOnly(on)) {
        return;
    }
    viewport()->update();
}


void ImageView::setWorkspaceBackground(const WorkspaceBackground &bg)
{
    // No-op when durable fields match: leave temporary "show default" preview
    // alone so the toolbar toggle does not desync from paint.
    if (!m_shell.canvasBg().setWorkspace(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_shell.canvasBg().workspaceTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_shell.canvasBg().setWorkspaceTile(px, bg.imagePath);
            }
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::clearWorkspaceBackground()
{
    WorkspaceBackground def;
    setWorkspaceBackground(def);
}


void ImageView::setWorkspaceBackgroundShowDefault(bool on)
{
    if (!m_shell.canvasBg().setWorkspaceShowDefault(on)) {
        return;
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setViewBackground(const WorkspaceBackground &bg)
{
    if (!m_shell.canvasBg().setView(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_shell.canvasBg().viewTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_shell.canvasBg().setViewTile(px, bg.imagePath);
            }
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}
