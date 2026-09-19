// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COLORADJUST_H
#define COLORADJUST_H

#include <QImage>
#include <QtGlobal>

struct ColorAdjustments {
    int brightness = 0;
    int contrast = 100;
    int saturation = 100;
    int hue = 0;
    double gamma = 1.0;
    /** Photographic negative: invert RGB after other grade ops. */
    bool invert = false;

    bool isIdentity() const
    {
        return brightness == 0 && contrast == 100 && saturation == 100
               && hue == 0 && qFuzzyCompare(gamma, 1.0) && !invert;
    }

    /** Grade ranges match AdjustmentsPanel slider limits. */
    static int clampBrightness(int v) { return qBound(-100, v, 100); }
    static int clampContrast(int v) { return qBound(0, v, 200); }
    static int clampSaturation(int v) { return qBound(0, v, 200); }
    static int clampHue(int v) { return qBound(-180, v, 180); }
    /** Display/process gamma (0.10…3.0). */
    static double clampGamma(double g) { return qBound(0.10, g, 3.0); }

    /** Durable / UI percent (100 = 1.0); range 10…300. */
    static int gammaToPercent(double g)
    {
        return qBound(10, qRound(clampGamma(g) * 100.0), 300);
    }

    static double gammaFromPercent(int percent)
    {
        return clampGamma(double(percent) / 100.0);
    }

    void clampInPlace()
    {
        brightness = clampBrightness(brightness);
        contrast = clampContrast(contrast);
        saturation = clampSaturation(saturation);
        hue = clampHue(hue);
        gamma = clampGamma(gamma);
    }
};

QImage applyColorAdjustments(const QImage &src, const ColorAdjustments &adj);

#endif
