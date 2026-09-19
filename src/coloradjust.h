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

    bool matches(const ColorAdjustments &o) const
    {
        return brightness == o.brightness && contrast == o.contrast
            && saturation == o.saturation && hue == o.hue
            && qFuzzyCompare(gamma, o.gamma) && invert == o.invert;
    }

    /**
     * Build grade from durable XDG fields (percent gamma, 0 contrast/sat = identity 100).
     */
    static ColorAdjustments fromDurableGrade(int brightness, int contrast, int saturation,
                                             int hue, int gammaPercent, bool invertFlag)
    {
        ColorAdjustments a;
        a.brightness = brightness;
        a.contrast = contrast == 0 ? 100 : contrast;
        a.saturation = saturation == 0 ? 100 : saturation;
        a.hue = hue;
        a.gamma = gammaPercent <= 0 ? 1.0 : (gammaPercent / 100.0);
        a.invert = invertFlag;
        a.clampInPlace();
        return a;
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

    /**
     * Subsample stride for vectorscope / waveform so large images stay cheap.
     * Targets ~@p targetSamples along the long edge.
     */
    static int scopeSampleStep(int width, int height, int targetSamples = 120)
    {
        const int longEdge = qMax(1, qMax(width, height));
        return qMax(1, longEdge / qMax(1, targetSamples));
    }

    /** Float channel / saturation factor into [0, 1]. */
    static float clampUnit(float v)
    {
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    }
};

QImage applyColorAdjustments(const QImage &src, const ColorAdjustments &adj);

#endif
