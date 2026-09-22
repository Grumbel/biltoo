// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"
#include "itemframegeometry.h"
#include "itemhandlepolicy.h"
#include "displayquality.h"
#include "biltoo_thread.h"

#include <cstdlib>
#include <cmath>
#include "tilelod/tile_lod_controller.hpp"
#include "tilelod/tile_lod_registry.hpp"
#include "thumtoocache.h"
#include "imagecache.h"
#include "contentxform.h"
#include "coloradjust.h"

#include <QCoreApplication>
#include "placementlinear.h"
#include "viewtransform.h"
#include "imageview.h"

#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLineF>
#include <QMetaObject>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QTimer>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

namespace {

bool tilePlanDebugOverlayEnabled()
{
    if (ImageCache::debugOverlayEnabled()) {
        return true;
    }
    const char *e = std::getenv("BILTOO_TILE_DEBUG");
    return e && e[0] && e[0] != '0';
}

/** SmoothPixmapTransform is expensive; skip at near-integer device zoom of the
 *  target scale (1:1 / 2:1 tile pixels). Parent stand-ins still need smooth. */
bool tilePaintNeedsSmooth(double devicePerContent, int targetScale,
                          const tilelod::DrawPlan &plan)
{
    for (const tilelod::DrawCommand &cmd : plan.commands) {
        if (cmd.kind == tilelod::DrawKind::CoarserTile) {
            return true;
        }
    }
    if (!(devicePerContent > 0.0) || targetScale < 0) {
        return true;
    }
    const int s = qMin(targetScale, 12);
    const double dpp = devicePerContent * static_cast<double>(1 << s);
    const double nearest = std::round(dpp);
    if (nearest >= 1.0 && std::abs(dpp - nearest) < 0.08) {
        return false;
    }
    return true;
}

/**
 * Tile-plan debug overlay: semi-transparent washes by source so the image
 * still shows through, plus a summary plate.
 *
 *   Exact (target-scale cache)  — yellow
 *   Parent (coarser cache)      — amber / orange
 *   Hole (soft / ladder under)  — cyan
 */
void paintTilePlanDebugOverlay(QPainter *painter, tilelod::TileSession *session,
                               const tilelod::DrawPlan &plan,
                               const QRectF &contentBounds,
                               const QPointF &contentOffset,
                               const QSize &nativeSize,
                               const ContentXform::Value &xform,
                               bool freeRotPainter)
{
    if (!painter || !session) {
        return;
    }
    painter->save();
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QTransform dt = painter->deviceTransform();
    const qreal sx = ViewTransform::scaleFrom(dt);

    // Yellow = exact tile, orange = coarser parent, cyan = underlay hole.
    // Host-side coverage debug only. Durable TILE text on pixels belongs in
    // thumtoo (bitmap stamps rotate/flip with the patch automatically).
    constexpr int kWashAlpha = 90;
    const QColor fillExact(255, 220, 40, kWashAlpha);
    const QColor fillParent(255, 140, 20, kWashAlpha);
    const QColor fillHole(40, 200, 255, kWashAlpha);
    const QColor edgeExact(255, 230, 60);
    const QColor edgeParent(255, 160, 40);
    const QColor edgeHole(60, 220, 255);

    const int target = session->target_scale();

    for (const tilelod::DrawCommand &cmd : plan.commands) {
        // Same mapping as tile paint: source content rect → oriented display.
        const QRectF srcBox(cmd.dst_content.x, cmd.dst_content.y,
                            cmd.dst_content.w, cmd.dst_content.h);
        if (srcBox.isEmpty()) {
            continue;
        }
        QRectF disp = ContentXform::mapSourceRectToOriented(srcBox, nativeSize, xform);
        if (disp.isEmpty()) {
            continue;
        }
        if (xform.hasCrop && !xform.cropRect.isEmpty()) {
            const QRect contentCrop = xform.cropRect.normalized();
            const QRectF local = disp.translated(-contentCrop.x(), -contentCrop.y());
            const QRectF cropLocal(0.0, 0.0, contentCrop.width(), contentCrop.height());
            disp = local.intersected(cropLocal);
            if (disp.isEmpty()) {
                continue;
            }
        }
        // Axis-aligned paint adds item offset(); free-rot uses painter xform only.
        QRectF dst = disp;
        if (!freeRotPainter) {
            dst = QRectF(disp.x() + contentOffset.x(), disp.y() + contentOffset.y(),
                         disp.width(), disp.height());
        }
        if (dst.isEmpty()) {
            continue;
        }

        QColor fill;
        QColor edge;
        int scale = cmd.src_key.scale;
        const int tx = cmd.src_key.x;
        const int ty = cmd.src_key.y;
        if (cmd.kind == tilelod::DrawKind::ExactTile) {
            fill = fillExact;
            edge = edgeExact;
        } else if (cmd.kind == tilelod::DrawKind::CoarserTile) {
            fill = fillParent;
            edge = edgeParent;
        } else if (cmd.kind == tilelod::DrawKind::Underlay) {
            fill = fillHole;
            edge = edgeHole;
            scale = target;
        } else {
            continue;
        }
        painter->fillRect(dst, fill);
        painter->setPen(QPen(edge, 0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(dst);

        // Pixel TILE text is stamped by thumtoo on the bitmap (source space;
        // orient follows the patch). Host overlay is coverage wash only.

    }

    QFont pf = painter->font();
    pf.setBold(true);
    pf.setStyleHint(QFont::SansSerif);
    pf.setFamily(QStringLiteral("Sans Serif"));
    pf.setPixelSize(ViewTransform::overlayFontPixelSize(sx));
    painter->setFont(pf);
    const QFontMetrics fm(pf);
    const tilelod::TileSession::Coverage cov = session->coverage();
    QStringList summary;
    summary << QStringLiteral("TILE s=%1").arg(target);
    if (cov.fully_covered()) {
        summary << QStringLiteral("COMPLETE");
    } else if (cov.in_flight > 0) {
        summary << QStringLiteral("LOADING");
    } else {
        summary << QStringLiteral("WAITING");
    }
    int blockW = 0;
    for (const QString &line : summary) {
        blockW = qMax(blockW, fm.horizontalAdvance(line));
    }
    const int pad = qMax(2, pf.pixelSize() / 4);
    const int lineH = fm.height();
    const int blockH = lineH * summary.size() + pad * 2;
    blockW += pad * 2;
    const QRectF plate(contentBounds.left() + 4.0,
                       contentBounds.top() + 4.0,
                       blockW + 2.0,
                       blockH + 2.0);
    painter->fillRect(plate, QColor(0, 0, 0, 200));
    for (int i = 0; i < summary.size(); ++i) {
        painter->setPen(QColor(255, 255, 255));
        painter->drawText(QPointF(plate.left() + pad,
                                  plate.top() + pad + (i + 1) * lineH - fm.descent()),
                          summary.at(i));
    }
    painter->restore();
}

} // namespace

QRectF ImageItem::boundingRect() const
{
    // Gallery Grid-Crop: report the cell, not the full pixmap, so layout/hit-tests match.
    if (!m_galleryCellSize.isEmpty()) {
        const QRectF clip = galleryClipLocal();
        if (!clip.isEmpty()) {
            return clip;
        }
    }
    // Modest expansion for external handles when selected. Precise hit-testing is
    // view-owned (viewport distances); this AABB only prevents scene clipping of
    // the painted chrome. Clamp pad so near-zero item scale cannot explode the
    // local rect to infinity (HANDLES.md).
    QRectF r = contentRect();
    if (isSelected() && m_interactive) {
        // Modest constant local pad only. Do not divide by view/item scale:
        // zoom used to change this AABB and QGraphicsView would scroll to keep
        // the selected item on screen, fighting free-form pan/zoom. Chrome is
        // painted in viewport space; hit-testing is view-owned.
        const qreal content = qMax(r.width(), r.height());
        const qreal pad = ItemFrameGeometry::selectedChromePad(content);
        r.adjust(-pad, -pad, pad, pad);
    }
    return r;
}

QPainterPath ImageItem::shape() const
{
    // Content body only. Transform chrome is hit-tested by ImageView in viewport
    // space (selected-item loop + handleAt view-pixel distances). Expanding the
    // item shape with /deviceScaleMin local radii caused the path to explode when
    // scale approached zero (HANDLES.md). External handles remain reachable
    // because the view queries selected items explicitly.
    QPainterPath path;
    if (!m_galleryCellSize.isEmpty()) {
        const QRectF clip = galleryClipLocal();
        if (!clip.isEmpty()) {
            path.addRect(clip);
            return path;
        }
    }
    path.addRect(contentRect());
    return path;
}

QPointF ImageItem::sceneToViewPx(const QPointF &scenePt) const
{
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            return QPointF(views.first()->mapFromScene(scenePt));
        }
    }
    return scenePt;
}

QPointF ImageItem::localToViewPx(const QPointF &local) const
{
    return sceneToViewPx(mapToScene(local));
}

qreal ImageItem::screenScale() const
{
    // Max stretch of local→view (draw chrome ~constant on screen).
    QTransform t = transform();
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            t = views.first()->transform() * t;
        }
    }
    qreal sMax = 1.0, sMin = 1.0;
    PlacementLinear::singularValues2x2(t.m11(), t.m12(), t.m21(), t.m22(), &sMax, &sMin);
    return ViewTransform::floorScale(sMax);
}

QPointF ImageItem::scaleAnchorLocal(Handle h) const
{
    // Opposite corner/edge — kept fixed when not scaling from the centre.
    using A = PlacementLinear::ContentAnchor;
    PlacementLinear::ContentAnchor anchor = A::Center;
    switch (h) {
    case Handle::ScaleTopLeft:
        anchor = A::BottomRight;
        break;
    case Handle::ScaleTopRight:
        anchor = A::BottomLeft;
        break;
    case Handle::ScaleBottomLeft:
        anchor = A::TopRight;
        break;
    case Handle::ScaleBottomRight:
        anchor = A::TopLeft;
        break;
    case Handle::ScaleTop:
    case Handle::ShearTop:
        anchor = A::BottomMid;
        break;
    case Handle::ScaleBottom:
    case Handle::ShearBottom:
        anchor = A::TopMid;
        break;
    case Handle::ScaleLeft:
    case Handle::ShearLeft:
        anchor = A::RightMid;
        break;
    case Handle::ScaleRight:
    case Handle::ShearRight:
        anchor = A::LeftMid;
        break;
    default:
        break;
    }
    return PlacementLinear::contentAnchorPoint(contentRect(), anchor);
}

void ImageItem::applyScaleHandleDrag(const QPointF &scenePos, Qt::KeyboardModifiers mods,
                                          HandlePressScratch &press)
{
    // Corner and edge (H/V) handles share modifier semantics (drawing-program usual):
    // default scales about the opposite corner/edge; Ctrl or Shift about centre.
    //
    // Edge stretch uses scene-space projections onto the *press-time* image axes
    // so the ratio stays stable when scale approaches the clamp (HANDLES.md).
    // Pose writes go through applyPlacement (Stage 2 single writer).
    const bool modifier = mods & (Qt::ControlModifier | Qt::ShiftModifier);
    const bool fromCenter = modifier;
    const QPointF itemCentre = this->scenePos();
    constexpr qreal kMinDist = 1.0; // scene px; below this ignore the sample

    const Handle h = press.handle;
    ItemComponents::Placement pl = press.placement;

    if (ItemHandlePolicy::isCornerScaleHandle(h)) {
        if (fromCenter) {
            const qreal d0 = QLineF(itemCentre, press.scenePos).length();
            const qreal d1 = QLineF(itemCentre, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = PlacementLinear::uniformScaleFactor(d0, d1, kMinDist);
                pl.scale = press.placement.scale * f;
                pl.scaleY = press.placement.scaleY * f;
                applyPlacement(pl);
            }
        } else {
            const QPointF anchor = press.anchorScene;
            const qreal d0 = QLineF(anchor, press.scenePos).length();
            const qreal d1 = QLineF(anchor, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = PlacementLinear::uniformScaleFactor(d0, d1, kMinDist);
                pl.scale = press.placement.scale * f;
                pl.scaleY = press.placement.scaleY * f;
                applyPlacement(pl);
                const QPointF now = mapToScene(press.anchorLocal);
                pl.pos = pos() + (anchor - now);
                applyPlacement(pl);
            }
        }
        return;
    }

    if (ItemHandlePolicy::isEdgeScaleHandle(h)) {
        // Image axes in *scene* at press — must match PlacementLinear/Qt (not a
        // textbook CCW formula; Qt rotate is clockwise with Y-down).
        const QTransform Lpress = PlacementLinear::make(
            press.placement.scale, press.placement.scaleY, press.placement.shear, press.placement.rotation);
        const QPointF axisX = Lpress.map(QPointF(1.0, 0.0));
        const QPointF axisY = Lpress.map(QPointF(0.0, 1.0));

        qreal sx = press.placement.scale;
        qreal sy = press.placement.scaleY;
        const bool stretchX = (h == Handle::ScaleLeft
                               || h == Handle::ScaleRight);

        if (fromCenter) {
            const QPointF v0 = press.scenePos - itemCentre;
            const QPointF v1 = scenePos - itemCentre;
            if (stretchX) {
                sx = PlacementLinear::axisScaleFromProjection(press.placement.scale, v0, v1, axisX);
            } else {
                sy = PlacementLinear::axisScaleFromProjection(press.placement.scaleY, v0, v1, axisY);
            }
        } else {
            // Anchor fixed: project (pointer - anchor) onto the stretch axis.
            const QPointF anchor = press.anchorScene;
            const QPointF v0 = press.scenePos - anchor;
            const QPointF v1 = scenePos - anchor;
            if (stretchX) {
                sx = PlacementLinear::axisScaleFromProjection(press.placement.scale, v0, v1, axisX);
            } else {
                sy = PlacementLinear::axisScaleFromProjection(press.placement.scaleY, v0, v1, axisY);
            }
        }
        pl.scale = sx;
        pl.scaleY = sy;
        // Scale-only drag must not disturb shear (already in pl).
        applyPlacement(pl);
        if (!fromCenter) {
            const QPointF now = mapToScene(press.anchorLocal);
            pl.pos = pos() + (press.anchorScene - now);
            applyPlacement(pl);
        }
    }
}

void ImageItem::applyShearHandleDrag(const QPointF &scenePos, HandlePressScratch &press)
{
    // Canonical pose is R·H(kx)·S with only horizontal shear. Top/Bottom grips
    // edit kx directly. Left/Right grips apply a *vertical* local shear V(m)
    // (tilt top/bottom edges), then re-decompose L·V into R·H·S so the same
    // 4-DOF model holds.
    //
    // Horizontal: H(kx) maps (x,y)→(x+kx·y, y); e2' = e2 + kx·e1 (via L·H).
    // Vertical:   V(m)  maps (x,y)→(x, y+m·x); e1' = e1 + m·e2  (via L·V).
    const Handle h = press.handle;
    const QPointF gripLocal = handleCenter(h);
    const QPointF anchor = press.anchorScene;

    QPointF e1, e2;
    PlacementLinear::unitAxes(press.placement.scale, press.placement.scaleY, press.placement.shear,
                              press.placement.rotation, &e1, &e2);

    const bool verticalEdge = (h == Handle::ShearLeft
                               || h == Handle::ShearRight);

    ItemComponents::Placement pl = press.placement;

    if (!verticalEdge) {
        // Top / Bottom: edit horizontal shear kx. Motion along local +X.
        QPointF dirX = e1;
        const qreal lenX = qHypot(dirX.x(), dirX.y());
        if (lenX > 1e-9) {
            dirX /= lenX;
        }
        const qreal yLever = gripLocal.y();
        const qreal len0 = QPointF::dotProduct(press.scenePos - anchor, dirX);
        const qreal len1 = QPointF::dotProduct(scenePos - anchor, dirX);
        // L·H(kx): e2_new = e2 + kx·e1 ⇒ scene move of a point with local y
        // along e1 is proportional to kx · |e1| · y (≈ sx·kx·y).
        pl.shear = PlacementLinear::horizontalShearFromDrag(
            press.placement.shear, press.placement.scale, yLever, len0, len1);
    } else {
        // Left / Right: vertical local shear m via L·V(m), then decompose.
        QPointF dirY = e2;
        const qreal lenY = qHypot(dirY.x(), dirY.y());
        if (lenY > 1e-9) {
            dirY /= lenY;
        }
        const qreal xLever = gripLocal.x();
        const qreal len0 = QPointF::dotProduct(press.scenePos - anchor, dirY);
        const qreal len1 = QPointF::dotProduct(scenePos - anchor, dirY);
        // L·V(m): e1_new = e1 + m·e2 ⇒ move along e2 ∝ m · |e2| · x (≈ sy·m·x).
        const qreal m = PlacementLinear::verticalShearParamFromDrag(
            press.placement.scaleY, xLever, len0, len1);
        // Compose vertical shear onto press axes: e1' = e1 + m·e2, e2' = e2.
        const QPointF e1n = e1 + m * e2;
        const QPointF e2n = e2;
        qreal sx = press.placement.scale;
        qreal sy = press.placement.scaleY;
        qreal kx = press.placement.shear;
        qreal rot = press.placement.rotation;
        if (PlacementLinear::decomposeAxes(e1n, e2n, &sx, &sy, &kx, &rot)) {
            pl.scale = sx;
            pl.scaleY = sy;
            pl.shear = kx;
            pl.rotation = rot;
        }
    }

    applyPlacement(pl);
    const QPointF now = mapToScene(press.anchorLocal);
    pl.pos = pos() + (anchor - now);
    applyPlacement(pl);
}

QRectF ImageItem::opacitySliderRect() const
{
    // Approximate local rect for legacy callers only. Paint / hit / drag use
    // ItemFrameGeometry::opacityTrackView() in viewport space (left outside, vertical, adaptive).
    const QRectF r = contentRect();
    const qreal h = qMin(r.height() * 0.4, r.height());
    const qreal w = qMin(8.0, r.width() * 0.1);
    return QRectF(r.left() - w - 4.0, r.bottom() - h - 4.0, w, h);
}

void ImageItem::setOpacityFromSliderPos(const QPointF &scenePos)
{
    QGraphicsView *view = nullptr;
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty()) {
            view = views.first();
        }
    }
    if (!view) {
        return;
    }
    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };
    const QRectF localRect = contentRect();
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    if (QLineF(tl, br).length() < ItemFrameGeometry::kMinFrameDiagPx) {
        return;
    }
    const ItemFrameGeometry::FrameViewGeom fg = ItemFrameGeometry::makeFrameViewGeom(tl, tr, br, bl);
    QPointF a, b;
    ItemFrameGeometry::opacityTrackView(fg, &a, &b);
    const QPointF p = sceneToViewPx(scenePos);
    const qreal tval = ItemFrameGeometry::trackParam(a, b, p);
    {
        ItemComponents::Placement pl = placement();
        pl.opacity = ItemFrameGeometry::opacityFromTrackParam(tval);
        applyPlacement(pl);
    }
}

QPointF ImageItem::handleCenter(Handle h) const
{
    // Local attachment points. Scale handles sit on the pixmap rect (align + rotate
    // with the image). Rotate / chrome use *clamped* local offsets so near-zero
    // scale cannot send centres to infinity; paint and hit-testing place the true
    // constant-screen-distance positions in viewport space (see paintInteractionChrome
    // and handleAt). Opacity uses the interior track.
    const QRectF r = contentRect();
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal content = qMax(r.width(), r.height());
    const qreal maxOff = ItemFrameGeometry::maxChromeOffset(content);

    auto axisScreenPerLocal = [this](const QPointF &localAxis) -> qreal {
        const QPointF o = localToViewPx(QPointF(0, 0));
        const QPointF p = localToViewPx(localAxis);
        const qreal len = QLineF(o, p).length();
        return qMax(1e-6, len);
    };
    const qreal sx = axisScreenPerLocal(QPointF(1, 0));
    const qreal sy = axisScreenPerLocal(QPointF(0, 1));

    const qreal rotOffX = qMin(ItemFrameGeometry::kRotateOffsetPx / sx, maxOff);
    const qreal rotOffY = qMin(ItemFrameGeometry::kRotateOffsetPx / sy, maxOff);
    const qreal btn = qMin(ItemFrameGeometry::kChromeBtnScreenPx / sy, maxOff / 6.0);
    const qreal gap = qMin(ItemFrameGeometry::kChromeBtnGapPx / sy, maxOff / 12.0);
    const qreal outX = qMin((ItemFrameGeometry::kChromeOutsidePx + ItemFrameGeometry::kChromeBtnScreenPx * 0.5) / sx, maxOff / 3.0);

    // Outside top-right (approx local; paint/hit use chromeCentersView).
    const qreal stackX = r.right() + outX;
    const qreal stackTop = r.top() + 4.0 / sy;
    auto chromeBtnCenter = [&](int index) {
        // Approximate local centres; precise hit/paint use chromeCentersView.
        return QPointF(stackX, stackTop + index * (btn + gap) + btn / 2.0);
    };

    switch (h) {
    case Handle::ScaleTopLeft:
        return r.topLeft();
    case Handle::ScaleTopRight:
        return r.topRight();
    case Handle::ScaleBottomLeft:
        return r.bottomLeft();
    case Handle::ScaleBottomRight:
        return r.bottomRight();
    case Handle::ScaleTop:
        return QPointF(cx, r.top());
    case Handle::ScaleRight:
        return QPointF(r.right(), cy);
    case Handle::ScaleBottom:
        return QPointF(cx, r.bottom());
    case Handle::ScaleLeft:
        return QPointF(r.left(), cy);
    case Handle::ShearTop:
        // Offset along edge so scale mid-handle and shear grip do not coincide.
        return QPointF(cx - r.width() * 0.22, r.top());
    case Handle::ShearBottom:
        return QPointF(cx + r.width() * 0.22, r.bottom());
    case Handle::ShearLeft:
        return QPointF(r.left(), cy - r.height() * 0.22);
    case Handle::ShearRight:
        return QPointF(r.right(), cy + r.height() * 0.22);
    case Handle::RotateTop:
        return QPointF(cx, r.top() - rotOffY);
    case Handle::RotateRight:
        return QPointF(r.right() + rotOffX, cy);
    case Handle::RotateBottom:
        return QPointF(cx, r.bottom() + rotOffY);
    case Handle::RotateLeft:
        return QPointF(r.left() - rotOffX, cy);
    case Handle::FlipH:
        return chromeBtnCenter(0);
    case Handle::FlipV:
        return chromeBtnCenter(1);
    case Handle::Rotate90CCW:
        return chromeBtnCenter(2);
    case Handle::Rotate90CW:
        return chromeBtnCenter(3);
    case Handle::Raise:
        return chromeBtnCenter(4);
    case Handle::Lower:
        return chromeBtnCenter(5);
    case Handle::ResetScale:
        return chromeBtnCenter(6);
    case Handle::ResetRotation:
        return chromeBtnCenter(7);
    case Handle::ResetShear:
        return chromeBtnCenter(8);
    case Handle::OpacitySlider:
        return opacitySliderRect().center();
    default:
        return QPointF();
    }
}

ImageItem::Handle ImageItem::handleAt(const QPointF &itemPos) const
{
    if (!isSelected() || !m_interactive) {
        return Handle::None;
    }

    // All distances in viewport pixels so rotation / anisotropic scale / near-zero
    // item scale cannot break hit testing (HANDLES.md). Centres for rotate and
    // chrome match paintInteractionChrome exactly.
    QGraphicsView *view = nullptr;
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty()) {
            view = views.first();
        }
    }
    if (!view) {
        return Handle::None;
    }

    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };

    const QRectF localRect = contentRect();
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    const QPointF p = toView(itemPos);

    // Degenerate frame: no chrome hits (matches paint early-out).
    if (QLineF(tl, br).length() < ItemFrameGeometry::kMinFrameDiagPx) {
        return Handle::None;
    }

    const ItemFrameGeometry::FrameViewGeom fg =
        ItemFrameGeometry::makeFrameViewGeom(tl, tr, br, bl);

    // Opacity + chrome: same adaptive outside layout as paint.
    {
        QPointF a, b;
        ItemFrameGeometry::opacityTrackView(fg, &a, &b);
        if (ItemFrameGeometry::distanceToSegment(a, b, p)
            <= ItemFrameGeometry::kChromeHitScreenPx) {
            return Handle::OpacitySlider;
        }

        QPointF centers[ItemFrameGeometry::kChromeCount];
        ItemFrameGeometry::chromeCentersView(fg, centers);
        const Handle chromeHandles[] = {
            Handle::FlipH, Handle::FlipV, Handle::Rotate90CCW, Handle::Rotate90CW,
            Handle::Raise, Handle::Lower, Handle::ResetScale, Handle::ResetRotation,
            Handle::ResetShear
        };
        Handle best = Handle::None;
        qreal bestDist = 1e300;
        for (int i = 0; i < ItemFrameGeometry::kChromeCount; ++i) {
            const bool isToggle = (chromeHandles[i] == Handle::FlipH
                                   || chromeHandles[i] == Handle::FlipV);
            if (isToggle) {
                // Rounded-square toggles: axis-aligned hit box (viewport px).
                const qreal half = ItemFrameGeometry::kChromeBtnScreenPx * 0.55 + 3.0;
                const qreal dx = qAbs(p.x() - centers[i].x());
                const qreal dy = qAbs(p.y() - centers[i].y());
                if (dx <= half && dy <= half) {
                    const qreal d = qMax(dx, dy);
                    if (d <= bestDist) {
                        bestDist = d;
                        best = chromeHandles[i];
                    }
                }
            } else {
                const qreal d = QLineF(p, centers[i]).length();
                if (d <= ItemFrameGeometry::kChromeHitScreenPx && d <= bestDist) {
                    bestDist = d;
                    best = chromeHandles[i];
                }
            }
        }
        if (best != Handle::None) {
            return best;
        }
    }

    // Scale corners + rotate knobs
    Handle best = Handle::None;
    qreal bestDist = ItemFrameGeometry::kHandleScreenPx * 1.75;
    struct PointH {
        Handle h;
        QPointF c;
    };
    QList<PointH> points;
    if (m_scaleHandlesEnabled) {
        points << PointH{Handle::ScaleTopLeft, tl}
               << PointH{Handle::ScaleTopRight, tr}
               << PointH{Handle::ScaleBottomRight, br}
               << PointH{Handle::ScaleBottomLeft, bl};
    }
    QPointF rotPts[4];
    ItemFrameGeometry::rotateHandlePoints(fg, rotPts);
    points << PointH{Handle::RotateTop, rotPts[0]}
           << PointH{Handle::RotateRight, rotPts[1]}
           << PointH{Handle::RotateBottom, rotPts[2]}
           << PointH{Handle::RotateLeft, rotPts[3]};
    for (const PointH &ph : points) {
        const qreal d = QLineF(p, ph.c).length();
        if (d <= bestDist) {
            bestDist = d;
            best = ph.h;
        }
    }

    // Edge stretch bars (short segment around mid-edge in view space)
    if (m_scaleHandlesEnabled) {
        const qreal halfLen = ItemFrameGeometry::kHandleScreenPx * 1.2;
        const qreal edgeHit = ItemFrameGeometry::kHandleScreenPx * 0.85;
        struct EdgeH {
            Handle h;
            QPointF mid;
            QPointF along;
        };
        const EdgeH edges[] = {
            {Handle::ScaleTop, fg.midTop, fg.dirTop},
            {Handle::ScaleRight, fg.midRight, fg.dirRight},
            {Handle::ScaleBottom, fg.midBottom, fg.dirBottom},
            {Handle::ScaleLeft, fg.midLeft, fg.dirLeft},
        };
        for (const EdgeH &ed : edges) {
            const QPointF a = ed.mid - ed.along * halfLen;
            const QPointF b = ed.mid + ed.along * halfLen;
            const qreal d = ItemFrameGeometry::distanceToSegment(a, b, p);
            if (d <= edgeHit && d <= bestDist) {
                bestDist = d;
                best = ed.h;
            }
        }
        // Shear diamonds: offset along each edge from mid (view space).
        QPointF shearPts[4];
        ItemFrameGeometry::shearHandlePoints(fg, shearPts);
        const Handle shearHs[] = {
            Handle::ShearTop, Handle::ShearBottom,
            Handle::ShearLeft, Handle::ShearRight,
        };
        for (int i = 0; i < 4; ++i) {
            const qreal d = QLineF(p, shearPts[i]).length();
            if (d <= ItemFrameGeometry::kHandleScreenPx * 1.1 && d <= bestDist) {
                bestDist = d;
                best = shearHs[i];
            }
        }
    }
    return best;
}

bool ImageItem::hasHandleAt(const QPointF &itemPos) const
{
    return handleAt(itemPos) != Handle::None;
}

void ImageItem::notifyViewStatus()
{
    if (!scene()) {
        return;
    }
    for (QGraphicsView *v : scene()->views()) {
        QMetaObject::invokeMethod(v, "refreshStatus", Qt::DirectConnection);
        // Chrome lives in the viewport paint path (flip toggles, etc.).
        if (v->viewport()) {
            v->viewport()->update();
        }
    }
}

void ImageItem::activateChromeHandle(Handle h)
{
    switch (h) {
    case Handle::FlipH:
    case Handle::FlipV:
    case Handle::Rotate90CCW:
    case Handle::Rotate90CW: {
        if (scene()) {
            for (QGraphicsView *v : scene()->views()) {
                if (auto *iv = qobject_cast<ImageView *>(v)) {
                    if (h == Handle::FlipH) {
                        iv->bakeItemFlip(this, true, false);
                    } else if (h == Handle::FlipV) {
                        iv->bakeItemFlip(this, false, true);
                    } else if (h == Handle::Rotate90CCW) {
                        iv->rotateContentByQuarterTurns(this, -1);
                    } else {
                        iv->rotateContentByQuarterTurns(this, 1);
                    }
                    if (iv->isGalleryMode()) {
                        iv->hostGallery().applyLayout(GalleryPackReason::ContentChange);
                    } else if (iv->isWorkspaceMode()) {
                        iv->updateWorkspaceSceneRect();
                    }
                    notifyViewStatus();
                    return;
                }
            }
        }
        // Content chrome requires ImageView (bakeItemFlip / rotateContentByQuarterTurns
        // write sparse ItemWorld). Baking pixels alone would desync the store.
        qWarning("ImageItem::activateChromeHandle: no ImageView — content op ignored");
        break;
    }
    case Handle::Raise:
    case Handle::Lower: {
        if (scene()) {
            for (QGraphicsView *v : scene()->views()) {
                if (auto *iv = qobject_cast<ImageView *>(v)) {
                    if (h == Handle::Raise) {
                        iv->raiseItem(this);
                    } else {
                        iv->lowerItem(this);
                    }
                    break;
                }
            }
        }
        break;
    }
    case Handle::ResetScale: {
        ItemComponents::Placement pl = placement();
        pl.scale = 1.0;
        pl.scaleY = 1.0;
        pl.shear = 0.0;
        applyPlacement(pl);
        break;
    }
    case Handle::ResetRotation: {
        ItemComponents::Placement pl = placement();
        pl.rotation = 0.0;
        applyPlacement(pl);
        break;
    }
    case Handle::ResetShear: {
        ItemComponents::Placement pl = placement();
        pl.shear = 0.0;
        applyPlacement(pl);
        break;
    }
    default:
        break;
    }
    // Persist session state + filmstrip (flip / 90° / reset). Raise/Lower only
    // change z-order but still refresh status.
    if (scene()) {
        for (QGraphicsView *v : scene()->views()) {
            if (auto *iv = qobject_cast<ImageView *>(v)) {
                iv->commitItemSessionEdit(this);
                break;
            }
        }
    }
    notifyViewStatus();
}

QVariant ImageItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged) {
        prepareGeometryChange();
        refreshStackingOrder();
    }
    return QGraphicsPixmapItem::itemChange(change, value);
}



void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget)
{
    GUI_BUDGET_MS("ImageItem::paint", 25);
    Q_UNUSED(widget);
    const QRectF crop = galleryClipLocal();
    const bool cropped = !crop.isEmpty();

    // Pixmap at item opacity; handles always fully opaque
    {
        QStyleOptionGraphicsItem opt = *option;
        opt.state &= ~QStyle::State_Selected;
        painter->save();
        if (cropped) {
            painter->setClipRect(crop);
        }
        painter->setOpacity(m_opacity);
        // Samples are drawn into the logical contentRect. Geometry comes from the
        // size probe (SIZE.md); LQIP/soft/full are only textures. Always stretch
        // to the full box so a correct layout does not show a small letterboxed
        // LQIP that later "grows" when soft fills the same rect.
        // Gallery product: LQIP underlay only until tiles cover. Never soft/HOST
        // whole-frame under tileLodWanted cells.
        const bool tilesWanted = tileLodWanted();
        const bool tilesLive = tilesWanted && tileLodActive();
        const bool tilesFullyCover =
            tilesWanted && tileLodViewportCovered();
        // LQIP/soft base until exact target coverage. Interactive Workspace
        // tiles always keep soft under the grid: coverage can report true
        // with an empty plan after zoom on rotated items, which blanked the
        // tile with no soft and no tile paint.
        const bool drawLqipBase = !tilesFullyCover
            || (m_interactive && hasDisplayPixels());
        // Live tiles must not sit under a frozen ItemCoordinateCache pixmap.
        if (tilesLive && cacheMode() != QGraphicsItem::NoCache) {
            setCacheMode(QGraphicsItem::NoCache);
        }

        auto isLqipSample = [](const QImage &img) {
            if (img.isNull()) {
                return false;
            }
            return qMax(img.width(), img.height())
                <= DisplayQuality::kLqipMaxEdge;
        };

        // Gallery packed cells (non-empty galleryCellSize): LQIP-only under
        // tileLodWanted (soft PreferCache removed — GALLERY_PIXELS.md).
        // Image mode also uses m_interactive=false, so do NOT key off that —
        // filmstrip thumbs (>96) must underlay when tiles are wanted but not
        // yet painted (nav-hot skips tile paint).
        const bool galleryLqipOnlyUnderTiles =
            tilesWanted && !m_galleryCellSize.isEmpty();
        auto drawSampleInContentRect = [&](const QImage &img) {
            const QRectF box = contentRect();
            if (img.isNull() || box.width() < 1.0 || box.height() < 1.0) {
                return;
            }
            if (galleryLqipOnlyUnderTiles && !isLqipSample(img)) {
                return;
            }
            painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter->drawImage(box, img);
        };
        if (drawLqipBase) {
            const QRectF box = contentRect();
            // Gallery scroll path: prefer baked QPixmap (ItemCoordinateCache).
            // Avoids QImage stretch every frame under QOpenGLWidget scroll.
            // Self-heal: if displayImage is a strict upgrade over the pixmap
            // (LQIP → larger host while pixmap still LQIP), rebake before draw.
            if (!m_interactive && !pixmap().isNull()
                && box.width() >= 1.0 && box.height() >= 1.0) {
                const QImage &live = displayImage();
                const int pixEdge = qMax(pixmap().width(), pixmap().height());
                const int liveEdge = live.isNull()
                    ? 0
                    : qMax(live.width(), live.height());
                if (!live.isNull() && liveEdge > pixEdge
                    && (!tilesWanted
                        || liveEdge <= DisplayQuality::kLqipMaxEdge)) {
                    setPixmap(QPixmap::fromImage(live));
                    if (cacheMode() == QGraphicsItem::ItemCoordinateCache) {
                        setCacheMode(QGraphicsItem::NoCache);
                        setCacheMode(QGraphicsItem::ItemCoordinateCache);
                    }
                }
                // Gallery tile cells: only LQIP-sized pixmap as underlay.
                if (galleryLqipOnlyUnderTiles
                    && qMax(pixmap().width(), pixmap().height())
                        > DisplayQuality::kLqipMaxEdge) {
                    // leave underlay to LQIP branch / placeholder
                } else {
                painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter->drawPixmap(box, pixmap(), QRectF(pixmap().rect()));
                }
            } else if (!m_source.isNull() && !m_previewPixels) {
                if (galleryLqipOnlyUnderTiles
                    && qMax(m_source.width(), m_source.height())
                        > DisplayQuality::kLqipMaxEdge) {
                    // Gallery: skip non-LQIP host under tiles
                } else if (!pixmap().isNull() && box.width() >= 1.0 && box.height() >= 1.0
                    && (!galleryLqipOnlyUnderTiles
                        || qMax(pixmap().width(), pixmap().height())
                            <= DisplayQuality::kLqipMaxEdge)) {
                    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
                    painter->drawPixmap(box, pixmap(), QRectF(pixmap().rect()));
                } else if (!m_source.isNull()) {
                    drawSampleInContentRect(m_source);
                }
            } else if (!m_preview.isNull()) {
                drawSampleInContentRect(m_preview);
            } else {
                // Sized placeholder (intrinsic from size memo/probe) or neutral
                // provisional box while cold. Never invent LQIP here.
                const QRectF cr = contentRect();
                painter->fillRect(cr, QColor(40, 40, 44));
                const qreal inset = ItemFrameGeometry::placeholderInset(cr.width(), cr.height());
                const QRectF inner = cr.adjusted(inset, inset, -inset, -inset);
                painter->setPen(QPen(QColor(70, 72, 80), 0));
                painter->setBrush(QColor(52, 54, 62));
                painter->drawRoundedRect(inner, inset * 0.8, inset * 0.8);
                painter->setPen(QPen(QColor(140, 145, 160), 0));
                QFont f = painter->font();
                const qreal edge = qMin(inner.width(), inner.height());
                f.setPointSizeF(ItemFrameGeometry::placeholderEllipsisPointSize(edge));
                f.setBold(true);
                painter->setFont(f);
                painter->drawText(inner, Qt::AlignCenter, QStringLiteral("⋯"));
            }
        }

        // Deep zoom: grid tiles over soft/LQIP underlay (TILE_LOD.md).
        // Refresh *plan* here so zoom-out does not keep painting scale-0 cells
        // until the debounced tick runs. Requests stay on tick only (cheap
        // set_viewport; cancel only when plan_changed).
        // Nav-hot: skip plan + tile paint — soft underlay only (IMAGE_MODE_NAV_SOFT).
        bool navHot = false;
        if (scene() && !scene()->views().isEmpty()) {
            if (auto *iv = qobject_cast<ImageView *>(scene()->views().first())) {
                navHot = iv->hostSlideshow().hud().isNavHot();
            }
        }
        if (tileLodWanted() && !navHot) {
            prepareTileLodPlan();
            if (tileLodBag().controller && tileLodBag().controller->path() == m_path && tileLodBag().controller->session()) {
                const QImage under = hasDecodedPixels() ? m_source
                    : (!m_preview.isNull() ? m_preview : QImage());
                tilelod::DrawPlan plan = tileLodBag().controller->session()->draw_plan();
                const QSize native = tileNativeSize();
                const ContentXform::Value x = liveContentXformForPaint();
                const QPointF off = offset();
                const bool freeRot = x.hasCrop && !x.cropRect.isEmpty()
                    && qAbs(x.cropRotation) > 1e-3;
                const bool orient =
                    x.hFlip || x.vFlip
                    || (ContentXform::normalizeQuarterTurns(x.quarterTurns) != 0);

                ColorAdjustments grade = x.colorAdjust;
                if (grade.isIdentity()) {
                    grade = liveColorForPaint();
                }
                auto resolve = [this, grade](tilelod::TileKey const &key,
                                             tilelod::TileBitmap const &) -> QImage {
                    return resolveGradedTile(key, grade);
                };

                // Source-aligned tile bitmaps: extract src_uv, then orient the
                // patch (flip/turn). Dest is AABB in display space. Avoid
                // reflective painter transforms (empty draw on GL after flip).
                auto orientPatch = [&x](QImage patch) -> QImage {
                    if (patch.isNull()) {
                        return patch;
                    }
                    if (x.hFlip || x.vFlip) {
                        Qt::Orientations axes;
                        if (x.hFlip) {
                            axes |= Qt::Horizontal;
                        }
                        if (x.vFlip) {
                            axes |= Qt::Vertical;
                        }
                        if (axes) {
                            patch = patch.flipped(axes);
                        }
                    }
                    const int turns =
                        ContentXform::normalizeQuarterTurns(x.quarterTurns);
                    if (turns != 0) {
                        QTransform rot;
                        rot.rotate(90.0 * turns);
                        patch = patch.transformed(rot, Qt::FastTransformation);
                    }
                    return patch;
                };

                auto extractUv = [](const QImage &img, const tilelod::RectF &uv) -> QImage {
                    if (img.isNull()) {
                        return {};
                    }
                    const QRectF srcUv(uv.x, uv.y, uv.w, uv.h);
                    if (srcUv.width() < 1.0 || srcUv.height() < 1.0) {
                        return img;
                    }
                    // Full-tile UV: skip copy.
                    if (srcUv.x() <= 0.5 && srcUv.y() <= 0.5
                        && srcUv.width() + 0.5 >= img.width()
                        && srcUv.height() + 0.5 >= img.height()) {
                        return img;
                    }
                    const QRect ir = srcUv.toAlignedRect().intersected(img.rect());
                    if (ir.isEmpty()) {
                        return {};
                    }
                    return img.copy(ir);
                };

                struct TilePaintCmd {
                    QRectF dst;
                    QImage patch;
                };
                QVector<TilePaintCmd> paintCmds;
                paintCmds.reserve(plan.commands.size());

                for (const tilelod::DrawCommand &cmd : plan.commands) {
                    if (cmd.kind != tilelod::DrawKind::ExactTile
                        && cmd.kind != tilelod::DrawKind::CoarserTile) {
                        continue;
                    }
                    QRectF srcBox(cmd.dst_content.x, cmd.dst_content.y,
                                  cmd.dst_content.w, cmd.dst_content.h);
                    QImage img = resolve(cmd.src_key, tilelod::TileBitmap{});
                    if (img.isNull()) {
                        continue;
                    }
                    QImage patch = extractUv(img, cmd.src_uv);
                    if (patch.isNull()) {
                        continue;
                    }
                    if (orient) {
                        patch = orientPatch(patch);
                    }
                    if (patch.isNull()) {
                        continue;
                    }

                    if (freeRot) {
                        QRectF ori = ContentXform::mapSourceRectToOriented(
                            srcBox, native, x);
                        if (ori.isEmpty()) {
                            continue;
                        }
                        paintCmds.push_back({ori, patch});
                        continue;
                    }

                    // Axis-aligned: oriented AABB, then crop-local + edge crop.
                    QRectF oriented = ContentXform::mapSourceRectToOriented(
                        srcBox, native, x);
                    if (oriented.isEmpty()) {
                        continue;
                    }
                    QRectF disp = oriented;
                    if (x.hasCrop && !x.cropRect.isEmpty()) {
                        const QRect contentCrop = x.cropRect.normalized();
                        const QRectF local = oriented.translated(
                            -contentCrop.x(), -contentCrop.y());
                        const QRectF cropLocal(0.0, 0.0, contentCrop.width(),
                                               contentCrop.height());
                        disp = local.intersected(cropLocal);
                        if (disp.isEmpty() || local.width() < 1e-6
                            || local.height() < 1e-6) {
                            continue;
                        }
                        // Partial edge: crop the (already oriented) patch with
                        // the same ratio so we do not stretch into a smaller dest.
                        if (qAbs(disp.width() - local.width()) > 0.5
                            || qAbs(disp.height() - local.height()) > 0.5) {
                            const qreal u0 =
                                (disp.left() - local.left()) / local.width();
                            const qreal v0 =
                                (disp.top() - local.top()) / local.height();
                            const qreal uw = disp.width() / local.width();
                            const qreal vh = disp.height() / local.height();
                            const QRect pr = QRectF(
                                u0 * patch.width(), v0 * patch.height(),
                                uw * patch.width(), vh * patch.height())
                                                 .toAlignedRect()
                                                 .intersected(patch.rect());
                            if (pr.isEmpty()) {
                                continue;
                            }
                            patch = patch.copy(pr);
                        }
                    }
                    paintCmds.push_back(
                        {QRectF(disp.x() + off.x(), disp.y() + off.y(),
                                disp.width(), disp.height()),
                         patch});
                }

                const QRectF cr = contentRect();
                painter->save();
                // Intersect with any outer gallery cell clip — ReplaceClip used
                // to drop GridCrop clipping so tiles spilled outside the cell.
                if (freeRot) {
                    // Free-rot dests are in oriented space; paint transform maps
                    // crop centre → contentRect centre. Clip must be set *after*
                    // that transform in crop-local coords. Setting contentRect
                    // first then rotating left the device clip as an unrotated
                    // AABB while tiles spun under it.
                    const QRect contentCrop = x.cropRect.normalized();
                    painter->translate(cr.center());
                    painter->rotate(-x.cropRotation);
                    painter->translate(-QPointF(contentCrop.center()));
                    painter->setClipRect(QRectF(contentCrop), Qt::IntersectClip);
                } else {
                    // Axis-aligned / quarter-turn: dests and contentRect share
                    // oriented display space (+ offset).
                    painter->setClipRect(cr, Qt::IntersectClip);
                }

                const bool smooth = tilePaintNeedsSmooth(
                    tileDevicePerContent(),
                    tileLodBag().controller->session()->target_scale(), plan);
                painter->setRenderHint(QPainter::SmoothPixmapTransform, smooth);

                for (const TilePaintCmd &pc : paintCmds) {
                    if (pc.patch.isNull() || pc.dst.isEmpty()) {
                        continue;
                    }
                    painter->drawImage(pc.dst, pc.patch);
                }
                (void)under;

                if (tilePlanDebugOverlayEnabled()) {
                    // Map plan cells like tile paint (orient/flip/crop). Pixel
                    // TILE stamps still belong in thumtoo so they follow patches.
                    paintTilePlanDebugOverlay(painter, tileLodBag().controller->session(), plan,
                                             contentRect(), off, native, x, freeRot);
                }
                painter->restore();

            }
        }
        painter->restore();
    }

    const QRectF r = cropped ? crop : displayContentRect();

    // Content-edit marks (View → Show content edit marks). Fixed ~20px on screen
    // (not fraction of tile — Gallery was tiny, tight crops were huge).
    if (contentEditMarksVisible() && r.width() > 4.0 && r.height() > 4.0) {
        const qreal fold = ItemFrameGeometry::kContentEditMarkScreenPx / ViewTransform::floorScale(screenScale());

        auto drawCornerFold = [&](const QPointF &corner, const QPointF &alongX,
                                  const QPointF &alongY, const QColor &face,
                                  const QColor &shade) {
            painter->save();
            painter->setOpacity(1.0);
            const QPointF a = corner + alongX;
            const QPointF b = corner + alongY;
            QPolygonF tri;
            tri << a << corner << b;
            painter->setPen(Qt::NoPen);
            painter->setBrush(face);
            painter->drawPolygon(tri);
            const QPointF mid((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
            QPolygonF under;
            under << a << mid << b;
            painter->setBrush(shade);
            painter->drawPolygon(under);
            painter->setPen(QPen(QColor(0, 0, 0, 100), 0));
            painter->setBrush(Qt::NoBrush);
            painter->drawLine(a, b);
            painter->restore();
        };

        // Crop / orient marks: host-preferring applied ContentXform (Stage 2).
        const ContentXform::Value contentMarks = liveContentXformForPaint();
        if (contentMarks.hasCrop) {
            drawCornerFold(QPointF(r.right(), r.bottom()),
                           QPointF(-fold, 0), QPointF(0, -fold),
                           QColor(242, 196, 40), QColor(200, 150, 20));
        }

        // Orient (flip / 90°): cyan fold, bottom-left.
        const bool orient = contentMarks.hFlip || contentMarks.vFlip
            || contentMarks.quarterTurns != 0;
        if (orient) {
            drawCornerFold(QPointF(r.left(), r.bottom()),
                           QPointF(fold, 0), QPointF(0, -fold),
                           QColor(56, 189, 248), QColor(14, 116, 144));
        }

        // Grade: coral fold, slightly inset from bottom-left when orient also set.
        const bool grade = !liveColorForPaint().isIdentity();
        if (grade) {
            const qreal inset = orient ? fold * 0.55 : 0.0;
            drawCornerFold(QPointF(r.left() + inset, r.bottom()),
                           QPointF(fold * 0.85, 0), QPointF(0, -fold * 0.85),
                           QColor(251, 113, 133), QColor(190, 48, 78));
        }
    }

    // Gallery: selection frame is painted by ImageView::drawForeground so
    // ItemCoordinateCache is not invalidated on every selection change / scroll.
    // Content-only paint stays cacheable across view pan under OpenGL.
    if (!m_interactive) {
        return;
    }

    // Selection transform chrome is painted by ImageView::paintEvent (viewport
    // device space) so handles sit above every image and stay scale-invariant.
    Q_UNUSED(r);
}


void ImageItem::paintSelectionFrame(QPainter *painter) const
{
    if (!painter || !scene()) {
        return;
    }
    const QList<QGraphicsView *> views = scene()->views();
    if (views.isEmpty()) {
        return;
    }
    QGraphicsView *view = views.first();
    const QRectF localRect = displayContentRect();
    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    QPolygonF poly;
    poly << tl << tr << br << bl;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(60, 140, 255, 220));
    pen.setWidthF(0);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPolygon(poly);
    painter->restore();
}

void ImageItem::paintInteractionChrome(QPainter *painter) const
{
    if (!m_interactive || !isSelected()) {
        return;
    }
    const QRectF crop = galleryClipLocal();
    const QRectF r = crop.isEmpty() ? contentRect() : crop;
    paintInteractionChrome(painter, r);
}

void ImageItem::setHoverHandle(Handle h)
{
    if (h == m_hoverHandle) {
        return;
    }
    m_hoverHandle = h;
    // Chrome is painted by ImageView::paintEvent; the view refreshes the
    // viewport. Avoid item-only update which would miss external handle pads.
}

void ImageItem::paintInteractionChrome(QPainter *painter, const QRectF &localRect) const
{
    // HARD RULE (AGENTS.md): draw only in viewport logical pixels.
    QGraphicsView *view = nullptr;
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty()) {
            view = views.first();
        }
    }
    if (!view || !painter) {
        return;
    }

    auto toView = [this, view](const QPointF &local) -> QPointF {
        return QPointF(view->mapFromScene(mapToScene(local)));
    };

    // Four corners of the content frame in viewport pixels.
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    const QPointF centerV = toView(localRect.center());

    const ItemFrameGeometry::FrameViewGeom fg =
        ItemFrameGeometry::makeFrameViewGeom(tl, tr, br, bl);
    const QPointF &dirTop = fg.dirTop;
    const QPointF &dirRight = fg.dirRight;
    const QPointF &dirBottom = fg.dirBottom;
    const QPointF &dirLeft = fg.dirLeft;
    const QPointF &midTop = fg.midTop;
    const QPointF &midRight = fg.midRight;
    const QPointF &midBottom = fg.midBottom;
    const QPointF &midLeft = fg.midLeft;
    // Outward normals: rotateHandlePoints / opacity track use fg.out* directly.

    painter->save();
    painter->setOpacity(1.0);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal hs = ItemFrameGeometry::kHandleScreenPx;

    // Degenerate frame (item scale near zero / collapsed): skip detailed chrome
    // so we never feed zero-length edges into norm() or draw Inf positions.
    // Threshold is deliberately higher than a few pixels so handles stay visible
    // while the image is still clearly on screen.
    const qreal frameDiag = QLineF(tl, br).length();
    const bool frameOk = frameDiag >= ItemFrameGeometry::kMinFrameDiagPx;

    // Selection frame
    QPen framePen(QColor(0, 160, 255), 0);
    framePen.setCosmetic(true);
    framePen.setWidthF(1.5);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    if (frameOk) {
        painter->drawPolygon(QPolygonF({tl, tr, br, bl}));
    } else {
        // Minimal centred marker so the selection is still visible.
        painter->drawEllipse(centerV, 6.0, 6.0);
        painter->restore();
        return;
    }

    // Rotate stems + knobs: constant viewport-pixel offset along outward normal
    // of each edge (HANDLES.md). Never derive from local / sx.
    QPointF rotPts[4];
    ItemFrameGeometry::rotateHandlePoints(fg, rotPts);
    auto drawRotate = [&](Handle h, const QPointF &edgeMid, const QPointF &c) {
        const bool hot = (m_hoverHandle == h);
        QPen stem(QColor(0, 160, 255), 0);
        stem.setCosmetic(true);
        stem.setWidthF(1.25);
        painter->setPen(stem);
        painter->drawLine(edgeMid, c);
        const qreal rad = hs * (hot ? 0.52 : 0.34);
        painter->setBrush(hot ? QColor(255, 230, 80) : QColor(255, 200, 40));
        QPen hp(hot ? QColor(255, 255, 255) : QColor(40, 40, 40), 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.75 : 1.15);
        painter->setPen(hp);
        painter->drawEllipse(c, rad, rad);
    };
    drawRotate(Handle::RotateTop, midTop, rotPts[0]);
    drawRotate(Handle::RotateRight, midRight, rotPts[1]);
    drawRotate(Handle::RotateBottom, midBottom, rotPts[2]);
    drawRotate(Handle::RotateLeft, midLeft, rotPts[3]);

    if (m_scaleHandlesEnabled) {
        // Corner L-brackets: arms along the two adjacent edges in *view* space.
        struct Corner {
            Handle h;
            QPointF corner;
            QPointF alongA;
            QPointF alongB;
        };
        const Corner corners[] = {
            {Handle::ScaleTopLeft, tl, dirTop, -dirLeft},
            {Handle::ScaleTopRight, tr, -dirTop, dirRight},
            {Handle::ScaleBottomRight, br, dirBottom, -dirRight},
            {Handle::ScaleBottomLeft, bl, -dirBottom, dirLeft},
        };
        for (const Corner &co : corners) {
            // Single continuous L: two arms through the corner with RoundJoin so
            // the elbow is a smooth fillet. Stroke weight matches edge scale bars.
            const bool hot = (m_hoverHandle == co.h);
            const QPointF c = co.corner;
            const QPointF d1 = ItemFrameGeometry::unitOr(co.alongA);
            const QPointF d2 = ItemFrameGeometry::unitOr(co.alongB);
            const qreal arm = hs * (hot ? 1.7 : 1.35);
            // Match edge-bar thickness (hs * ~0.30–0.48), not a hairline.
            const qreal thick = hs * (hot ? 0.48 : 0.36);
            const QColor strokeCol = hot ? QColor(255, 255, 255) : QColor(0, 160, 255);
            QPainterPath path;
            path.moveTo(c + d1 * arm);
            path.lineTo(c);
            path.lineTo(c + d2 * arm);
            QPen hp(strokeCol, 0);
            hp.setCosmetic(true);
            hp.setWidthF(thick);
            hp.setCapStyle(Qt::RoundCap);
            hp.setJoinStyle(Qt::RoundJoin);
            painter->setPen(hp);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
            // Soft fill under the stroke so hot state reads solid like edge bars.
            if (hot) {
                QPen glow(QColor(0, 160, 255, 180), 0);
                glow.setCosmetic(true);
                glow.setWidthF(thick * 0.55);
                glow.setCapStyle(Qt::RoundCap);
                glow.setJoinStyle(Qt::RoundJoin);
                painter->setPen(glow);
                painter->drawPath(path);
            }
        }

        // Edge stretch bars along each edge, mid-edge, constant view size.
        struct EdgeBar {
            Handle h;
            QPointF mid;
            QPointF along;
        };
        const EdgeBar edges[] = {
            {Handle::ScaleTop, midTop, dirTop},
            {Handle::ScaleRight, midRight, dirRight},
            {Handle::ScaleBottom, midBottom, dirBottom},
            {Handle::ScaleLeft, midLeft, dirLeft},
        };
        for (const EdgeBar &ed : edges) {
            const bool hot = (m_hoverHandle == ed.h);
            const QPointF along = ItemFrameGeometry::unitOr(ed.along);
            const QPointF perp(-along.y(), along.x());
            const qreal len = hs * (hot ? 2.4 : 1.7);
            const qreal thick = hs * (hot ? 0.48 : 0.30);
            const QColor base = hot ? QColor(0, 160, 255, 255) : QColor(0, 160, 255, 230);
            const QColor edgeCol = hot ? QColor(255, 255, 255) : QColor(0, 120, 200);
            QPen hp(edgeCol, 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 1.6 : 1.15);
            painter->setPen(hp);
            painter->setBrush(base);
            QPolygonF bar;
            bar << ed.mid + along * (len / 2) + perp * (thick / 2)
                << ed.mid - along * (len / 2) + perp * (thick / 2)
                << ed.mid - along * (len / 2) - perp * (thick / 2)
                << ed.mid + along * (len / 2) - perp * (thick / 2);
            painter->drawPolygon(bar);
            painter->setBrush(Qt::NoBrush);
        }

        // Shear grips: small diamonds on all four edges (offset from mid).
        QPointF shearPts[4];
        ItemFrameGeometry::shearHandlePoints(fg, shearPts, hs * 2.2);
        struct ShearD {
            Handle h;
            QPointF c;
        };
        const ShearD shears[] = {
            {Handle::ShearTop, shearPts[0]},
            {Handle::ShearBottom, shearPts[1]},
            {Handle::ShearLeft, shearPts[2]},
            {Handle::ShearRight, shearPts[3]},
        };
        for (const ShearD &sh : shears) {
            const bool hot = (m_hoverHandle == sh.h);
            const qreal rad = hs * (hot ? 0.55 : 0.40);
            QPolygonF dia;
            dia << sh.c + QPointF(0, -rad)
                << sh.c + QPointF(rad, 0)
                << sh.c + QPointF(0, rad)
                << sh.c + QPointF(-rad, 0);
            painter->setBrush(hot ? QColor(255, 200, 60) : QColor(180, 120, 255));
            QPen hp(hot ? QColor(255, 255, 255) : QColor(60, 40, 100), 0);
            hp.setCosmetic(true);
            hp.setWidthF(hot ? 1.6 : 1.1);
            painter->setPen(hp);
            painter->drawPolygon(dia);
            painter->setBrush(Qt::NoBrush);
        }
    }

    // Chrome buttons: top-right *outside*. Adaptive — stack shifts upward when
    // split upper/lower groups around the right rotate knob (see chromeCentersView).
    {
        // fg already built for this paint call
        QPointF centers[ItemFrameGeometry::kChromeCount];
        ItemFrameGeometry::chromeCentersView(fg, centers);

        // Design language (HANDLES.md):
        //   circle  = momentary action (click once)
        //   rounded square = latching toggle
        //   open strokes on the frame = geometry grips (elsewhere)
        const qreal btnR = ItemFrameGeometry::kChromeBtnScreenPx / 2.0;
        const qreal toggleHalf = ItemFrameGeometry::kChromeBtnScreenPx * 0.52; // half-side of toggle square

        auto drawFlipToggle = [&](Handle h, int index, bool on, const QString &glyph) {
            const QPointF c = centers[index];
            const bool hovered = (m_hoverHandle == h);
            const qreal half = toggleHalf * (hovered ? 1.08 : 1.0);
            const qreal corner = half * 0.28; // squircle-ish
            const QRectF box(c.x() - half, c.y() - half, half * 2.0, half * 2.0);
            QColor fill = on ? QColor(0, 160, 255, 245)
                             : QColor(40, 40, 40, 220);
            if (hovered && !on) {
                fill = QColor(0, 100, 180, 200);
            }
            QPen border(on || hovered ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(on ? 2.0 : (hovered ? 1.75 : 1.25));
            border.setCosmetic(true);
            painter->setPen(border);
            painter->setBrush(fill);
            painter->drawRoundedRect(box, corner, corner);
            if (on) {
                // Inner inset mark so "latched" reads differently from hover.
                QPen ring(QColor(255, 255, 255, 210));
                ring.setWidthF(1.2);
                ring.setCosmetic(true);
                painter->setPen(ring);
                painter->setBrush(Qt::NoBrush);
                const qreal inset = half * 0.28;
                painter->drawRoundedRect(box.adjusted(inset, inset, -inset, -inset),
                                         corner * 0.7, corner * 0.7);
            }
            painter->setPen(on ? QColor(255, 255, 255) : QColor(230, 230, 230));
            QFont f = painter->font();
            f.setPointSizeF(ItemFrameGeometry::contentEditGlyphPointSize(half));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(box, Qt::AlignCenter, glyph);
        };

        auto drawBtn = [&](Handle h, int index, const QString &glyph) {
            const QPointF c = centers[index];
            const bool hovered = (m_hoverHandle == h);
            const qreal rad = btnR * (hovered ? 1.08 : 1.0);
            QColor fill = hovered ? QColor(0, 140, 255, 240) : QColor(50, 50, 50, 230);
            QPen border(hovered ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(hovered ? 1.75 : 1.0);
            border.setCosmetic(true);
            painter->setPen(border);
            painter->setBrush(fill);
            painter->drawEllipse(c, rad, rad);
            painter->setPen(QColor(240, 240, 240));
            QFont f = painter->font();
            f.setPointSizeF(ItemFrameGeometry::chromeGlyphPointSize(rad));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(QRectF(c.x() - rad, c.y() - rad, rad * 2, rad * 2),
                              Qt::AlignCenter, glyph);
        };

        // Source flags via host-preferring applied ContentXform (Stage 2).
        // Chrome labels are display axes: after odd turns H↔V, so light the
        // conjugated pair or FlipH looks like FlipV after a 90° rotate.
        const ContentXform::Value chromeX = liveContentXformForPaint();
        int turns = chromeX.quarterTurns % 4;
        if (turns < 0) {
            turns += 4;
        }
        const bool swapAxes = (turns == 1 || turns == 3);
        const bool displayHFlip = swapAxes ? chromeX.vFlip : chromeX.hFlip;
        const bool displayVFlip = swapAxes ? chromeX.hFlip : chromeX.vFlip;
        drawFlipToggle(Handle::FlipH, 0, displayHFlip, QStringLiteral("↔"));
        drawFlipToggle(Handle::FlipV, 1, displayVFlip, QStringLiteral("↕"));
        drawBtn(Handle::Rotate90CCW, 2, QStringLiteral("↺"));
        drawBtn(Handle::Rotate90CW, 3, QStringLiteral("↻"));
        drawBtn(Handle::Raise, 4, QStringLiteral("↑"));
        drawBtn(Handle::Lower, 5, QStringLiteral("↓"));
        drawBtn(Handle::ResetScale, 6, QStringLiteral("1:1"));
        drawBtn(Handle::ResetRotation, 7, QStringLiteral("0°"));
        drawBtn(Handle::ResetShear, 8, QStringLiteral("//"));
    }

    // Opacity: left *outside*, vertical. Bottom end = 5%, top end = 100%.
    // Clears the left scale bar and rotate knob (see opacityTrackView).
    {
        // fg already built for this paint call
        QPointF a, b;
        ItemFrameGeometry::opacityTrackView(fg, &a, &b);
        const QPointF ab = b - a;
        const qreal abLen = qHypot(ab.x(), ab.y());
        const QPointF along = abLen > 1e-6 ? ab / abLen : QPointF(0, -1);
        Q_UNUSED(along);
        const QPointF perp = fg.outLeft;
        const qreal thick = ItemFrameGeometry::kSliderHeightPx;
        const bool hot = (m_hoverHandle == Handle::OpacitySlider);
        const qreal tval = ItemFrameGeometry::trackParamFromOpacity(m_opacity);
        QPen trackPen(hot ? QColor(200, 180, 255) : QColor(120, 100, 160), 0);
        trackPen.setCosmetic(true);
        trackPen.setWidthF(1.15);
        painter->setPen(trackPen);
        painter->setBrush(QColor(40, 40, 40, hot ? 230 : 200));
        QPolygonF trackPoly;
        trackPoly << a + perp * (thick / 2) << b + perp * (thick / 2)
                  << b - perp * (thick / 2) << a - perp * (thick / 2);
        painter->drawPolygon(trackPoly);
        const QPointF mid = ViewTransform::pointAlong(a, b, tval);
        painter->setBrush(QColor(140, 100, 200, 230));
        QPolygonF filled;
        filled << a + perp * (thick / 2) << mid + perp * (thick / 2)
               << mid - perp * (thick / 2) << a - perp * (thick / 2);
        painter->drawPolygon(filled);
        painter->setBrush(QColor(230, 230, 230));
        painter->setPen(QPen(QColor(30, 30, 30), 0));
        painter->drawEllipse(mid, 5.5, 5.5);
    }

    painter->restore();
}


bool ImageItem::beginHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods,
                                         HandlePressScratch *outPress)
{
    Q_UNUSED(mods);
    if (!m_interactive || !isSelected()) {
        return false;
    }
    const Handle h = handleAt(mapFromScene(scenePos));
    if (h == Handle::None) {
        return false;
    }
    if (h == Handle::FlipH || h == Handle::FlipV
        || h == Handle::Rotate90CCW || h == Handle::Rotate90CW
        || h == Handle::Raise || h == Handle::Lower
        || h == Handle::ResetScale || h == Handle::ResetRotation
        || h == Handle::ResetShear) {
        activateChromeHandle(h);
        return true;
    }
    // Paint hot sticks for the drag: hover is frozen while isHandleDragging().
    setHoverHandle(h);
    HandlePressScratch press;
    press.scenePos = scenePos;
    press.placement = placement();
    press.itemPos = mapFromScene(scenePos);
    press.anchorLocal = scaleAnchorLocal(h);
    press.anchorScene = mapToScene(press.anchorLocal);
    press.handle = h;
    if (outPress) {
        *outPress = press;
    }
    if (h == Handle::OpacitySlider) {
        setOpacityFromSliderPos(scenePos);
        notifyViewStatus();
    }
    return true;
}

void ImageItem::updateHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods,
                                          HandlePressScratch &press)
{
    const Handle h = press.handle;
    if (h == Handle::None) {
        return;
    }
    if (h == Handle::OpacitySlider) {
        setOpacityFromSliderPos(scenePos);
    } else if (ItemHandlePolicy::isRotateHandle(h)) {
        const QPointF itemCentre = this->scenePos();
        // Ctrl → 45° (includes 90°); Shift (alone or with Ctrl) → 15°.
        const qreal a0 = PlacementLinear::angleAbout(itemCentre, press.scenePos);
        const qreal a1 = PlacementLinear::angleAbout(itemCentre, scenePos);
        ItemComponents::Placement pl = press.placement;
        pl.rotation = PlacementLinear::freeRotationFromDrag(
            press.placement.rotation, a0, a1, mods & Qt::ShiftModifier, mods & Qt::ControlModifier);
        applyPlacement(pl);
    } else if (ItemHandlePolicy::isScaleHandle(h)) {
        applyScaleHandleDrag(scenePos, mods, press);
    } else if (ItemHandlePolicy::isShearHandle(h)) {
        applyShearHandleDrag(scenePos, press);
    }
    notifyViewStatus();
}

void ImageItem::endHandleInteraction(Handle continuous)
{
    // Free-rotate / scale: persist when the gesture finishes.
    // continuous comes from HandlePressScratch (interaction authority).
    if (continuous != Handle::None && continuous != Handle::OpacitySlider) {
        if (scene()) {
            for (QGraphicsView *v : scene()->views()) {
                if (auto *iv = qobject_cast<ImageView *>(v)) {
                    iv->commitItemSessionEdit(this);
                    break;
                }
            }
        }
    }
}

void ImageItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // AUDIT H2: transform chrome is owned exclusively by ImageView (viewport
    // hit-testing + paintEvent). Item only handles body selection / move.
    QGraphicsPixmapItem::mousePressEvent(event);
}

void ImageItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsPixmapItem::mouseMoveEvent(event);
}

void ImageItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsPixmapItem::mouseReleaseEvent(event);
}

void ImageItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    // Gallery / Workspace hover chrome and cursors are driven by ImageView.
    // Keep a neutral item cursor so it does not fight the viewport cursor.
    unsetCursor();
    QGraphicsPixmapItem::hoverMoveEvent(event);
}

void ImageItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    if (m_hoverHandle != Handle::None) {
        m_hoverHandle = Handle::None;
        setToolTip(QString());
    }
    unsetCursor();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}
