// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionappearance.h"
#include "contentxform.h"
#include "imageview_types.h"

#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>

/**
 * Crop geometry + materializeDisplay contracts used by crop Apply / filmstrip.
 *
 * - scaleCropRect maps stored crop between soft and native sample sizes
 * - materializeDisplay must return crop-sized output (not full frame) when
 *   hasCrop is set — filmstrip override uses that image
 */
class SessionAppearanceCropTest : public QObject
{
    Q_OBJECT
private slots:
    void scaleCropRect_identity();
    void scaleCropRect_nativeToSoft();
    void scaleCropRect_softToNative();
    void scaleCropRect_emptyRejected();
    void materializeDisplay_cropReducesSize();
    void materializeDisplay_noCropKeepsSize();
    void materializeDisplay_cropSourceSizeScales();
    void layoutSize_matchesMaterializeCropBox();
};

void SessionAppearanceCropTest::scaleCropRect_identity()
{
    const QRect crop(10, 20, 100, 80);
    const QSize sz(400, 300);
    QCOMPARE(SessionAppearance::scaleCropRect(crop, sz, sz), crop);
}

void SessionAppearanceCropTest::scaleCropRect_nativeToSoft()
{
    // Crop recorded on native 4000×3000; soft sample 400×300 (10×).
    const QRect crop(1000, 500, 800, 600);
    const QRect soft = SessionAppearance::scaleCropRect(
        crop, QSize(4000, 3000), QSize(400, 300));
    QCOMPARE(soft, QRect(100, 50, 80, 60));
}

void SessionAppearanceCropTest::scaleCropRect_softToNative()
{
    const QRect softCrop(100, 50, 80, 60);
    const QRect native = SessionAppearance::scaleCropRect(
        softCrop, QSize(400, 300), QSize(4000, 3000));
    QCOMPARE(native, QRect(1000, 500, 800, 600));
}

void SessionAppearanceCropTest::scaleCropRect_emptyRejected()
{
    QVERIFY(SessionAppearance::scaleCropRect(QRect(), QSize(100, 100), QSize(50, 50))
                .isEmpty());
    QVERIFY(SessionAppearance::scaleCropRect(QRect(0, 0, 10, 10), QSize(100, 100), QSize())
                .isEmpty());
}

static QImage solidImage(int w, int h, QRgb rgb)
{
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(QColor::fromRgba(rgb));
    return img;
}

void SessionAppearanceCropTest::materializeDisplay_cropReducesSize()
{
    const QImage raw = solidImage(400, 300, 0xffff0000);
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(50, 40, 120, 80);
    st.cropSourceSize = QSize(400, 300);
    const QImage out = SessionAppearance::materializeDisplay(
        raw, st, SessionAppearance::PixelKind::SoftPreview);
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), QSize(120, 80));
    // Must not remain full frame (filmstrip regression: cropApply img=full).
    QVERIFY(out.width() < raw.width() || out.height() < raw.height());
}

void SessionAppearanceCropTest::materializeDisplay_noCropKeepsSize()
{
    const QImage raw = solidImage(200, 150, 0xff00ff00);
    WorkspaceItemState st;
    const QImage out = SessionAppearance::materializeDisplay(
        raw, st, SessionAppearance::PixelKind::SoftPreview);
    QCOMPARE(out.size(), raw.size());
}

void SessionAppearanceCropTest::materializeDisplay_cropSourceSizeScales()
{
    // Crop stored in native space; sample is soft 10× smaller.
    const QImage raw = solidImage(400, 300, 0xff0000ff);
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(1000, 500, 800, 600); // native
    st.cropSourceSize = QSize(4000, 3000);
    const QImage out = SessionAppearance::materializeDisplay(
        raw, st, SessionAppearance::PixelKind::SoftPreview);
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), QSize(80, 60));
}

void SessionAppearanceCropTest::layoutSize_matchesMaterializeCropBox()
{
    const QSize native(4000, 3000);
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(100, 50, 800, 600);
    st.cropSourceSize = native;
    const QSize lay = ContentXform::layoutSize(native, st);
    QCOMPARE(lay, QSize(800, 600));

    const QImage raw = solidImage(400, 300, 0xffffffff);
    // Soft 10×: materialize output size should match scaled crop, not layoutSize
    // (layoutSize is file-native geometry; soft is a stand-in).
    st.cropRect = QRect(100, 50, 800, 600);
    st.cropSourceSize = native;
    const QImage out = SessionAppearance::materializeDisplay(
        raw, st, SessionAppearance::PixelKind::SoftPreview);
    QCOMPARE(out.size(), QSize(80, 60));
}

QTEST_MAIN(SessionAppearanceCropTest)
#include "sessionappearance_crop_test.moc"
