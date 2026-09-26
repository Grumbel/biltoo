// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CropPanelRecipe / CropRecipeUtil — pure geometry for batch crop panel.
 */

#include "crop/croprecipe.h"

#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>

class CropRecipeTest : public QObject
{
    Q_OBJECT
private slots:
    void identity_isIdentity();
    void manual_zeroMargins_notUsableFullFrame();
    void manual_equalInsets();
    void manual_asymmetricInsets();
    void manual_marginsClampWhenTooLarge();
    void manual_invalidLogicalSize();
    void isUsable_rejectsOutsideAndEmpty();
    void extraMargins_expandThenClamp();
    void autocrop_nullSample_returnsEmpty();
    void autocrop_uniformImage_noContentTrim();
    void autocrop_centerBlob_trimsBorder();
    void suggest_emptyBands_notOk();
    void suggest_headerFooter_insets();
    void suggest_clampsExtremeInsets();
};

void CropRecipeTest::identity_isIdentity()
{
    CropPanelRecipe r;
    QVERIFY(r.isIdentity());
    r.marginLeft = 1;
    QVERIFY(!r.isIdentity());
}

void CropRecipeTest::manual_zeroMargins_notUsableFullFrame()
{
    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::ManualMargins;
    const QSize page(200, 100);
    const QRect crop = CropRecipeUtil::computeCropRect(r, page, QImage());
    // Full frame is not a usable crop — identity.
    QVERIFY(crop.isEmpty());
    QVERIFY(!CropRecipeUtil::isUsableCrop(QRect(QPoint(0, 0), page), page));
}

void CropRecipeTest::manual_equalInsets()
{
    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::ManualMargins;
    r.marginLeft = r.marginRight = r.marginTop = r.marginBottom = 10;
    const QSize page(200, 100);
    const QRect crop = CropRecipeUtil::computeCropRect(r, page, QImage());
    QCOMPARE(crop, QRect(10, 10, 180, 80));
    QVERIFY(CropRecipeUtil::isUsableCrop(crop, page));
}

void CropRecipeTest::manual_asymmetricInsets()
{
    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::ManualMargins;
    r.marginLeft = 5;
    r.marginTop = 10;
    r.marginRight = 15;
    r.marginBottom = 20;
    const QSize page(100, 80);
    const QRect crop = CropRecipeUtil::computeCropRect(r, page, QImage());
    QCOMPARE(crop, QRect(5, 10, 80, 50));
}

void CropRecipeTest::manual_marginsClampWhenTooLarge()
{
    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::ManualMargins;
    r.marginLeft = 1000;
    r.marginRight = 1000;
    const QSize page(50, 50);
    const QRect crop = CropRecipeUtil::computeCropRect(r, page, QImage());
    // Over-inset collapses to empty (no positive width).
    QVERIFY(crop.isEmpty());
}

void CropRecipeTest::manual_invalidLogicalSize()
{
    CropPanelRecipe r;
    r.marginLeft = 1;
    QVERIFY(CropRecipeUtil::computeCropRect(r, QSize(0, 10), QImage()).isEmpty());
    QVERIFY(CropRecipeUtil::computeCropRect(r, QSize(10, 0), QImage()).isEmpty());
}

void CropRecipeTest::isUsable_rejectsOutsideAndEmpty()
{
    const QSize page(100, 100);
    QVERIFY(!CropRecipeUtil::isUsableCrop(QRect(), page));
    QVERIFY(!CropRecipeUtil::isUsableCrop(QRect(0, 0, 0, 10), page));
    QVERIFY(!CropRecipeUtil::isUsableCrop(QRect(-1, 0, 10, 10), page));
    QVERIFY(!CropRecipeUtil::isUsableCrop(QRect(0, 0, 100, 100), page)); // full frame
    QVERIFY(CropRecipeUtil::isUsableCrop(QRect(1, 1, 50, 50), page));
}

void CropRecipeTest::extraMargins_expandThenClamp()
{
    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::ManualMargins;
    r.marginLeft = r.marginTop = r.marginRight = r.marginBottom = 20;
    r.extraLeft = r.extraTop = r.extraRight = r.extraBottom = 5;
    const QSize page(100, 100);
    // Core would be (20,20,60,60); expand by 5 → (15,15,70,70)
    const QRect crop = CropRecipeUtil::computeCropRect(r, page, QImage());
    QCOMPARE(crop, QRect(15, 15, 70, 70));
}

void CropRecipeTest::autocrop_nullSample_returnsEmpty()
{
    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::Autocrop;
    QVERIFY(CropRecipeUtil::computeCropRect(r, QSize(100, 100), QImage()).isEmpty());
}

void CropRecipeTest::autocrop_uniformImage_noContentTrim()
{
    // Solid image: autoTrim may fail or yield full frame → empty usable crop.
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::white);
    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::Autocrop;
    r.autocropThreshold = 24;
    const QRect crop = CropRecipeUtil::computeCropRect(r, img.size(), img);
    // Either empty (no content edge) or full-frame rejected — both OK as empty.
    QVERIFY(crop.isEmpty() || crop == QRect(QPoint(0, 0), img.size()));
    if (!crop.isEmpty()) {
        QVERIFY(!CropRecipeUtil::isUsableCrop(crop, img.size()));
    }
}

void CropRecipeTest::autocrop_centerBlob_trimsBorder()
{
    QImage img(100, 100, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.fillRect(30, 30, 40, 40, Qt::black);
    p.end();

    CropPanelRecipe r;
    r.mode = CropPanelRecipe::Mode::Autocrop;
    r.autocropThreshold = 24;
    const QRect crop = CropRecipeUtil::computeCropRect(r, img.size(), img);
    // Expect a rect around the blob (autoTrim may pad slightly).
    QVERIFY(crop.isValid());
    QVERIFY(CropRecipeUtil::isUsableCrop(crop, img.size()));
    QVERIFY(crop.width() < 100);
    QVERIFY(crop.height() < 100);
    QVERIFY(crop.contains(QPoint(50, 50))); // center of blob
}

void CropRecipeTest::suggest_emptyBands_notOk()
{
    const CropRecipeUtil::SuggestedMargins s =
        CropRecipeUtil::suggestMarginsFromBandRegions(QSize(200, 300), {});
    QVERIFY(!s.ok);
}

void CropRecipeTest::suggest_headerFooter_insets()
{
    QVector<QRectF> bands;
    bands.append(QRectF(10, 5, 180, 20));   // header near top
    bands.append(QRectF(20, 270, 160, 20)); // footer near bottom
    const CropRecipeUtil::SuggestedMargins s =
        CropRecipeUtil::suggestMarginsFromBandRegions(QSize(200, 300), bands);
    QVERIFY(s.ok);
    QCOMPARE(s.left, 0);
    QCOMPARE(s.right, 0);
    QVERIFY(s.top >= 25);
    QVERIFY(s.bottom >= 20);
}

void CropRecipeTest::suggest_clampsExtremeInsets()
{
    QVector<QRectF> bands;
    // Huge "header" covering half the page — must clamp.
    bands.append(QRectF(0, 0, 200, 150));
    const CropRecipeUtil::SuggestedMargins s =
        CropRecipeUtil::suggestMarginsFromBandRegions(QSize(200, 300), bands);
    QVERIFY(s.ok);
    QVERIFY(s.top <= 300 * 3 / 10);
}

QTEST_MAIN(CropRecipeTest)
#include "croprecipe_test.moc"
