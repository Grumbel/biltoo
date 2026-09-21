// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Stage 3 pure helpers: GalleryLayout axes swap + layoutSizeForNative,
 * GalleryPackFit fitted targets and overshoot scale.
 * No ImageItem / QGraphicsView.
 */

#include "gallerylayout.h"
#include "gallerypackfit.h"

#include <QtTest/QtTest>

class GalleryLayoutTest : public QObject
{
    Q_OBJECT
private slots:
    void axesSwap_cardinalAndDiagonal();
    void layoutSizeForNative_swapAndEmpty();
    void packFit_fittedTargets_heightVsWidthModes();
    void packFit_overshootUniformScale();
    void packFit_modeFromLayoutMode();
};

void GalleryLayoutTest::axesSwap_cardinalAndDiagonal()
{
    QVERIFY(!GalleryLayout::axesSwapForItemRotation(0.0));
    QVERIFY(!GalleryLayout::axesSwapForItemRotation(45.0));
    QVERIFY(GalleryLayout::axesSwapForItemRotation(46.0));
    QVERIFY(GalleryLayout::axesSwapForItemRotation(90.0));
    QVERIFY(GalleryLayout::axesSwapForItemRotation(134.0));
    QVERIFY(!GalleryLayout::axesSwapForItemRotation(135.0));
    QVERIFY(!GalleryLayout::axesSwapForItemRotation(180.0));
    QVERIFY(GalleryLayout::axesSwapForItemRotation(-90.0));
    QVERIFY(GalleryLayout::axesSwapForItemRotation(450.0)); // 90
}

void GalleryLayoutTest::layoutSizeForNative_swapAndEmpty()
{
    QCOMPARE(GalleryLayout::layoutSizeForNative(QSizeF(), 90.0), QSizeF());
    QCOMPARE(GalleryLayout::layoutSizeForNative(QSizeF(200, 100), 0.0), QSizeF(200, 100));
    QCOMPARE(GalleryLayout::layoutSizeForNative(QSizeF(200, 100), 90.0), QSizeF(100, 200));
    QCOMPARE(GalleryLayout::layoutSizeForNative(QSizeF(200, 100), 180.0), QSizeF(200, 100));
    QCOMPARE(GalleryLayout::layoutSizeForNative(QSizeF(200, 100), -90.0), QSizeF(100, 200));
}

void GalleryLayoutTest::packFit_fittedTargets_heightVsWidthModes()
{
    qreal tw = 0.0;
    qreal th = 0.0;
    GalleryPackFit::fittedTargets(GalleryLayout::Mode::SideBySide, 800.0, 600.0, &tw, &th);
    QCOMPARE(tw, -1.0);
    QCOMPARE(th, 600.0);

    GalleryPackFit::fittedTargets(GalleryLayout::Mode::MasonryRows, 800.0, 600.0, &tw, &th);
    QCOMPARE(tw, -1.0);
    QCOMPARE(th, 600.0);

    GalleryPackFit::fittedTargets(GalleryLayout::Mode::Grid, 800.0, 600.0, &tw, &th);
    QCOMPARE(tw, 800.0);
    QCOMPARE(th, -1.0);

    GalleryPackFit::fittedTargets(GalleryLayout::Mode::Masonry, 800.0, 600.0, &tw, &th);
    QCOMPARE(tw, 800.0);
    QCOMPARE(th, -1.0);

    GalleryPackFit::fittedTargets(GalleryLayout::Mode::Flow, 800.0, 600.0, &tw, &th);
    QCOMPARE(tw, 800.0);
    QCOMPARE(th, -1.0);
}

void GalleryLayoutTest::packFit_overshootUniformScale()
{
    QCOMPARE(GalleryPackFit::overshootUniformScale(QRectF(), 100.0, 100.0), 1.0);
    // Fits exactly → no shrink.
    QCOMPARE(GalleryPackFit::overshootUniformScale(QRectF(0, 0, 100, 50), 100.0, -1.0), 1.0);
    // Width overshoot.
    const qreal sW = GalleryPackFit::overshootUniformScale(QRectF(0, 0, 110, 50), 100.0, -1.0);
    QVERIFY(sW < 1.0);
    QVERIFY(qAbs(sW - 100.0 / 110.0) < 1e-9);
    // Height overshoot (SideBySide-style).
    const qreal sH = GalleryPackFit::overshootUniformScale(QRectF(0, 0, 50, 120), -1.0, 100.0);
    QVERIFY(sH < 1.0);
    QVERIFY(qAbs(sH - 100.0 / 120.0) < 1e-9);
    // Within epsilon → no shrink.
    QCOMPARE(GalleryPackFit::overshootUniformScale(QRectF(0, 0, 100.00005, 50.0), 100.0, -1.0, 1e-4), 1.0);
}

void GalleryLayoutTest::packFit_modeFromLayoutMode()
{
    QCOMPARE(GalleryPackFit::modeFromLayoutMode(LayoutMode::Grid), GalleryLayout::Mode::Grid);
    QCOMPARE(GalleryPackFit::modeFromLayoutMode(LayoutMode::SideBySide),
             GalleryLayout::Mode::SideBySide);
    QCOMPARE(GalleryPackFit::modeFromLayoutMode(LayoutMode::FreeForm),
             GalleryLayout::Mode::Masonry);
    QCOMPARE(GalleryPackFit::modeFromLayoutMode(LayoutMode::Facing), GalleryLayout::Mode::Facing);
}

QTEST_MAIN(GalleryLayoutTest)
#include "gallerylayout_test.moc"
