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

    bool isIdentity() const
    {
        return brightness == 0 && contrast == 100 && saturation == 100
               && hue == 0 && qFuzzyCompare(gamma, 1.0);
    }
};

QImage applyColorAdjustments(const QImage &src, const ColorAdjustments &adj);

#endif
