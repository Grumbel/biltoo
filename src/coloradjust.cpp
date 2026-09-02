// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "coloradjust.h"
#include <QtMath>
#include <algorithm>

static int clampByte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void rgbToHsv(int r, int g, int b, float *h, float *s, float *v)
{
    const float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
    const float maxc = std::max({rf, gf, bf});
    const float minc = std::min({rf, gf, bf});
    const float delta = maxc - minc;
    *v = maxc;
    *s = (maxc <= 0.f) ? 0.f : (delta / maxc);
    if (delta <= 1e-6f) { *h = 0.f; return; }
    if (maxc == rf) *h = 60.f * std::fmod((gf - bf) / delta, 6.f);
    else if (maxc == gf) *h = 60.f * ((bf - rf) / delta + 2.f);
    else *h = 60.f * ((rf - gf) / delta + 4.f);
    if (*h < 0.f) *h += 360.f;
}

static void hsvToRgb(float h, float s, float v, int *r, int *g, int *b)
{
    if (s <= 1e-6f) {
        const int c = clampByte(int(v * 255.f + 0.5f));
        *r = *g = *b = c;
        return;
    }
    h = std::fmod(h, 360.f);
    if (h < 0.f) h += 360.f;
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;
    float rf = 0, gf = 0, bf = 0;
    if (h < 60.f) { rf = c; gf = x; }
    else if (h < 120.f) { rf = x; gf = c; }
    else if (h < 180.f) { gf = c; bf = x; }
    else if (h < 240.f) { gf = x; bf = c; }
    else if (h < 300.f) { rf = x; bf = c; }
    else { rf = c; bf = x; }
    *r = clampByte(int((rf + m) * 255.f + 0.5f));
    *g = clampByte(int((gf + m) * 255.f + 0.5f));
    *b = clampByte(int((bf + m) * 255.f + 0.5f));
}

QImage applyColorAdjustments(const QImage &src, const ColorAdjustments &adj)
{
    if (src.isNull() || adj.isIdentity()) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const float brightness = adj.brightness / 100.f * 255.f;
    const float contrast = adj.contrast / 100.f;
    const float satMul = adj.saturation / 100.f;
    const float hueShift = float(adj.hue);
    const float gamma = float(qBound(0.10, adj.gamma, 3.0));
    const float invGamma = 1.f / gamma;
    uchar gammaLut[256];
    for (int i = 0; i < 256; ++i)
        gammaLut[i] = uchar(clampByte(int(std::pow(i / 255.f, invGamma) * 255.f + 0.5f)));
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            int r = qRed(px), g = qGreen(px), b = qBlue(px);
            const int a = qAlpha(px);
            auto tone = [&](int c) {
                return clampByte(int((c - 128.f) * contrast + 128.f + brightness + 0.5f));
            };
            r = tone(r); g = tone(g); b = tone(b);
            if (adj.hue != 0 || !qFuzzyCompare(satMul, 1.f)) {
                float hh, ss, vv;
                rgbToHsv(r, g, b, &hh, &ss, &vv);
                hh += hueShift;
                ss = qBound(0.f, ss * satMul, 1.f);
                hsvToRgb(hh, ss, vv, &r, &g, &b);
            }
            if (!qFuzzyCompare(gamma, 1.f)) {
                r = gammaLut[r]; g = gammaLut[g]; b = gammaLut[b];
            }
            line[x] = qRgba(r, g, b, a);
        }
    }
    return img;
}
