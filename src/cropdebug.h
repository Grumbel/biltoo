// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPDEBUG_H
#define CROPDEBUG_H

#include <QString>
#include <QSize>
#include <QRect>
#include <QtGlobal>
#include <QDebug>

/**
 * BILTOO_DEBUG_CROP logging. ImageView gathers item fields; formatting lives here.
 */
namespace CropDebug {

inline bool enabled()
{
    return qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP");
}

inline void keepEnterDisplay(int displayEdge, const QString &path)
{
    if (!enabled()) {
        return;
    }
    qWarning().noquote()
        << QStringLiteral("[crop] enter-full KEEP display edge=%1 path=%2")
               .arg(displayEdge)
               .arg(path);
}

inline void draftEnterBegin(const QString &path, int imageW, int imageH, bool hasDecoded,
                            bool hadPriorCrop, int hostEdge)
{
    if (!enabled()) {
        return;
    }
    qWarning().noquote()
        << QStringLiteral(
               "[crop] enter-full path=%1 imageSize=%2x%3 hasDecoded=%4 "
               "appliedCrop=%5 hostEdge=%6")
               .arg(path)
               .arg(imageW)
               .arg(imageH)
               .arg(hasDecoded ? 1 : 0)
               .arg(hadPriorCrop ? 1 : 0)
               .arg(hostEdge);
}

inline void draftEnterDone(int imageW, int imageH, int displayW, int displayH,
                           bool appliedCrop, int contentTurns)
{
    if (!enabled()) {
        return;
    }
    qWarning().noquote()
        << QStringLiteral(
               "[crop] enter-full done imageSize=%1x%2 display=%3x%4 "
               "appliedCrop=%5 contentTurns=%6")
               .arg(imageW)
               .arg(imageH)
               .arg(displayW)
               .arg(displayH)
               .arg(appliedCrop ? 1 : 0)
               .arg(contentTurns);
}

inline void recordCrop(const QSize &cropBasis, const QSize &imageSize, const QRect &disp)
{
    if (!enabled() || disp.isEmpty() || cropBasis == imageSize) {
        return;
    }
    qWarning().noquote()
        << QStringLiteral(
               "[crop] record basis=%1x%2 imageSize=%3x%4 rect=%5x%6+%7x%8")
               .arg(cropBasis.width())
               .arg(cropBasis.height())
               .arg(imageSize.width())
               .arg(imageSize.height())
               .arg(disp.x())
               .arg(disp.y())
               .arg(disp.width())
               .arg(disp.height());
}

inline void applyCrop(const QString &path, int hostW, int hostH, bool hostFromCache,
                      int displayW, int displayH, qreal cropW, qreal cropH, qreal footW,
                      qreal footH, int imageW, int imageH)
{
    if (!enabled()) {
        return;
    }
    qWarning().noquote()
        << QStringLiteral(
               "[crop] Apply path=%1 host=%2x%3 cache=%4 display=%5x%6 "
               "cropDraft=%7x%8 foot=%9x%10 "
               "imageSizeBefore=%11x%12")
               .arg(path)
               .arg(hostW)
               .arg(hostH)
               .arg(hostFromCache ? 1 : 0)
               .arg(displayW)
               .arg(displayH)
               .arg(cropW)
               .arg(cropH)
               .arg(footW)
               .arg(footH)
               .arg(imageW)
               .arg(imageH);
}

} // namespace CropDebug

#endif // CROPDEBUG_H
