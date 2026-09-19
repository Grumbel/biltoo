// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoomblurhelpers.h"
#include "viewtransform.h"

#include <QHash>
#include <QPainter>
#include <QtMath>
#include <cmath>

namespace ZoomBlur {

qint64 key(const QString &path, int vw, int vh)
{
    if (path.isEmpty() || vw < 1 || vh < 1) {
        return 0;
    }
    return qint64(qHash(path)) ^ (qint64(vw) << 16) ^ qint64(vh);
}

bool keyCached(const SlideshowZoomBlurState &st, qint64 key)
{
    for (int i = 0; i < 2; ++i) {
        if (st.sourceKey[i] == key && !st.underlay[i].isNull()) {
            return true;
        }
    }
    return false;
}

bool keyInFlight(const SlideshowZoomBlurState &st, qint64 key)
{
    for (int i = 0; i < 2; ++i) {
        if (st.inFlightGen[i] == st.generation && st.inFlightKey[i] == key) {
            return true;
        }
    }
    return false;
}

int claimFlightSlot(SlideshowZoomBlurState *st, qint64 key)
{
    if (!st) {
        return -1;
    }
    int flightSlot = -1;
    for (int i = 0; i < 2; ++i) {
        if (st->inFlightKey[i] == 0 || st->inFlightGen[i] != st->generation) {
            flightSlot = i;
            break;
        }
    }
    if (flightSlot < 0) {
        return -1;
    }
    st->inFlightGen[flightSlot] = st->generation;
    st->inFlightKey[flightSlot] = key;
    return flightSlot;
}

void pruneOutsidePair(SlideshowZoomBlurState *st, qint64 keepA, qint64 keepB)
{
    if (!st) {
        return;
    }
    for (int i = 0; i < 2; ++i) {
        const qint64 k = st->sourceKey[i];
        if (k != 0 && k != keepA && k != keepB) {
            st->underlay[i] = QPixmap();
            st->sourceKey[i] = 0;
        }
        const qint64 fk = st->inFlightKey[i];
        if (fk != 0 && fk != keepA && fk != keepB
            && st->inFlightGen[i] == st->generation) {
            st->inFlightGen[i] = 0;
            st->inFlightKey[i] = 0;
        }
    }
}

void clearAllSlots(SlideshowZoomBlurState *st)
{
    if (!st) {
        return;
    }
    clearSizedSlots(st);
    st->lastGood = QPixmap();
    st->lastGoodKey = 0;
}

void clearSizedSlots(SlideshowZoomBlurState *st)
{
    if (!st) {
        return;
    }
    st->underlay[0] = QPixmap();
    st->underlay[1] = QPixmap();
    st->sourceKey[0] = 0;
    st->sourceKey[1] = 0;
}

void invalidateQueue(SlideshowZoomBlurState *st)
{
    if (!st) {
        return;
    }
    ++st->generation;
    st->inFlightGen[0] = st->inFlightGen[1] = 0;
    st->inFlightKey[0] = st->inFlightKey[1] = 0;
}

bool installResult(SlideshowZoomBlurState *st, const QPixmap &blurred, qint64 key,
                   quint64 gen)
{
    if (!st || gen != st->generation) {
        return false;
    }
    int slot = -1;
    for (int i = 0; i < 2; ++i) {
        if (st->sourceKey[i] == key) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = st->underlay[0].isNull() ? 0 : 1;
    }
    st->underlay[slot] = blurred;
    st->sourceKey[slot] = key;
    st->lastGood = blurred;
    st->lastGoodKey = key;
    for (int i = 0; i < 2; ++i) {
        if (st->inFlightKey[i] == key) {
            st->inFlightGen[i] = 0;
            st->inFlightKey[i] = 0;
        }
    }
    return true;
}

QImage makeCover(const QImage &src, int vw, int vh)
{
    if (src.isNull() || vw < 1 || vh < 1) {
        return {};
    }
    const qreal iw = qreal(src.width());
    const qreal ih = qreal(src.height());
    if (iw < 1.0 || ih < 1.0) {
        return {};
    }
    // ~1/16 of the viewport, hard-capped — soft background, not a second slide.
    constexpr int kMaxLongEdge = 128;
    int workW = qMax(8, vw / 16);
    int workH = qMax(8, vh / 16);
    const int longEdge = qMax(workW, workH);
    if (longEdge > kMaxLongEdge) {
        const qreal s = qreal(kMaxLongEdge) / qreal(longEdge);
        workW = qMax(8, int(workW * s));
        workH = qMax(8, int(workH * s));
    }
    const qreal cover = ViewTransform::coverScale(qreal(workW), qreal(workH), iw, ih);
    const int sw = ViewTransform::atLeast1(int(std::ceil(iw * cover)));
    const int sh = ViewTransform::atLeast1(int(std::ceil(ih * cover)));
    // FastTransformation: underlay is blurred anyway; Smooth is pure CPU cost.
    QImage scaled = src.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // Centre-crop to workW×workH
    const int x0 = qMax(0, (sw - workW) / 2);
    const int y0 = qMax(0, (sh - workH) / 2);
    scaled = scaled.copy(x0, y0, qMin(workW, scaled.width()), qMin(workH, scaled.height()));
    if (scaled.width() != workW || scaled.height() != workH) {
        QImage canvas(workW, workH, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::black);
        QPainter p(&canvas);
        p.drawImage((workW - scaled.width()) / 2, (workH - scaled.height()) / 2, scaled);
        p.end();
        scaled = canvas;
    }

    auto boxBlurPass = [](QImage &img, int radius, bool horizontal) {
        if (radius < 1) {
            return;
        }
        const int w = img.width();
        const int h = img.height();
        QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
        const int diam = radius * 2 + 1;
        if (horizontal) {
            for (int y = 0; y < h; ++y) {
                const QRgb *inLine = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
                int rSum = 0, gSum = 0, bSum = 0, aSum = 0;
                for (int i = -radius; i <= radius; ++i) {
                    const QRgb px = inLine[ViewTransform::clampPixel(i, w)];
                    rSum += qRed(px); gSum += qGreen(px); bSum += qBlue(px); aSum += qAlpha(px);
                }
                outLine[0] = qRgba(rSum / diam, gSum / diam, bSum / diam, aSum / diam);
                for (int x = 1; x < w; ++x) {
                    const QRgb leave = inLine[ViewTransform::clampPixel(x - radius - 1, w)];
                    const QRgb enter = inLine[ViewTransform::clampPixel(x + radius, w)];
                    rSum += qRed(enter) - qRed(leave);
                    gSum += qGreen(enter) - qGreen(leave);
                    bSum += qBlue(enter) - qBlue(leave);
                    aSum += qAlpha(enter) - qAlpha(leave);
                    outLine[x] = qRgba(rSum / diam, gSum / diam, bSum / diam, aSum / diam);
                }
            }
        } else {
            // Vertical: column-wise sliding window
            QVector<int> rSum(w), gSum(w), bSum(w), aSum(w);
            rSum.fill(0); gSum.fill(0); bSum.fill(0); aSum.fill(0);
            for (int i = -radius; i <= radius; ++i) {
                const int yy = ViewTransform::clampPixel(i, h);
                const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(yy));
                for (int x = 0; x < w; ++x) {
                    const QRgb px = line[x];
                    rSum[x] += qRed(px); gSum[x] += qGreen(px);
                    bSum[x] += qBlue(px); aSum[x] += qAlpha(px);
                }
            }
            QRgb *out0 = reinterpret_cast<QRgb *>(out.scanLine(0));
            for (int x = 0; x < w; ++x) {
                out0[x] = qRgba(rSum[x] / diam, gSum[x] / diam, bSum[x] / diam, aSum[x] / diam);
            }
            for (int y = 1; y < h; ++y) {
                const int leaveY = ViewTransform::clampPixel(y - radius - 1, h);
                const int enterY = ViewTransform::clampPixel(y + radius, h);
                const QRgb *leaveLine = reinterpret_cast<const QRgb *>(img.constScanLine(leaveY));
                const QRgb *enterLine = reinterpret_cast<const QRgb *>(img.constScanLine(enterY));
                QRgb *outLine = reinterpret_cast<QRgb *>(out.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const QRgb leave = leaveLine[x];
                    const QRgb enter = enterLine[x];
                    rSum[x] += qRed(enter) - qRed(leave);
                    gSum[x] += qGreen(enter) - qGreen(leave);
                    bSum[x] += qBlue(enter) - qBlue(leave);
                    aSum[x] += qAlpha(enter) - qAlpha(leave);
                    outLine[x] = qRgba(rSum[x] / diam, gSum[x] / diam, bSum[x] / diam, aSum[x] / diam);
                }
            }
        }
        img = out;
    };

    // Two box passes, modest radius — strong enough once stretched to the
    // viewport; cheaper first-build on the GUI thread.
    constexpr int kRadius = 6;
    for (int pass = 0; pass < 2; ++pass) {
        boxBlurPass(scaled, kRadius, true);
        boxBlurPass(scaled, kRadius, false);
    }
    Q_UNUSED(vw);
    Q_UNUSED(vh);
    return scaled;
}

} // namespace ZoomBlur
