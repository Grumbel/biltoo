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
    void packFit_scaledDisplaySizeAndCenteredBounds();
    void layout_resolvedColumnsAndAxisFill();
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

void GalleryLayoutTest::packFit_scaledDisplaySizeAndCenteredBounds()
{
    QCOMPARE(GalleryPackFit::scaledDisplaySize(QSizeF(), 2.0), QSizeF());
    QCOMPARE(GalleryPackFit::scaledDisplaySize(QSizeF(100, 50), 2.0), QSizeF(200, 100));
    QCOMPARE(GalleryPackFit::scaledDisplaySize(QSizeF(100, 50), 2.0, 3.0), QSizeF(200, 150));

    const QRectF b = GalleryPackFit::centeredTileBounds(QPointF(50, 40), QSizeF(20, 10));
    QCOMPARE(b, QRectF(40, 35, 20, 10));
    QVERIFY(GalleryPackFit::centeredTileBounds(QPointF(0, 0), QSizeF()).isEmpty());

    // Union of two tiles matches pack overshoot measurement shape.
    QRectF content;
    content = content.united(GalleryPackFit::centeredTileBounds(QPointF(10, 10), QSizeF(20, 20)));
    content = content.united(GalleryPackFit::centeredTileBounds(QPointF(40, 10), QSizeF(20, 20)));
    QCOMPARE(content, QRectF(0, 0, 50, 20));
}

void GalleryLayoutTest::layout_resolvedColumnsAndAxisFill()
{
    QCOMPARE(GalleryLayout::resolvedColumns(0, 0), 1);
    QCOMPARE(GalleryLayout::resolvedColumns(4, 0), 2); // ceil sqrt 4
    QCOMPARE(GalleryLayout::resolvedColumns(5, 0), 3);
    QCOMPARE(GalleryLayout::resolvedColumns(10, 4), 4);
    QCOMPARE(GalleryLayout::resolvedFlowColumns(0), 3);
    QCOMPARE(GalleryLayout::resolvedFlowColumns(5), 5);
    QCOMPARE(GalleryLayout::resolvedBandCount(0, 5), 1);
    QCOMPARE(GalleryLayout::resolvedBandCount(9, 5), 5);
    QCOMPARE(GalleryLayout::axisFillScale(100.0, 50.0), 2.0);
    QCOMPARE(GalleryLayout::axisFillScale(100.0, 0.0), 100.0); // native floored at 1
    QCOMPARE(GalleryLayout::cellAxisLength(100.0, 10.0, 3), (100.0 - 20.0) / 3.0);
}

QTEST_MAIN(GalleryLayoutTest)
#include "gallerylayout_test.moc"
