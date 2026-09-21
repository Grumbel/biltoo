// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Stage 3 pure helpers: GalleryLayout axes swap + layoutSizeForNative.
 * No ImageItem / QGraphicsView — pack geometry only.
 */

#include "gallerylayout.h"

#include <QtTest/QtTest>

class GalleryLayoutTest : public QObject
{
    Q_OBJECT
private slots:
    void axesSwap_cardinalAndDiagonal();
    void layoutSizeForNative_swapAndEmpty();
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
    // Negative and >360 normalize via abs + fmod.
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

QTEST_MAIN(GalleryLayoutTest)
#include "gallerylayout_test.moc"
