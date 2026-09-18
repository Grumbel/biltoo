// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageitem.h"
#include "displayquality.h"
#include "biltoo_thread.h"

#include <cstdlib>
#include <cmath>
#include "tilelod/tile_lod_controller.hpp"
#include "thumtoocache.h"
#include "imagecache.h"
#include "contentxform.h"
#include "coloradjust.h"

#include <QCoreApplication>
#include "placementlinear.h"
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
                               const QRectF &contentBounds)
{
    if (!painter || !session) {
        return;
    }
    painter->save();
    // Washes under labels; keep image readable.
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QTransform dt = painter->deviceTransform();
    const qreal sx = qMax(1e-6, qSqrt(dt.m11() * dt.m11() + dt.m12() * dt.m12()));
    // Aim for ~13–16 device px font regardless of zoom.
    const int fontPx = qBound(10, qRound(14.0 / sx), 36);
    QFont of = painter->font();
    of.setBold(true);
    of.setPixelSize(fontPx);
    painter->setFont(of);
    const QFontMetrics fm(of);

    // Alpha high enough to read at a glance, low enough to see the photo.
    constexpr int kWashAlpha = 90;
    const QColor fillExact(255, 220, 40, kWashAlpha);   // yellow — exact cache
    const QColor fillParent(255, 140, 20, kWashAlpha);  // orange — parent cache
    const QColor fillHole(40, 200, 255, kWashAlpha);    // cyan — soft/ladder hole
    const QColor edgeExact(255, 230, 60);
    const QColor edgeParent(255, 160, 40);
    const QColor edgeHole(60, 220, 255);

    int nExact = 0, nParent = 0;
    for (const tilelod::DrawCommand &cmd : plan.commands) {
        if (cmd.kind == tilelod::DrawKind::ExactTile) {
            ++nExact;
        } else if (cmd.kind == tilelod::DrawKind::CoarserTile) {
            ++nParent;
        }
    }
    const tilelod::TileSession::Coverage cov = session->coverage();
    // Holes are omitted from the draw plan when soft is the continuous base;
    // derive count from coverage vs painted exact/parent commands.
    const int nHole = qMax(0, cov.visible - nExact - nParent);
    const int target = session->target_scale();
    const int desired = session->desired_scale();

    // Cell washes first (under the summary plate).
    const qreal minLabelLocal = 40.0 / sx;
    for (const tilelod::DrawCommand &cmd : plan.commands) {
        const QRectF dst(cmd.dst_content.x, cmd.dst_content.y,
                         cmd.dst_content.w, cmd.dst_content.h);
        if (dst.isEmpty()) {
            continue;
        }
        QColor fill;
        QColor edge;
        QString tag;
        if (cmd.kind == tilelod::DrawKind::ExactTile) {
            fill = fillExact;
            edge = edgeExact;
            tag = QStringLiteral("E %1,%2").arg(cmd.src_key.x).arg(cmd.src_key.y);
        } else if (cmd.kind == tilelod::DrawKind::CoarserTile) {
            fill = fillParent;
            edge = edgeParent;
            tag = QStringLiteral("P s%1").arg(cmd.src_key.scale);
        } else if (cmd.kind == tilelod::DrawKind::Underlay) {
            fill = fillHole;
            edge = edgeHole;
            tag = QStringLiteral("H");
        } else {
            continue;
        }
        painter->fillRect(dst, fill);
        painter->setPen(QPen(edge, 0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(dst);
        if (qMin(dst.width(), dst.height()) >= minLabelLocal && !tag.isEmpty()) {
            painter->fillRect(QRectF(dst.left(), dst.top(),
                                     fm.horizontalAdvance(tag) + 4, fm.height() + 2),
                              QColor(0, 0, 0, 170));
            painter->setPen(edge);
            painter->drawText(dst.adjusted(2, 1, -1, -1),
                              Qt::AlignTop | Qt::AlignLeft, tag);
        }
    }

    QStringList summary;
    summary << QStringLiteral("TILE s=%1").arg(target);
    if (desired != target) {
        summary.back() += QStringLiteral(" (want %1)").arg(desired);
    }
    summary << QStringLiteral("vis=%1 exact=%2 parent=%3 hole=%4")
                   .arg(cov.visible)
                   .arg(nExact)
                   .arg(nParent)
                   .arg(nHole);
    summary << QStringLiteral("ok=%1 flight=%2")
                   .arg(cov.exact_succeeded)
                   .arg(cov.in_flight);
    summary << QStringLiteral("Y=exact  O=parent  C=hole");
    if (cov.fully_covered()) {
        summary << QStringLiteral("COMPLETE");
    } else if (cov.in_flight > 0) {
        summary << QStringLiteral("LOADING…");
    } else if (nHole > 0 || cov.exact_succeeded < cov.visible) {
        summary << QStringLiteral("WAITING");
    }

    int blockW = 0;
    for (const QString &line : summary) {
        blockW = qMax(blockW, fm.horizontalAdvance(line));
    }
    const int pad = qMax(2, fontPx / 4);
    const int lineH = fm.height();
    const int blockH = lineH * summary.size() + pad * 2;
    blockW += pad * 2;
    const QRectF plate(contentBounds.left() + 4.0,
                       contentBounds.top() + 4.0,
                       blockW + 2.0,
                       blockH + 2.0);
    painter->fillRect(plate, QColor(0, 0, 0, 200));
    // Colour-coded first lines for the wash legend.
    for (int i = 0; i < summary.size(); ++i) {
        QColor pen(255, 255, 255);
        if (summary.at(i).startsWith(QStringLiteral("Y="))) {
            pen = edgeExact;
        } else if (cov.fully_covered() && summary.at(i) == QStringLiteral("COMPLETE")) {
            pen = QColor(120, 255, 120);
        } else if (summary.at(i).startsWith(QStringLiteral("LOADING"))) {
            pen = QColor(255, 220, 120);
        }
        painter->setPen(pen);
        painter->drawText(QPointF(plate.left() + pad,
                                  plate.top() + pad + (i + 1) * lineH - fm.descent()),
                          summary.at(i));
    }
    painter->restore();
}

constexpr qreal kHandleScreenPx = 16.0;      // scale/rotate markers in *viewport* px (grow on hover)
constexpr qreal kContentEditMarkScreenPx = 20.0; // crop/orient/grade folds (viewport px)
constexpr qreal kRotateOffsetPx = 36.0;      // rotate handle distance from edge (viewport px)
// Chrome buttons (flip / raise / lower / reset): larger + roomier.
constexpr qreal kChromeBtnScreenPx = 34.0;   // diameter in viewport px
constexpr qreal kChromeHitScreenPx = 28.0;   // hit radius in viewport px
// Outside offset from the visual right edge to the button column centre.
constexpr qreal kChromeOutsidePx = 18.0;     // a little more air from the frame
constexpr qreal kChromeBtnGapPx = 14.0;      // gap within a chrome button group
constexpr qreal kChromeClearPx = 16.0;       // min air from group edge to rotate knob
constexpr qreal kChromeGroupGapPx = 22.0;    // extra gap between upper/lower groups (rotate lives here)
constexpr int kChromeUpperCount = 4;         // flip / flip / 90°CCW / 90°CW
constexpr int kChromeLowerCount = 5;         // raise / lower / 1:1 / 0° / shear
constexpr int kChromeCount = kChromeUpperCount + kChromeLowerCount;
// Opacity track length (along the left edge) and thickness (perpendicular).
constexpr qreal kSliderWidthPx = 100.0;   // track length in viewport px
constexpr qreal kSliderHeightPx = 10.0;   // track thickness in viewport px
// Outside offset from the visual left edge to the opacity track centre-line.
constexpr qreal kSliderOutsidePx = 18.0;
// Min air between opacity track and left scale/rotate clearance along the edge.
constexpr qreal kSliderClearPx = 16.0;
// Skip detailed chrome only when the frame is truly a few pixels across.
constexpr qreal kMinFrameDiagPx = 16.0;

struct FrameViewGeom {
    QPointF tl, tr, br, bl, center;
    QPointF midTop, midRight, midBottom, midLeft;
    QPointF dirTop, dirRight, dirBottom, dirLeft;
    QPointF outTop, outRight, outBottom, outLeft;
};

QPointF unitOr(const QPointF &v, const QPointF &fallback = QPointF(1, 0))
{
    const qreal len = qHypot(v.x(), v.y());
    return len > 1e-6 ? v / len : fallback;
}

FrameViewGeom makeFrameViewGeom(const QPointF &tl, const QPointF &tr,
                                const QPointF &br, const QPointF &bl)
{
    FrameViewGeom g;
    g.tl = tl;
    g.tr = tr;
    g.br = br;
    g.bl = bl;
    g.center = (tl + tr + br + bl) * 0.25;
    g.dirTop = unitOr(tr - tl);
    g.dirRight = unitOr(br - tr);
    g.dirBottom = unitOr(bl - br);
    g.dirLeft = unitOr(tl - bl);
    g.midTop = (tl + tr) * 0.5;
    g.midRight = (tr + br) * 0.5;
    g.midBottom = (br + bl) * 0.5;
    g.midLeft = (bl + tl) * 0.5;
    auto outward = [&](const QPointF &mid, const QPointF &along) {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - g.center) < 0) {
            n = -n;
        }
        return n;
    };
    g.outTop = outward(g.midTop, g.dirTop);
    g.outRight = outward(g.midRight, g.dirRight);
    g.outBottom = outward(g.midBottom, g.dirBottom);
    g.outLeft = outward(g.midLeft, g.dirLeft);
    return g;
}

// Top-right outside column. Stack runs along the right edge direction starting
// near the top-right corner. If the stack would collide with the right rotate
// knob, the whole column is shifted further "up" (toward / past the top edge).
// Two chrome groups outside the *rotated* right edge, split around the free-
// rotate knob (along-edge layout — tracks the content frame).
//   upper: FlipH, FlipV, Rotate90CCW, Rotate90CW  — prefer top of edge
//   lower: Raise, Lower, ResetScale, ResetRotation, ResetShear — prefer bottom of edge
void chromeCentersView(const FrameViewGeom &g, QPointF outCenters[kChromeCount])
{
    const qreal btn = kChromeBtnScreenPx;
    const qreal step = btn + kChromeBtnGapPx;
    const qreal colOffset = kChromeOutsidePx + btn * 0.5;
    const QPointF colBase = g.tr + g.outRight * colOffset;
    const QPointF along = g.dirRight; // top → bottom along the right edge

    const QPointF rotR = g.midRight + g.outRight * kRotateOffsetPx;
    const qreal rotClear = kHandleScreenPx * 0.5 + kChromeClearPx + btn * 0.5;
    auto distAlong = [&](const QPointF &p) {
        return QPointF::dotProduct(p - colBase, along);
    };
    const qreal rotAlong = distAlong(rotR);
    const qreal lateral = qAbs(colOffset - kRotateOffsetPx);
    const qreal needAlongClear = qMax(0.0, rotClear - lateral);

    // Reserved band around the free-rotate knob.
    const qreal upperLastAlong = rotAlong - needAlongClear - kChromeGroupGapPx * 0.5;
    const qreal lowerFirstAlong = rotAlong + needAlongClear + kChromeGroupGapPx * 0.5;

    // Upper group: prefer flush with the top-right corner when it fits above
    // the reserved band; otherwise pack against the band from above.
    const qreal preferTop = btn * 0.5 + 4.0;
    qreal firstUpper = preferTop;
    if (preferTop + (kChromeUpperCount - 1) * step > upperLastAlong) {
        firstUpper = upperLastAlong - (kChromeUpperCount - 1) * step;
    }

    // Lower group: prefer flush with the bottom-right corner when it fits below
    // the reserved band; otherwise pack against the band from below.
    const qreal edgeLen = distAlong(g.br + g.outRight * colOffset);
    const qreal preferBottomFirst = edgeLen - ((kChromeLowerCount - 1) * step + btn * 0.5 + 4.0);
    qreal firstLower = preferBottomFirst;
    if (preferBottomFirst < lowerFirstAlong) {
        firstLower = lowerFirstAlong;
    }

    for (int i = 0; i < kChromeUpperCount; ++i) {
        outCenters[i] = colBase + along * (firstUpper + i * step);
    }
    for (int i = 0; i < kChromeLowerCount; ++i) {
        outCenters[kChromeUpperCount + i] = colBase + along * (firstLower + i * step);
    }
}

// Bottom-right outside opacity track. Returns endpoints a→b along the bottom
// edge direction. Left end is kept clear of the bottom scale bar and bottom
// rotate knob; when the preferred right-aligned position would collide, the
// whole track shifts toward the bottom-right corner / further right.
void opacityTrackView(const FrameViewGeom &g, QPointF *aOut, QPointF *bOut)
{
    // Vertical track outside the *left* edge — same adaptive idea as
    // chromeCentersView lower group: constant size, prefer bottom, pack
    // against the mid-edge rotate clearance when the free span is tight.
    //
    // a = bottom end (opacity 5%), b = top end (opacity 100%).
    // Track length is always kSliderWidthPx (never shrinks).
    //
    // Free span = [cornerMargin, maxTop] where maxTop is just below the left
    // free-rotate knob. If the full track fits there, bottom-anchor it.
    // Otherwise pin the top to maxTop and keep full length (bottom may extend
    // past the image bottom), matching how chrome buttons spill when cramped.
    const QPointF alongUp = g.dirLeft; // bl → tl
    const qreal outDist = kSliderOutsidePx + kSliderHeightPx * 0.5;
    const qreal trackLen = kSliderWidthPx;
    const qreal cornerMargin = kHandleScreenPx * 0.6;

    auto projFromBl = [&](const QPointF &p) {
        return QPointF::dotProduct(p - g.bl, alongUp);
    };

    const QPointF rotL = g.midLeft + g.outLeft * kRotateOffsetPx;
    const qreal rotAlong = projFromBl(rotL);
    const qreal needClear = kHandleScreenPx * 0.5 + kSliderClearPx;
    const qreal maxTop = rotAlong - needClear;

    // Prefer bottom-anchored (clear of corner scale handle).
    qreal aAlong = cornerMargin;
    qreal bAlong = aAlong + trackLen;
    if (bAlong > maxTop) {
        // Not enough free space under the rotate knob: pin top to maxTop,
        // keep full track length (extends below the frame if needed).
        bAlong = maxTop;
        aAlong = bAlong - trackLen;
    }

    const QPointF origin = g.bl + g.outLeft * outDist;
    *aOut = origin + alongUp * aAlong;
    *bOut = origin + alongUp * bAlong;
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
        const qreal pad = qMin(96.0, qMax(32.0, content * 0.12));
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

static void singularValues2x2(qreal a, qreal b, qreal c, qreal d, qreal *sMax, qreal *sMin)
{
    // Singular values of [[a,b],[c,d]] = sqrt(eigenvalues of M^T M).
    const qreal e11 = a * a + c * c;
    const qreal e22 = b * b + d * d;
    const qreal e12 = a * b + c * d;
    const qreal tr = e11 + e22;
    const qreal disc = qMax(0.0, (e11 - e22) * (e11 - e22) + 4.0 * e12 * e12);
    const qreal root = qSqrt(disc);
    const qreal ev1 = qMax(0.0, 0.5 * (tr + root));
    const qreal ev2 = qMax(0.0, 0.5 * (tr - root));
    *sMax = qMax(qSqrt(ev1), qSqrt(ev2));
    *sMin = qMax(1e-6, qMin(qSqrt(ev1), qSqrt(ev2)));
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
    singularValues2x2(t.m11(), t.m12(), t.m21(), t.m22(), &sMax, &sMin);
    return qMax(0.01, sMax);
}

qreal ImageItem::deviceScaleMin() const
{
    // Min stretch of local→view: local radius covering a screen-pixel disc
    // under rotation / anisotropic scale (critical for shape() and delivery).
    QTransform t = transform();
    if (scene()) {
        const QList<QGraphicsView *> views = scene()->views();
        if (!views.isEmpty() && views.first()) {
            t = views.first()->transform() * t;
        }
    }
    qreal sMax = 1.0, sMin = 1.0;
    singularValues2x2(t.m11(), t.m12(), t.m21(), t.m22(), &sMax, &sMin);
    return sMin;
}

qreal ImageItem::handleDrawSize() const
{
    // Constant size in screen pixels regardless of item or view zoom
    return kHandleScreenPx / screenScale();
}

qreal ImageItem::handleHitRadius() const
{
    return handleDrawSize() * 1.2;
}

void ImageItem::updateHandleLayout()
{
    prepareGeometryChange();
    update();
}

bool ImageItem::isChromeHandle(Handle h) const
{
    return h == Handle::FlipH || h == Handle::FlipV
        || h == Handle::Rotate90CCW || h == Handle::Rotate90CW
        || h == Handle::Raise || h == Handle::Lower
        || h == Handle::ResetScale || h == Handle::ResetRotation
        || h == Handle::ResetShear
        || h == Handle::OpacitySlider;
}

bool ImageItem::isRotateHandle(Handle h) const
{
    return h == Handle::RotateTop || h == Handle::RotateRight
        || h == Handle::RotateBottom || h == Handle::RotateLeft;
}

bool ImageItem::isCornerScaleHandle(Handle h) const
{
    return h == Handle::ScaleTopLeft || h == Handle::ScaleTopRight
        || h == Handle::ScaleBottomLeft || h == Handle::ScaleBottomRight;
}

bool ImageItem::isEdgeScaleHandle(Handle h) const
{
    return h == Handle::ScaleTop || h == Handle::ScaleRight
        || h == Handle::ScaleBottom || h == Handle::ScaleLeft;
}

bool ImageItem::isScaleHandle(Handle h) const
{
    return isCornerScaleHandle(h) || isEdgeScaleHandle(h);
}

bool ImageItem::isShearHandle(Handle h) const
{
    return h == Handle::ShearTop || h == Handle::ShearBottom
        || h == Handle::ShearLeft || h == Handle::ShearRight;
}

QPointF ImageItem::scaleAnchorLocal(Handle h) const
{
    // Opposite corner/edge — kept fixed when not scaling from the centre.
    const QRectF r = contentRect();
    switch (h) {
    case Handle::ScaleTopLeft:
        return r.bottomRight();
    case Handle::ScaleTopRight:
        return r.bottomLeft();
    case Handle::ScaleBottomLeft:
        return r.topRight();
    case Handle::ScaleBottomRight:
        return r.topLeft();
    case Handle::ScaleTop:
        return QPointF(r.center().x(), r.bottom());
    case Handle::ScaleBottom:
        return QPointF(r.center().x(), r.top());
    case Handle::ScaleLeft:
        return QPointF(r.right(), r.center().y());
    case Handle::ScaleRight:
        return QPointF(r.left(), r.center().y());
    case Handle::ShearTop:
        return QPointF(r.center().x(), r.bottom());
    case Handle::ShearBottom:
        return QPointF(r.center().x(), r.top());
    case Handle::ShearLeft:
        return QPointF(r.right(), r.center().y());
    case Handle::ShearRight:
        return QPointF(r.left(), r.center().y());
    default:
        return r.center();
    }
}

void ImageItem::drawCornerBracket(QPainter *painter, const QPointF &c,
                                   qreal dx, qreal dy, qreal armPx, bool hot) const
{
    // @p dx/@p dy are unit directions in *device* space along the visual edges.
    QPen pen(hot ? QColor(255, 255, 255) : QColor(0, 160, 255), 0);
    pen.setCosmetic(true);
    pen.setWidthF(hot ? 2.5 : 2.0);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(c + QPointF(dx * armPx, dy * 0.0), c);
    painter->drawLine(c, c + QPointF(dx * 0.0, dy * armPx));
}

void ImageItem::applyScaleHandleDrag(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    // Corner and edge (H/V) handles share modifier semantics (drawing-program usual):
    // default scales about the opposite corner/edge; Ctrl or Shift about centre.
    //
    // Edge stretch uses scene-space projections onto the *press-time* image axes
    // so the ratio stays stable when scale approaches the clamp (HANDLES.md).
    // Local ratios after each setItemScale amplify noise near zero.
    const bool modifier = mods & (Qt::ControlModifier | Qt::ShiftModifier);
    const bool fromCenter = modifier;
    const QPointF itemCentre = this->scenePos();
    constexpr qreal kMinDist = 1.0; // scene px; below this ignore the sample

    if (isCornerScaleHandle(m_activeHandle)) {
        if (fromCenter) {
            const qreal d0 = QLineF(itemCentre, m_pressScenePos).length();
            const qreal d1 = QLineF(itemCentre, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
                setItemShear(m_pressShear);
            }
        } else {
            const QPointF anchor = m_pressAnchorScene;
            const qreal d0 = QLineF(anchor, m_pressScenePos).length();
            const qreal d1 = QLineF(anchor, scenePos).length();
            if (d0 > kMinDist) {
                const qreal f = d1 / d0;
                setItemScale(m_pressScaleX * f, m_pressScaleY * f);
                setItemShear(m_pressShear);
                const QPointF now = mapToScene(m_pressAnchorLocal);
                setPos(pos() + (anchor - now));
            }
        }
        return;
    }

    if (isEdgeScaleHandle(m_activeHandle)) {
        // Image axes in *scene* at press — must match PlacementLinear/Qt (not a
        // textbook CCW formula; Qt rotate is clockwise with Y-down).
        const QTransform Lpress = PlacementLinear::make(
            m_pressScaleX, m_pressScaleY, m_pressShear, m_pressRotation);
        const QPointF axisX = Lpress.map(QPointF(1.0, 0.0));
        const QPointF axisY = Lpress.map(QPointF(0.0, 1.0));

        qreal sx = m_pressScaleX;
        qreal sy = m_pressScaleY;
        const bool stretchX = (m_activeHandle == Handle::ScaleLeft
                               || m_activeHandle == Handle::ScaleRight);

        if (fromCenter) {
            const QPointF v0 = m_pressScenePos - itemCentre;
            const QPointF v1 = scenePos - itemCentre;
            if (stretchX) {
                const qreal len0 = QPointF::dotProduct(v0, axisX) / qMax(1e-9, QPointF::dotProduct(axisX, axisX));
                const qreal len1 = QPointF::dotProduct(v1, axisX) / qMax(1e-9, QPointF::dotProduct(axisX, axisX));
                // len is in "press-scale units" of half-width; recover scale factor.
                if (qAbs(len0) > 1e-6) {
                    sx = m_pressScaleX * (qAbs(len1) / qAbs(len0));
                }
            } else {
                const qreal len0 = QPointF::dotProduct(v0, axisY) / qMax(1e-9, QPointF::dotProduct(axisY, axisY));
                const qreal len1 = QPointF::dotProduct(v1, axisY) / qMax(1e-9, QPointF::dotProduct(axisY, axisY));
                if (qAbs(len0) > 1e-6) {
                    sy = m_pressScaleY * (qAbs(len1) / qAbs(len0));
                }
            }
        } else {
            // Anchor fixed: project (pointer - anchor) onto the stretch axis.
            const QPointF anchor = m_pressAnchorScene;
            const QPointF v0 = m_pressScenePos - anchor;
            const QPointF v1 = scenePos - anchor;
            if (stretchX) {
                const qreal uAxis = qMax(1e-9, QPointF::dotProduct(axisX, axisX));
                const qreal len0 = QPointF::dotProduct(v0, axisX) / uAxis;
                const qreal len1 = QPointF::dotProduct(v1, axisX) / uAxis;
                if (qAbs(len0) > 1e-6) {
                    sx = m_pressScaleX * (qAbs(len1) / qAbs(len0));
                }
            } else {
                const qreal uAxis = qMax(1e-9, QPointF::dotProduct(axisY, axisY));
                const qreal len0 = QPointF::dotProduct(v0, axisY) / uAxis;
                const qreal len1 = QPointF::dotProduct(v1, axisY) / uAxis;
                if (qAbs(len0) > 1e-6) {
                    sy = m_pressScaleY * (qAbs(len1) / qAbs(len0));
                }
            }
        }
        setItemScale(sx, sy);
        // Scale-only drag must not disturb shear.
        setItemShear(m_pressShear);
        if (!fromCenter) {
            const QPointF now = mapToScene(m_pressAnchorLocal);
            setPos(pos() + (m_pressAnchorScene - now));
        }
    }
}

void ImageItem::applyShearHandleDrag(const QPointF &scenePos)
{
    // Canonical pose is R·H(kx)·S with only horizontal shear. Top/Bottom grips
    // edit kx directly. Left/Right grips apply a *vertical* local shear V(m)
    // (tilt top/bottom edges), then re-decompose L·V into R·H·S so the same
    // 4-DOF model holds.
    //
    // Horizontal: H(kx) maps (x,y)→(x+kx·y, y); e2' = e2 + kx·e1 (via L·H).
    // Vertical:   V(m)  maps (x,y)→(x, y+m·x); e1' = e1 + m·e2  (via L·V).
    const QPointF gripLocal = handleCenter(m_activeHandle);
    const QPointF anchor = m_pressAnchorScene;

    QPointF e1, e2;
    PlacementLinear::unitAxes(m_pressScaleX, m_pressScaleY, m_pressShear,
                              m_pressRotation, &e1, &e2);

    const bool verticalEdge = (m_activeHandle == Handle::ShearLeft
                               || m_activeHandle == Handle::ShearRight);

    if (!verticalEdge) {
        // Top / Bottom: edit horizontal shear kx. Motion along local +X.
        QPointF dirX = e1;
        const qreal lenX = qHypot(dirX.x(), dirX.y());
        if (lenX > 1e-9) {
            dirX /= lenX;
        }
        const qreal yLever = gripLocal.y();
        const qreal lever = qMax(1e-3, qAbs(yLever));
        const qreal len0 = QPointF::dotProduct(m_pressScenePos - anchor, dirX);
        const qreal len1 = QPointF::dotProduct(scenePos - anchor, dirX);
        // L·H(kx): e2_new = e2 + kx·e1 ⇒ scene move of a point with local y
        // along e1 is proportional to kx · |e1| · y (≈ sx·kx·y).
        const qreal denom = qMax(1e-6, m_pressScaleX * lever);
        const qreal delta = (len1 - len0) / denom;
        qreal kx = m_pressShear;
        if (yLever < 0.0) {
            kx = m_pressShear - delta;
        } else {
            kx = m_pressShear + delta;
        }
        setItemShear(kx);
    } else {
        // Left / Right: vertical local shear m via L·V(m), then decompose.
        QPointF dirY = e2;
        const qreal lenY = qHypot(dirY.x(), dirY.y());
        if (lenY > 1e-9) {
            dirY /= lenY;
        }
        const qreal xLever = gripLocal.x();
        const qreal lever = qMax(1e-3, qAbs(xLever));
        const qreal len0 = QPointF::dotProduct(m_pressScenePos - anchor, dirY);
        const qreal len1 = QPointF::dotProduct(scenePos - anchor, dirY);
        // L·V(m): e1_new = e1 + m·e2 ⇒ move along e2 ∝ m · |e2| · x (≈ sy·m·x).
        const qreal denom = qMax(1e-6, m_pressScaleY * lever);
        const qreal delta = (len1 - len0) / denom;
        qreal m = 0.0;
        if (xLever < 0.0) {
            m = -delta; // left edge (x<0)
        } else {
            m = delta;
        }
        // Compose vertical shear onto press axes: e1' = e1 + m·e2, e2' = e2.
        const QPointF e1n = e1 + m * e2;
        const QPointF e2n = e2;
        qreal sx = m_pressScaleX;
        qreal sy = m_pressScaleY;
        qreal kx = m_pressShear;
        qreal rot = m_pressRotation;
        if (PlacementLinear::decomposeAxes(e1n, e2n, &sx, &sy, &kx, &rot)) {
            setItemScale(qBound(0.01, sx, 50.0), qBound(0.01, sy, 50.0));
            setItemShear(kx);
            setItemRotation(rot);
        }
    }

    const QPointF now = mapToScene(m_pressAnchorLocal);
    setPos(pos() + (anchor - now));
}

qreal ImageItem::chromeButtonSize() const
{
    return kChromeBtnScreenPx / screenScale();
}

QRectF ImageItem::opacitySliderRect() const
{
    // Approximate local rect for legacy callers only. Paint / hit / drag use
    // opacityTrackView() in viewport space (left outside, vertical, adaptive).
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
    if (QLineF(tl, br).length() < kMinFrameDiagPx) {
        return;
    }
    const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
    QPointF a, b;
    opacityTrackView(fg, &a, &b);
    const QPointF p = sceneToViewPx(scenePos);
    const QPointF ab = b - a;
    const qreal ab2 = QPointF::dotProduct(ab, ab);
    qreal tval = 0.0;
    if (ab2 > 1e-6) {
        tval = qBound(0.0, QPointF::dotProduct(p - a, ab) / ab2, 1.0);
    }
    setItemOpacity(0.05 + tval * 0.95);
}

QList<ImageItem::Handle> ImageItem::activeHandles() const
{
    QList<Handle> handles;
    if (m_scaleHandlesEnabled) {
        handles << Handle::ScaleTopLeft << Handle::ScaleTopRight
                << Handle::ScaleBottomLeft << Handle::ScaleBottomRight
                << Handle::ScaleTop << Handle::ScaleRight
                << Handle::ScaleBottom << Handle::ScaleLeft
                << Handle::ShearTop << Handle::ShearBottom
                << Handle::ShearLeft << Handle::ShearRight;
    }
    handles << Handle::RotateTop << Handle::RotateRight
            << Handle::RotateBottom << Handle::RotateLeft
            << Handle::FlipH << Handle::FlipV
            << Handle::Rotate90CCW << Handle::Rotate90CW
            << Handle::Raise << Handle::Lower
            << Handle::ResetScale << Handle::ResetRotation << Handle::ResetShear
            << Handle::OpacitySlider;
    return handles;
}

bool ImageItem::isUprightChromeHandle(Handle h) const
{
    // Raise/Lower glyphs stay screen-upright so "up" always means raise.
    return h == Handle::Raise || h == Handle::Lower;
}

qreal ImageItem::handleDistanceScreenPx(Handle h, const QPointF &itemPos) const
{
    return QLineF(localToViewPx(handleCenter(h)), localToViewPx(itemPos)).length();
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
    const qreal maxOff = qMax(40.0, content * 0.75);

    auto axisScreenPerLocal = [this](const QPointF &localAxis) -> qreal {
        const QPointF o = localToViewPx(QPointF(0, 0));
        const QPointF p = localToViewPx(localAxis);
        const qreal len = QLineF(o, p).length();
        return qMax(1e-6, len);
    };
    const qreal sx = axisScreenPerLocal(QPointF(1, 0));
    const qreal sy = axisScreenPerLocal(QPointF(0, 1));

    const qreal rotOffX = qMin(kRotateOffsetPx / sx, maxOff);
    const qreal rotOffY = qMin(kRotateOffsetPx / sy, maxOff);
    const qreal btn = qMin(kChromeBtnScreenPx / sy, maxOff / 6.0);
    const qreal gap = qMin(kChromeBtnGapPx / sy, maxOff / 12.0);
    const qreal outX = qMin((kChromeOutsidePx + kChromeBtnScreenPx * 0.5) / sx, maxOff / 3.0);

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
    auto norm = [](QPointF v) -> QPointF {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };

    const QRectF localRect = contentRect();
    const QPointF tl = toView(localRect.topLeft());
    const QPointF tr = toView(localRect.topRight());
    const QPointF br = toView(localRect.bottomRight());
    const QPointF bl = toView(localRect.bottomLeft());
    const QPointF centerV = toView(localRect.center());
    const QPointF p = toView(itemPos);

    // Degenerate frame: no chrome hits (matches paint early-out).
    if (QLineF(tl, br).length() < kMinFrameDiagPx) {
        return Handle::None;
    }

    const QPointF dirTop = norm(tr - tl);
    const QPointF dirRight = norm(br - tr);
    const QPointF dirBottom = norm(bl - br);
    const QPointF dirLeft = norm(tl - bl);
    auto outward = [&](const QPointF &mid, const QPointF &along) -> QPointF {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - centerV) < 0) {
            n = -n;
        }
        return n;
    };
    const QPointF midTop = (tl + tr) * 0.5;
    const QPointF midRight = (tr + br) * 0.5;
    const QPointF midBottom = (br + bl) * 0.5;
    const QPointF midLeft = (bl + tl) * 0.5;
    const QPointF outTop = outward(midTop, dirTop);
    const QPointF outRight = outward(midRight, dirRight);
    const QPointF outBottom = outward(midBottom, dirBottom);
    const QPointF outLeft = outward(midLeft, dirLeft);

    // Opacity + chrome: same adaptive outside layout as paint.
    {
        const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
        QPointF a, b;
        opacityTrackView(fg, &a, &b);
        const QPointF ab = b - a;
        const qreal ab2 = QPointF::dotProduct(ab, ab);
        qreal tt = 0.0;
        if (ab2 > 1e-6) {
            tt = qBound(0.0, QPointF::dotProduct(p - a, ab) / ab2, 1.0);
        }
        if (QLineF(p, a + ab * tt).length() <= kChromeHitScreenPx) {
            return Handle::OpacitySlider;
        }

        QPointF centers[kChromeCount];
        chromeCentersView(fg, centers);
        const Handle chromeHandles[] = {
            Handle::FlipH, Handle::FlipV, Handle::Rotate90CCW, Handle::Rotate90CW,
            Handle::Raise, Handle::Lower, Handle::ResetScale, Handle::ResetRotation,
            Handle::ResetShear
        };
        Handle best = Handle::None;
        qreal bestDist = 1e300;
        for (int i = 0; i < kChromeCount; ++i) {
            const bool isToggle = (chromeHandles[i] == Handle::FlipH
                                   || chromeHandles[i] == Handle::FlipV);
            if (isToggle) {
                // Rounded-square toggles: axis-aligned hit box (viewport px).
                const qreal half = kChromeBtnScreenPx * 0.55 + 3.0;
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
                if (d <= kChromeHitScreenPx && d <= bestDist) {
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
    qreal bestDist = kHandleScreenPx * 1.75;
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
    points << PointH{Handle::RotateTop, midTop + outTop * kRotateOffsetPx}
           << PointH{Handle::RotateRight, midRight + outRight * kRotateOffsetPx}
           << PointH{Handle::RotateBottom, midBottom + outBottom * kRotateOffsetPx}
           << PointH{Handle::RotateLeft, midLeft + outLeft * kRotateOffsetPx};
    for (const PointH &ph : points) {
        const qreal d = QLineF(p, ph.c).length();
        if (d <= bestDist) {
            bestDist = d;
            best = ph.h;
        }
    }

    // Edge stretch bars (short segment around mid-edge in view space)
    if (m_scaleHandlesEnabled) {
        const qreal halfLen = kHandleScreenPx * 1.2;
        const qreal edgeHit = kHandleScreenPx * 0.85;
        struct EdgeH {
            Handle h;
            QPointF mid;
            QPointF along;
        };
        const EdgeH edges[] = {
            {Handle::ScaleTop, midTop, dirTop},
            {Handle::ScaleRight, midRight, dirRight},
            {Handle::ScaleBottom, midBottom, dirBottom},
            {Handle::ScaleLeft, midLeft, dirLeft},
        };
        for (const EdgeH &ed : edges) {
            const QPointF a = ed.mid - ed.along * halfLen;
            const QPointF b = ed.mid + ed.along * halfLen;
            const QPointF ab = b - a;
            const qreal ab2 = QPointF::dotProduct(ab, ab);
            qreal t = 0.0;
            if (ab2 > 1e-6) {
                t = qBound(0.0, QPointF::dotProduct(p - a, ab) / ab2, 1.0);
            }
            const qreal d = QLineF(p, a + ab * t).length();
            if (d <= edgeHit && d <= bestDist) {
                bestDist = d;
                best = ed.h;
            }
        }
        // Shear diamonds: offset along each edge from mid (view space).
        const qreal shearAlong = kHandleScreenPx * 2.2;
        const QPointF shearPts[] = {
            midTop - dirTop * shearAlong,
            midBottom + dirBottom * shearAlong,
            midLeft - dirLeft * shearAlong,
            midRight + dirRight * shearAlong,
        };
        const Handle shearHs[] = {
            Handle::ShearTop, Handle::ShearBottom,
            Handle::ShearLeft, Handle::ShearRight,
        };
        for (int i = 0; i < 4; ++i) {
            const qreal d = QLineF(p, shearPts[i]).length();
            if (d <= kHandleScreenPx * 1.1 && d <= bestDist) {
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
                        iv->applyLayout(GalleryPackReason::ContentChange);
                    } else if (iv->isWorkspaceMode()) {
                        iv->updateWorkspaceSceneRect();
                    }
                    notifyViewStatus();
                    return;
                }
            }
        }
        // Fallback without a view: bake pixels and toggle content indicators.
        if (h == Handle::FlipH) {
            bakeFlip(true, false);
            m_contentHFlip = !m_contentHFlip;
        } else if (h == Handle::FlipV) {
            bakeFlip(false, true);
            m_contentVFlip = !m_contentVFlip;
        } else if (h == Handle::Rotate90CCW) {
            bakeRotate90(-1);
        } else {
            bakeRotate90(1);
        }
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
    case Handle::ResetScale:
        setItemScale(1.0, 1.0);
        setItemShear(0.0);
        break;
    case Handle::ResetRotation:
        setItemRotation(0.0);
        break;
    case Handle::ResetShear:
        setItemShear(0.0);
        break;
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


qreal ImageItem::tileDevicePerContent() const
{
    // Logical view scale × widget devicePixelRatio (Retina / fractional DPI).
    qreal dpr = 1.0;
    if (scene() && !scene()->views().isEmpty() && scene()->views().first()) {
        QWidget *vp = scene()->views().first()->viewport();
        if (vp) {
            dpr = vp->devicePixelRatioF();
        }
    }
    if (!(dpr > 0.0)) {
        dpr = 1.0;
    }
    return screenScale() * dpr;
}

ContentXform::Value ImageItem::tileContentXform() const
{
    if (m_hasAppliedContentXform) {
        return m_appliedContentXform;
    }
    ContentXform::Value x;
    x.hFlip = m_contentHFlip;
    x.vFlip = m_contentVFlip;
    x.hasCrop = m_sessionHasCrop;
    // Session crop rect is not stored on the item for all paths; applied
    // xform is preferred. Without applied crop geometry, crop flag alone
    // cannot map — treat as no crop for tile planning.
    return x;
}


static quint64 colorAdjustSignature(const ColorAdjustments &g)
{
    // Stable fingerprint for graded-tile cache keys.
    quint64 h = 1469598103934665603ull;
    auto mix = [&](quint64 v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(static_cast<quint64>(static_cast<uint32_t>(g.brightness)));
    mix(static_cast<quint64>(static_cast<uint32_t>(g.contrast)));
    mix(static_cast<quint64>(static_cast<uint32_t>(g.saturation)));
    mix(static_cast<quint64>(static_cast<uint32_t>(g.hue)));
    mix(static_cast<quint64>(qRound(g.gamma * 1000.0)));
    mix(g.invert ? 1ull : 0ull);
    return h;
}

void ImageItem::clearTileGradedCache() const
{
    m_tileGradedCache.clear();
    m_tileGradeSig = 0;
}

QImage ImageItem::resolveGradedTile(tilelod::TileKey const &key,
                                    ColorAdjustments const &grade) const
{
    if (!m_tileLod || !m_tileLod->session()) {
        return {};
    }
    tilelod::CacheEntry const *e = m_tileLod->session()->cache().find(key);
    if (!e || e->state != tilelod::TileState::Succeeded || !e->bitmap.valid()) {
        return {};
    }
    // Cache QImage conversion for identity and graded alike. Identity used to
    // re-copy every rgba8 cell on every paint frame (256²×4 per tile).
    const quint64 sig = colorAdjustSignature(grade);
    if (sig != m_tileGradeSig) {
        m_tileGradedCache.clear();
        m_tileGradeSig = sig;
    }
    // ~96 MiB of rgba cell images; LRU eviction instead of nuke-at-256.
    constexpr int kGradedCacheMaxKiB = 96 * 1024;
    if (m_tileGradedCache.maxCost() < kGradedCacheMaxKiB) {
        m_tileGradedCache.setMaxCost(kGradedCacheMaxKiB);
    }
    // Pack scale,x,y into one key — avoid QString alloc per cell per paint.
    const quint64 ck = (static_cast<quint64>(static_cast<uint32_t>(key.scale)) << 42)
                       | (static_cast<quint64>(static_cast<uint32_t>(key.x)) << 21)
                       | static_cast<quint64>(static_cast<uint32_t>(key.y));
    if (const QImage *hit = m_tileGradedCache.object(ck)) {
        return *hit; // QImage is implicitly shared
    }
    QImage img = tilelod::tile_bitmap_to_qimage(e->bitmap);
    if (img.isNull()) {
        return {};
    }
    if (!grade.isIdentity()) {
        img = applyColorAdjustments(img, grade);
    }
    const int costKiB = qMax(1, (img.width() * img.height() * 4) / 1024);
    auto *stored = new QImage(std::move(img));
    m_tileGradedCache.insert(ck, stored, costKiB);
    return *stored;
}

QSize ImageItem::tileNativeSize() const
{
    const QSize cached = ThumtooCache::cachedSize(m_path);
    if (cached.isValid() && cached.width() > 0 && cached.height() > 0) {
        return cached;
    }
    // Fall back to layout size when native unknown (identity xform only).
    return imageSize();
}

void ImageItem::setTileLodSuppressed(bool on)
{
    if (m_tileLodSuppressed == on) {
        return;
    }
    m_tileLodSuppressed = on;
    if (on && m_tileLod) {
        // Drop private session so paint cannot draw stale cells over the
        // crop-draft full frame; shared path cache is left intact.
        m_tileLod.reset();
        m_tileLodLastUpdateGen = 0;
        clearTileGradedCache();
    }
}

bool ImageItem::tileLodWanted() const
{
    if (m_tileLodSuppressed || m_path.isEmpty() || !ThumtooCache::isAvailable()) {
        return false;
    }
    // Durable tiles are a *speed* optimization (Store hits), not a requirement.
    // Interactive request_tiles encodes on miss (JPEG DCT shrink for scale>0).
    // Requiring hasDurableTiles forced PreferCache/Full whole-frame climbs
    // (often →2048) before any tile cell could run — redundant with the tile path.
    // Prefer thumtoo size; fall back to layout imageSize so Gallery can enter
    // the tile band before the size probe completes (otherwise LQIP forever).
    QSize native = tileNativeSize();
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        native = imageSize();
    }
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        return false;
    }

    // Gallery packed cell: threshold is *on-screen cell* long edge, not
    // content×(view×item). galleryCellSize is already scene footprint (item
    // scale baked in); only the view transform maps scene → device pixels.
    // Using tileDevicePerContent() (view×item) here under-counted by ~itemScale
    // and blocked Ctrl+wheel inspection tiles (TILE_LOD_RUNTIME.md).
    if (!m_galleryCellSize.isEmpty()) {
        qreal viewScale = 1.0;
        qreal dpr = 1.0;
        if (scene() && !scene()->views().isEmpty() && scene()->views().first()) {
            QGraphicsView *view = scene()->views().first();
            const QTransform vt = view->transform();
            qreal sMax = 1.0;
            qreal sMin = 1.0;
            singularValues2x2(vt.m11(), vt.m12(), vt.m21(), vt.m22(), &sMax, &sMin);
            viewScale = qMax(0.01, sMax);
            if (QWidget *vp = view->viewport()) {
                dpr = vp->devicePixelRatioF();
            }
        }
        if (!(dpr > 0.0)) {
            dpr = 1.0;
        }
        const qreal cellLong = qMax(m_galleryCellSize.width(), m_galleryCellSize.height());
        if (!(cellLong > 0.0)) {
            return false;
        }
        const qreal screenLong = cellLong * viewScale * dpr;
        // Match Image mode (shouldUseTiles ~32px screen): Gallery used 256 and
        // left typical packed cells on soft-only with SOFT debug stamps.
        // Soft underlay remains until the tile plan fully covers.
        constexpr qreal kGalleryTileScreenMin = 32.0;
        return screenLong > kGalleryTileScreenMin;
    }

    // Image / Workspace: layout content long edge × device-per-content.
    const QSize isz = imageSize();
    const int displayLong = qMax(isz.width(), isz.height());
    if (displayLong < 1) {
        return false;
    }
    return tilelod::TileLodController::shouldUseTiles(
        tileDevicePerContent(), displayLong);
}

void ImageItem::prepareTileLodPlan()
{
    if (!tileLodWanted()) {
        return;
    }
    QSize native = tileNativeSize();
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        native = imageSize();
    }
    if (!native.isValid() || native.width() < 1 || native.height() < 1) {
        return;
    }
    if (!m_tileLod) {
        m_tileLod = std::make_unique<tilelod::TileLodController>();
        m_tileLod->setPath(m_path);
    } else if (m_tileLod->path() != m_path) {
        m_tileLod->setPath(m_path);
    }
    // Tile grid is always full native (source) size.
    // min_scale from durable coverage so we do not request finer than the pyramid.
    m_tileLod->setContentSize(native.width(), native.height(),
                              ThumtooCache::durableTileMinScale(m_path));

    const qreal dpc = tileDevicePerContent();
    QRectF visLocal = contentRect();
    if (scene() && !scene()->views().isEmpty() && scene()->views().first()) {
        QGraphicsView *view = scene()->views().first();
        const QRectF sceneVis =
            view->mapToScene(view->viewport()->rect()).boundingRect();
        const QRectF localVis = mapFromScene(sceneVis).boundingRect();
        visLocal = localVis.intersected(contentRect());
    }
    if (visLocal.isEmpty()) {
        return;
    }
    // Item local → display content (0..layout); then → source for the planner.
    const QRectF visDisplay = visLocal.translated(-offset());
    const ContentXform::Value x = tileContentXform();
    QRectF visSource = ContentXform::mapDisplayRectToSource(visDisplay, native, x);
    if (visSource.isEmpty()) {
        // Identity xform: display == source when layout matches native.
        visSource = visDisplay;
    }
    // Skip set_viewport when density and visible region are unchanged — paint
    // runs this every frame while tiles stream in; replanning is pure waste.
    // Progressive climb advances in TileSession::pump/issue (tick path), not here.
    if (m_tileLod->session()
        && m_tileLodLastDpc > 0.0
        && qAbs(dpc - m_tileLodLastDpc) < 1e-4
        && qAbs(visSource.x() - m_tileLodLastVisSource.x()) < 0.5
        && qAbs(visSource.y() - m_tileLodLastVisSource.y()) < 0.5
        && qAbs(visSource.width() - m_tileLodLastVisSource.width()) < 0.5
        && qAbs(visSource.height() - m_tileLodLastVisSource.height()) < 0.5) {
        return;
    }
    m_tileLodLastDpc = dpc;
    m_tileLodLastVisSource = visSource;
    // Soft is continuous base; only set once (set_has_lqip no-ops on same value).
    m_tileLod->setHasLqip(false);
    // Prefetch margin in content pixels: ~one tile side of *screen* space
    // (256 device px). Enough to absorb small pans without issuing a second
    // ring of cells; not a full off-screen ring (that multiplies issue work).
    const double margin = 256.0 / qMax(1e-6, dpc);
    m_tileLod->updateViewport(visSource, dpc, margin);
    // Completions arrive off the GUI; without a wake, pump never runs until the
    // next pan/scroll and new tiles never repaint (ImageView "stuck coarse").
    if (m_tileLod->session()) {
        std::shared_ptr<bool> alive = m_tileLodAlive;
        m_tileLod->session()->set_wake([this, alive]() {
            QTimer::singleShot(0, QCoreApplication::instance(), [this, alive]() {
                if (!alive || !*alive || !m_tileLod) {
                    return;
                }
                const int applied = m_tileLod->tick(12);
                Q_UNUSED(applied);
                update();
                if (scene()) {
                    for (QGraphicsView *v : scene()->views()) {
                        if (v && v->viewport()) {
                            v->viewport()->update();
                        }
                    }
                }
            });
        });
    }
}

void ImageItem::prepareTileLod()
{
    prepareTileLodPlan();
}

void ImageItem::tickTileLod(int budget)
{
    // Sole per-item tile service entry (called only from TileLoadCoordinator).
    // Leave tile band: do not pump/issue on a stale deep-zoom viewport.
    if (!tileLodWanted()) {
        // Size probe may still be pending — without it we never enter the band.
        if (!m_path.isEmpty() && !ThumtooCache::cachedSize(m_path).isValid()) {
            ThumtooCache::scheduleProbe(m_path);
        }
        if (!m_interactive && cacheMode() == QGraphicsItem::NoCache
            && hasDisplayPixels()) {
            syncGalleryScrollCache();
        }
        return;
    }
    // Gallery tile cells: plan changes often — keep NoCache while in the band
    // so ItemCoordinateCache cannot freeze an LQIP pixmap over live tiles.
    if (!m_interactive && cacheMode() != QGraphicsItem::NoCache) {
        setCacheMode(QGraphicsItem::NoCache);
    }
    prepareTileLod();
    if (!m_tileLod) {
        return;
    }
    const int applied = m_tileLod->tick(budget);
    const std::uint64_t gen =
        m_tileLod->session() ? m_tileLod->session()->generation() : 0;
    // Repaint only when tiles actually landed — gen-only changes every pan
    // queued a singleShot(0) storm (100% CPU, still LQIP).
    if (applied > 0) {
        m_tileLodLastUpdateGen = gen;
        if (!m_interactive) {
            setCacheMode(QGraphicsItem::NoCache);
            // Keep LQIP pixmap until paint draws tiles over it — clearing
            // caused temporary disappear (blank cells while plan catches up).
        }
        if (!m_tileLodRepaintQueued) {
            m_tileLodRepaintQueued = true;
            QGraphicsScene *sc = scene();
            QObject *ctx = sc ? static_cast<QObject *>(sc)
                              : static_cast<QObject *>(QCoreApplication::instance());
            std::shared_ptr<bool> alive = m_tileLodAlive;
            QTimer::singleShot(0, ctx, [this, sc, alive]() {
                if (!alive || !*alive) {
                    return;
                }
                if (sc && scene() != sc) {
                    m_tileLodRepaintQueued = false;
                    return;
                }
                m_tileLodRepaintQueued = false;
                update();
            });
        }
    } else if (gen != m_tileLodLastUpdateGen) {
        m_tileLodLastUpdateGen = gen;
        // Plan-only change (pan): cheap mark, no pixmap clear.
        update();
    }
}

bool ImageItem::tileLodActive() const
{
    return m_tileLod && m_tileLod->enabled() && m_tileLod->hasAnyTile();
}

bool ImageItem::tileLodViewportCovered() const
{
    return m_tileLod && m_tileLod->viewportFullyCovered();
}

QString ImageItem::tileLodDebugLine() const
{
    const QString name = QFileInfo(m_path).fileName();
    if (!tileLodWanted()) {
        return QStringLiteral("%1 wanted=0 disp=%2")
            .arg(name)
            .arg(displayPixelLongEdge());
    }
    if (!m_tileLod || !m_tileLod->session()) {
        return QStringLiteral("%1 wanted=1 session=0 disp=%2")
            .arg(name)
            .arg(displayPixelLongEdge());
    }
    const tilelod::TileSession::DebugSnapshot s =
        m_tileLod->session()->debug_snapshot();
    return QStringLiteral(
               "%1 tgt=%2 des=%3 max=%4 vis=%5 exact=%6 inflight=%7 "
               "cacheOk=%8 hold=%9 reached=%10 gen=%11 disp=%12")
        .arg(name)
        .arg(s.target_scale)
        .arg(s.desired_scale)
        .arg(s.max_scale)
        .arg(s.visible)
        .arg(s.exact_succeeded)
        .arg(s.in_flight)
        .arg(s.cache_succeeded)
        .arg(s.holding ? 1 : 0)
        .arg(s.reached_desired ? 1 : 0)
        .arg(static_cast<qulonglong>(s.generation))
        .arg(displayPixelLongEdge());
}

void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                      QWidget *widget)
{
    GUI_BUDGET_MS("ImageItem::paint", 3);
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
        // LQIP base until exact target coverage; keep under live coarse tiles too.
        const bool drawLqipBase = !tilesFullyCover;
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

        // Gallery: underlay is LQIP-only under tileLodWanted (soft PreferCache
        // removed — GALLERY_PIXELS.md). Image/Workspace (interactive): any
        // in-process host (LQIP, filmstrip thumb, prior sample) may underlay
        // until tiles fully cover — otherwise nav-hot (no tile paint) + a
        // >96 host left a blank frame while ImageCache was hot.
        const bool galleryLqipOnlyUnderTiles = tilesWanted && !m_interactive;
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
                const qreal inset = qMin(cr.width(), cr.height()) * 0.06;
                const QRectF inner = cr.adjusted(inset, inset, -inset, -inset);
                painter->setPen(QPen(QColor(70, 72, 80), 0));
                painter->setBrush(QColor(52, 54, 62));
                painter->drawRoundedRect(inner, inset * 0.8, inset * 0.8);
                painter->setPen(QPen(QColor(140, 145, 160), 0));
                QFont f = painter->font();
                const qreal edge = qMin(inner.width(), inner.height());
                f.setPointSizeF(qBound(8.0, edge * 0.08, 28.0));
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
                navHot = iv->slideshowNavHot();
            }
        }
        if (tileLodWanted() && !navHot) {
            prepareTileLodPlan();
            if (m_tileLod && m_tileLod->session()) {
                const QImage under = hasDecodedPixels() ? m_source
                    : (!m_preview.isNull() ? m_preview : QImage());
                tilelod::DrawPlan plan = m_tileLod->session()->draw_plan();
                const QSize native = tileNativeSize();
                const ContentXform::Value x = tileContentXform();
                const QPointF off = offset();
                const bool freeRot = x.hasCrop && !x.cropRect.isEmpty()
                    && qAbs(x.cropRotation) > 1e-3;

                ColorAdjustments grade = x.colorAdjust;
                if (grade.isIdentity() && !m_colorAdjust.isIdentity()) {
                    grade = m_colorAdjust;
                }
                auto resolve = [this, grade](tilelod::TileKey const &key,
                                             tilelod::TileBitmap const &) -> QImage {
                    return resolveGradedTile(key, grade);
                };

                if (freeRot) {
                    // Draw tiles in oriented space under the same centre/rotate
                    // transform as materializeDisplay (not AABB-squashed).
                    for (tilelod::DrawCommand &cmd : plan.commands) {
                        QRectF srcBox(cmd.dst_content.x, cmd.dst_content.y,
                                      cmd.dst_content.w, cmd.dst_content.h);
                        QRectF ori = ContentXform::mapSourceRectToOriented(
                            srcBox, native, x);
                        if (ori.isEmpty()) {
                            ori = srcBox;
                        }
                        cmd.dst_content = {ori.x(), ori.y(), ori.width(), ori.height()};
                    }
                    const QRect contentCrop = x.cropRect.normalized();
                    const QRectF cr = contentRect();
                    painter->save();
                    painter->setClipRect(cr);
                    painter->translate(cr.center());
                    painter->rotate(-x.cropRotation);
                    painter->translate(-QPointF(contentCrop.center()));
                    tilelod::PaintDrawPlanArgs args;
                    args.plan = &plan;
                    args.lqip = QImage(); // soft already in display space
                    args.smooth = tilePaintNeedsSmooth(
                        tileDevicePerContent(),
                        m_tileLod->session()->target_scale(), plan);
                    args.resolve = resolve;
                    tilelod::paint_draw_plan(painter, args);

                if (tilePlanDebugOverlayEnabled()) {
                    paintTilePlanDebugOverlay(painter, m_tileLod->session(), plan,
                                             contentRect());
                }

                    painter->restore();
                } else {
                    for (tilelod::DrawCommand &cmd : plan.commands) {
                        QRectF srcBox(cmd.dst_content.x, cmd.dst_content.y,
                                      cmd.dst_content.w, cmd.dst_content.h);
                        QRectF disp = ContentXform::mapSourceRectToDisplay(
                            srcBox, native, x);
                        if (disp.isEmpty()) {
                            disp = srcBox;
                        }
                        cmd.dst_content = {disp.x() + off.x(), disp.y() + off.y(),
                                           disp.width(), disp.height()};
                    }
                    tilelod::PaintDrawPlanArgs args;
                    args.plan = &plan;
                    // Soft is already painted over contentRect above. Passing
                    // it as lqip would stretch the *full* soft into every
                    // Underlay cell (repeated mini-images in holes). Leave
                    // empty so holes show the continuous soft underneath;
                    // CoarserTile stand-ins still paint via resolve.
                    args.lqip = QImage();
                    args.smooth = tilePaintNeedsSmooth(
                        tileDevicePerContent(),
                        m_tileLod->session()->target_scale(), plan);
                    args.resolve = resolve;
                    tilelod::paint_draw_plan(painter, args);
                    (void)under;

                if (tilePlanDebugOverlayEnabled()) {
                    paintTilePlanDebugOverlay(painter, m_tileLod->session(), plan,
                                             contentRect());
                }

                }
            }
        }
        painter->restore();
    }

    const QRectF r = cropped ? crop : displayContentRect();

    // Content-edit marks (View → Show content edit marks). Fixed ~20px on screen
    // (not fraction of tile — Gallery was tiny, tight crops were huge).
    if (contentEditMarksVisible() && r.width() > 4.0 && r.height() > 4.0) {
        const qreal fold = kContentEditMarkScreenPx / qMax(0.01, screenScale());

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

        // Crop: yellow fold, bottom-right.
        if (m_sessionHasCrop
            || (m_hasAppliedContentXform && m_appliedContentXform.hasCrop)) {
            drawCornerFold(QPointF(r.right(), r.bottom()),
                           QPointF(-fold, 0), QPointF(0, -fold),
                           QColor(242, 196, 40), QColor(200, 150, 20));
        }

        // Orient (flip / 90°): cyan fold, bottom-left.
        bool orient = m_contentHFlip || m_contentVFlip;
        if (m_hasAppliedContentXform) {
            const ContentXform::Value &x = m_appliedContentXform;
            orient = orient || x.hFlip || x.vFlip || x.quarterTurns != 0;
        }
        if (orient) {
            drawCornerFold(QPointF(r.left(), r.bottom()),
                           QPointF(fold, 0), QPointF(0, -fold),
                           QColor(56, 189, 248), QColor(14, 116, 144));
        }

        // Grade: coral fold, slightly inset from bottom-left when orient also set.
        const bool grade = !m_colorAdjust.isIdentity();
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

QString ImageItem::handleToolTip(Handle h)
{
    switch (h) {
    case Handle::None:
        return {};
    case Handle::ScaleTopLeft:
    case Handle::ScaleTopRight:
    case Handle::ScaleBottomLeft:
    case Handle::ScaleBottomRight:
        return QCoreApplication::translate("ImageItem", "Scale (Shift: opposite edge; Ctrl: about centre)");
    case Handle::ScaleTop:
    case Handle::ScaleBottom:
        return QCoreApplication::translate("ImageItem", "Scale height");
    case Handle::ScaleLeft:
    case Handle::ScaleRight:
        return QCoreApplication::translate("ImageItem", "Scale width");
    case Handle::ShearTop:
    case Handle::ShearBottom:
    case Handle::ShearLeft:
    case Handle::ShearRight:
        return QCoreApplication::translate("ImageItem", "Shear");
    case Handle::RotateTop:
    case Handle::RotateRight:
    case Handle::RotateBottom:
    case Handle::RotateLeft:
        return QCoreApplication::translate("ImageItem", "Rotate");
    case Handle::FlipH:
        return QCoreApplication::translate("ImageItem", "Flip horizontal");
    case Handle::FlipV:
        return QCoreApplication::translate("ImageItem", "Flip vertical");
    case Handle::Rotate90CCW:
        return QCoreApplication::translate("ImageItem", "Rotate 90° counter-clockwise");
    case Handle::Rotate90CW:
        return QCoreApplication::translate("ImageItem", "Rotate 90° clockwise");
    case Handle::Raise:
        return QCoreApplication::translate("ImageItem", "Raise (bring forward)");
    case Handle::Lower:
        return QCoreApplication::translate("ImageItem", "Lower (send backward)");
    case Handle::ResetScale:
        return QCoreApplication::translate("ImageItem", "Reset scale to 1:1");
    case Handle::ResetRotation:
        return QCoreApplication::translate("ImageItem", "Reset rotation to 0°");
    case Handle::ResetShear:
        return QCoreApplication::translate("ImageItem", "Reset shear");
    case Handle::OpacitySlider:
        return QCoreApplication::translate("ImageItem", "Opacity");
    }
    return {};
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

    auto norm = [](QPointF v) -> QPointF {
        const qreal len = qHypot(v.x(), v.y());
        if (len > 1e-6) {
            return v / len;
        }
        return QPointF(1, 0);
    };
    // Unit edge directions (view space) and outward normals (away from centre).
    const QPointF dirTop = norm(tr - tl);
    const QPointF dirRight = norm(br - tr);
    const QPointF dirBottom = norm(bl - br);
    const QPointF dirLeft = norm(tl - bl);
    auto outward = [&](const QPointF &mid, const QPointF &along) -> QPointF {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - centerV) < 0) {
            n = -n;
        }
        return n;
    };
    const QPointF midTop = (tl + tr) * 0.5;
    const QPointF midRight = (tr + br) * 0.5;
    const QPointF midBottom = (br + bl) * 0.5;
    const QPointF midLeft = (bl + tl) * 0.5;
    const QPointF outTop = outward(midTop, dirTop);
    const QPointF outRight = outward(midRight, dirRight);
    const QPointF outBottom = outward(midBottom, dirBottom);
    const QPointF outLeft = outward(midLeft, dirLeft);

    painter->save();
    painter->setOpacity(1.0);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal hs = kHandleScreenPx;

    // Degenerate frame (item scale near zero / collapsed): skip detailed chrome
    // so we never feed zero-length edges into norm() or draw Inf positions.
    // Threshold is deliberately higher than a few pixels so handles stay visible
    // while the image is still clearly on screen.
    const qreal frameDiag = QLineF(tl, br).length();
    const bool frameOk = frameDiag >= kMinFrameDiagPx;

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
    auto drawRotate = [&](Handle h, const QPointF &edgeMid, const QPointF &outN) {
        const QPointF c = edgeMid + outN * kRotateOffsetPx;
        const bool hot = (m_hoverHandle == h || m_activeHandle == h);
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
    drawRotate(Handle::RotateTop, midTop, outTop);
    drawRotate(Handle::RotateRight, midRight, outRight);
    drawRotate(Handle::RotateBottom, midBottom, outBottom);
    drawRotate(Handle::RotateLeft, midLeft, outLeft);

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
            const bool hot = (m_hoverHandle == co.h || m_activeHandle == co.h);
            const QPointF c = co.corner;
            const QPointF d1 = norm(co.alongA);
            const QPointF d2 = norm(co.alongB);
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
            const bool hot = (m_hoverHandle == ed.h || m_activeHandle == ed.h);
            const QPointF along = norm(ed.along);
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
        const qreal shearAlong = hs * 2.2;
        struct ShearD {
            Handle h;
            QPointF c;
        };
        const ShearD shears[] = {
            {Handle::ShearTop, midTop - dirTop * shearAlong},
            {Handle::ShearBottom, midBottom + dirBottom * shearAlong},
            {Handle::ShearLeft, midLeft - dirLeft * shearAlong},
            {Handle::ShearRight, midRight + dirRight * shearAlong},
        };
        for (const ShearD &sh : shears) {
            const bool hot = (m_hoverHandle == sh.h || m_activeHandle == sh.h);
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
        const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
        QPointF centers[kChromeCount];
        chromeCentersView(fg, centers);

        // Design language (HANDLES.md):
        //   circle  = momentary action (click once)
        //   rounded square = latching toggle
        //   open strokes on the frame = geometry grips (elsewhere)
        const qreal btnR = kChromeBtnScreenPx / 2.0;
        const qreal toggleHalf = kChromeBtnScreenPx * 0.52; // half-side of toggle square

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
            f.setPointSizeF(qMax(8.0, half * 0.72));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(box, Qt::AlignCenter, glyph);
        };

        auto drawBtn = [&](Handle h, int index, const QString &glyph) {
            const QPointF c = centers[index];
            const bool hovered = (m_hoverHandle == h);
            const bool active = (m_activeHandle == h);
            const qreal rad = btnR * (hovered || active ? 1.08 : 1.0);
            QColor fill = hovered || active ? QColor(0, 140, 255, 240) : QColor(50, 50, 50, 230);
            QPen border(hovered || active ? QColor(255, 255, 255) : QColor(0, 160, 255));
            border.setWidthF(hovered || active ? 1.75 : 1.0);
            border.setCosmetic(true);
            painter->setPen(border);
            painter->setBrush(fill);
            painter->drawEllipse(c, rad, rad);
            painter->setPen(QColor(240, 240, 240));
            QFont f = painter->font();
            f.setPointSizeF(qMax(7.0, rad * 0.55));
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(QRectF(c.x() - rad, c.y() - rad, rad * 2, rad * 2),
                              Qt::AlignCenter, glyph);
        };

        // contentHFlip/VFlip are *source* flags (flip → then quarter-turns). Chrome
        // labels are display axes: after odd turns H↔V, so light the conjugated
        // pair or FlipH looks like FlipV after a 90° rotate.
        int turns = 0;
        if (hasAppliedContentXform()) {
            turns = appliedContentXform().quarterTurns % 4;
            if (turns < 0) {
                turns += 4;
            }
        }
        const bool swapAxes = (turns == 1 || turns == 3);
        const bool displayHFlip = swapAxes ? m_contentVFlip : m_contentHFlip;
        const bool displayVFlip = swapAxes ? m_contentHFlip : m_contentVFlip;
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
        const FrameViewGeom fg = makeFrameViewGeom(tl, tr, br, bl);
        QPointF a, b;
        opacityTrackView(fg, &a, &b);
        const QPointF ab = b - a;
        const qreal abLen = qHypot(ab.x(), ab.y());
        const QPointF along = abLen > 1e-6 ? ab / abLen : QPointF(0, -1);
        Q_UNUSED(along);
        const QPointF perp = fg.outLeft;
        const qreal thick = kSliderHeightPx;
        const bool hot = (m_hoverHandle == Handle::OpacitySlider
                          || m_activeHandle == Handle::OpacitySlider);
        const qreal tval = (m_opacity - 0.05) / 0.95;
        QPen trackPen(hot ? QColor(200, 180, 255) : QColor(120, 100, 160), 0);
        trackPen.setCosmetic(true);
        trackPen.setWidthF(1.15);
        painter->setPen(trackPen);
        painter->setBrush(QColor(40, 40, 40, hot ? 230 : 200));
        QPolygonF trackPoly;
        trackPoly << a + perp * (thick / 2) << b + perp * (thick / 2)
                  << b - perp * (thick / 2) << a - perp * (thick / 2);
        painter->drawPolygon(trackPoly);
        const QPointF mid = a + ab * qBound(0.0, tval, 1.0);
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


bool ImageItem::beginHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods)
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
    m_activeHandle = h;
    m_pressScenePos = scenePos;
    m_pressScaleX = m_scaleX;
    m_pressScaleY = m_scaleY;
    m_pressShear = m_shear;
    m_pressRotation = m_rotation;
    m_pressItemPos = mapFromScene(scenePos);
    m_pressAnchorLocal = scaleAnchorLocal(h);
    m_pressAnchorScene = mapToScene(m_pressAnchorLocal);
    if (h == Handle::OpacitySlider) {
        setOpacityFromSliderPos(scenePos);
        notifyViewStatus();
    }
    return true;
}

void ImageItem::updateHandleInteraction(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (m_activeHandle == Handle::None) {
        return;
    }
    if (m_activeHandle == Handle::OpacitySlider) {
        setOpacityFromSliderPos(scenePos);
    } else if (isRotateHandle(m_activeHandle)) {
        const QPointF itemCentre = this->scenePos();
        const QPointF v0 = m_pressScenePos - itemCentre;
        const QPointF v1 = scenePos - itemCentre;
        const qreal a0 = qAtan2(v0.y(), v0.x());
        const qreal a1 = qAtan2(v1.y(), v1.x());
        const qreal deltaDeg = qRadiansToDegrees(a1 - a0);
        qreal angle = m_pressRotation + deltaDeg;
        // Ctrl → 45° (includes 90°); Shift (alone or with Ctrl) → 15°.
        if (mods & Qt::ShiftModifier) {
            angle = qRound(angle / 15.0) * 15.0;
        } else if (mods & Qt::ControlModifier) {
            angle = qRound(angle / 45.0) * 45.0;
        }
        setItemRotation(angle);
    } else if (isScaleHandle(m_activeHandle)) {
        applyScaleHandleDrag(scenePos, mods);
    } else if (isShearHandle(m_activeHandle)) {
        applyShearHandleDrag(scenePos);
    }
    notifyViewStatus();
}

void ImageItem::endHandleInteraction()
{
    // Free-rotate / scale: persist when the gesture finishes.
    if (m_activeHandle != Handle::None && m_activeHandle != Handle::OpacitySlider) {
        if (scene()) {
            for (QGraphicsView *v : scene()->views()) {
                if (auto *iv = qobject_cast<ImageView *>(v)) {
                    iv->commitItemSessionEdit(this);
                    break;
                }
            }
        }
    }
    m_activeHandle = Handle::None;
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
    if (m_galleryHovered) {
        m_galleryHovered = false;
    }
    if (m_hoverHandle != Handle::None) {
        m_hoverHandle = Handle::None;
        setToolTip(QString());
    }
    unsetCursor();
    QGraphicsPixmapItem::hoverLeaveEvent(event);
}
