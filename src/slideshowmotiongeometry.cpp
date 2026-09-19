// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshowmotiongeometry.h"

#include <QHash>
#include <QtGlobal>

namespace SlideshowMotionGeometry {

QRectF coverDestRect(SlideshowMotion motion, qreal baseScale, qreal panZoomFactor,
                     qreal iw, qreal ih, int vw, int vh, qreal motionT,
                     QPointF biasA, QPointF biasB, bool dwellBiasValid,
                     const QString &pathForSeed)
{
    motionT = qBound(0.0, motionT, 1.0);
    if (baseScale <= 0.0 || !qIsFinite(baseScale) || iw < 1.0 || ih < 1.0
        || vw < 1 || vh < 1) {
        return QRectF();
    }

    qreal scale = baseScale;
    qreal biasX = 0.0;
    qreal biasY = 0.0;
    qreal destX = 0.0;
    qreal destY = 0.0;
    bool destFromOffset = false;

    if (motion == SlideshowMotion::PanScan) {
        qreal s = baseScale;
        const bool preferX = iw * qreal(vh) >= ih * qreal(vw);
        {
            const qreal viewW = qreal(vw) / s;
            const qreal viewH = qreal(vh) / s;
            const qreal halfX = qMax(0.0, (iw - viewW) * 0.5);
            const qreal halfY = qMax(0.0, (ih - viewH) * 0.5);
            const qreal travel = preferX ? halfX : halfY;
            const qreal kMinTravel = qMax(2.0, qMax(iw, ih) * 0.015);
            if (travel < kMinTravel) {
                const qreal longSide = preferX ? iw : ih;
                const qreal targetHalf = qMax(kMinTravel, longSide * 0.10);
                const qreal neededView = preferX ? (iw - 2.0 * targetHalf)
                                                 : (ih - 2.0 * targetHalf);
                if (neededView > 1.0) {
                    s = preferX ? (qreal(vw) / neededView) : (qreal(vh) / neededView);
                    s = qMax(s, baseScale);
                }
            }
        }
        scale = s;
        const qreal along = -1.0 + 2.0 * motionT;
        if (preferX) {
            biasX = along;
            biasY = 0.0;
        } else {
            biasX = 0.0;
            biasY = along;
        }
    } else if (motion == SlideshowMotion::PanZoom) {
        const qreal factor = clampPanZoomFactor(panZoomFactor);
        qreal motionBase = baseScale;
        {
            constexpr qreal kMinHalf = 32.0;
            for (int i = 0; i < 10; ++i) {
                const qreal hx = qMax(0.0, (iw - qreal(vw) / motionBase) * 0.5);
                const qreal hy = qMax(0.0, (ih - qreal(vh) / motionBase) * 0.5);
                if (hx >= kMinHalf || hy >= kMinHalf) {
                    break;
                }
                motionBase *= 1.08;
            }
        }
        const qreal s0 = motionBase;
        const qreal s1 = motionBase * factor;
        scale = s0 + (s1 - s0) * motionT;

        if (!dwellBiasValid && biasA == QPointF(-1.0, -1.0)
            && biasB == QPointF(1.0, 1.0)) {
            uint seed = pathForSeed.isEmpty() ? 1u : uint(qHash(pathForSeed));
            if (seed == 0) {
                seed = 1u;
            }
            const BiasPath geo = geometricBiasPath(seed);
            biasA = geo.a;
            biasB = geo.b;
        }
        // Linear path only: scale s0→s1, image-space pan off0→off1.
        // dest = viewportCentre − off×scale (no bias encoding, no overflow gate —
        // the old overflow>0 ? bias : 0 snap was a discontinuity when an axis
        // first gained crop room, often at a corner).
        const qreal half0x = qMax(0.0, (iw - qreal(vw) / s0) * 0.5);
        const qreal half0y = qMax(0.0, (ih - qreal(vh) / s0) * 0.5);
        const qreal half1x = qMax(0.0, (iw - qreal(vw) / s1) * 0.5);
        const qreal half1y = qMax(0.0, (ih - qreal(vh) / s1) * 0.5);
        const qreal offX = (biasA.x() * half0x)
            + (biasB.x() * half1x - biasA.x() * half0x) * motionT;
        const qreal offY = (biasA.y() * half0y)
            + (biasB.y() * half1y - biasA.y() * half0y) * motionT;
        const qreal dw = iw * scale;
        const qreal dh = ih * scale;
        destX = (qreal(vw) - dw) * 0.5 - offX * scale;
        destY = (qreal(vh) - dh) * 0.5 - offY * scale;
        destFromOffset = true;
    } else {
        scale = baseScale;
        biasX = 0.0;
        biasY = 0.0;
    }

    qreal dw = iw * scale;
    qreal dh = ih * scale;
    if (!destFromOffset) {
        const qreal overflowX = qMax(0.0, (dw - qreal(vw)) * 0.5);
        const qreal overflowY = qMax(0.0, (dh - qreal(vh)) * 0.5);
        destX = (qreal(vw) - dw) * 0.5 - biasX * overflowX;
        destY = (qreal(vh) - dh) * 0.5 - biasY * overflowY;
    }
    return QRectF(destX, destY, dw, dh);
}

BiasPath geometricBiasPath(uint seed)
{
    static const QPointF kBias[8] = {
        QPointF(-1.0, -1.0), QPointF(1.0, -1.0),
        QPointF(-1.0, 1.0), QPointF(1.0, 1.0),
        QPointF(-1.0, 0.0), QPointF(1.0, 0.0),
        QPointF(0.0, -1.0), QPointF(0.0, 1.0),
    };
    BiasPath path;
    path.a = kBias[seed % 8];
    path.b = kBias[(seed / 8 + 3) % 8];
    if (qFuzzyCompare(path.a.x(), path.b.x()) && qFuzzyCompare(path.a.y(), path.b.y())) {
        path.b = kBias[(seed + 5) % 8];
    }
    if (qAbs(path.a.x() - path.b.x()) < 0.1 && qAbs(path.a.y() - path.b.y()) < 0.1) {
        path.b = kBias[(seed + 7) % 8];
    }
    path.travelDir = path.b - path.a;
    path.motionSign = (path.travelDir.y() >= 0.0) ? 1.0 : -1.0;
    return path;
}

bool attentionBiasPath(const QPointF &att01, uint seed, BiasPath *out)
{
    if (!out) {
        return false;
    }
    // Map normalized focus to bias space [-1, 1] (same as corner table).
    QPointF subject((att01.x() - 0.5) * 2.0, (att01.y() - 0.5) * 2.0);
    subject = clampBiasPoint(subject);
    // Near-centre attention still needs travel — fall through to geometry.
    if (qAbs(subject.x()) <= 0.12 && qAbs(subject.y()) <= 0.12) {
        return false;
    }
    // Subject must sit mid-path: start/end of the dwell are largely
    // hidden by the transition, so endpoint focus is invisible.
    // Travel along the subject↔opposite axis, centred on the subject.
    const QPointF travel = QPointF(subject.x() * 0.55, subject.y() * 0.55);
    BiasPath path;
    if (seed & 1u) {
        path.a = subject - travel;
        path.b = subject + travel;
    } else {
        path.a = subject + travel;
        path.b = subject - travel;
    }
    path.a = clampBiasPoint(path.a);
    path.b = clampBiasPoint(path.b);
    path.travelDir = path.b - path.a;
    path.motionSign = (path.travelDir.y() >= 0.0) ? 1.0 : -1.0;
    *out = path;
    return true;
}

bool aspectMismatch(qreal atlasW, qreal atlasH, qreal imageW, qreal imageH,
                    qreal threshold)
{
    if (atlasW <= 0.0 || atlasH <= 0.0 || imageW <= 0.0 || imageH <= 0.0) {
        return false;
    }
    const qreal aAsp = atlasW / atlasH;
    const qreal iAsp = imageW / imageH;
    return qAbs(aAsp - iAsp) > threshold;
}

} // namespace SlideshowMotionGeometry
