// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"

#include <QtTest/QtTest>
#include <QtMath>

/**
 * Content transform + crop geometry contract tests.
 *
 * layoutSize / scaleToPreserveFootprint are the pure rules for crop Apply
 * (Workspace footprint, Image intrinsic). needsRematerialize drives whether
 * a soft full-frame install may replace a crop bake on re-enter crop.
 */
class ContentXformTest : public QObject
{
    Q_OBJECT
private slots:
    void normalizeTurns();
    void layoutSize_identity();
    void layoutSize_swapsOnOddTurns();
    void layoutSize_cropDefinesBox();
    void layoutSize_cropScaledFromSourceSize();
    void layoutSize_cropThenOddTurn();
    void layoutSize_cropWithEvenTurn();
    void layoutSize_emptyCropFallsBackToOriented();
    void layoutSize_invalidNative();
    void scaleToPreserveFootprint_preservesSceneSize();
    void scaleToPreserveFootprint_rejectsDegenerate();
    void needsRematerialize_cropToFullRequiresRematerialize();
    void needsRematerialize_upgradeAndXform();
    void equal_ignoresPlacement();
    void equal_detectsCrop();
    void fromState_roundTrip();
    void needsRematerialize_fullToCrop();
    void needsRematerialize_secondCropDifferentRect();
    void layoutSize_secondCropIsNotPriorCropBox();
    void scaleCropRect_nativeToSoft();
    void scaleCropRect_softToNative();
    void scaleCropRect_identity();
    void mapCropRect_oneStepMatchesTrueMatrix();
    void mapCropThrough_updatesSourceSizeAndRotation();
    void layoutSize_cropThenMappedRotate();
    void layoutSize_staleCropSourceOrientation();
    void mapCropRect_negativeTurnsEqualsPlusThree();
    void mapCropRect_fourTurnsIdentity();
    void layoutSize_freeCropRotationDoesNotChangeSize();
    void mapCropThrough_freeRotationAngleTracksContentTurn();
    void layoutSize_freeRotThenContentTurn();
};

/** Must match SessionAppearance::scaleCropRect (sessionappearance.cpp). */
static QRect scaleCropRectMirror(const QRect &crop, const QSize &recorded, const QSize &live)
{
    if (crop.isEmpty() || live.width() < 1 || live.height() < 1) {
        return {};
    }
    if (!recorded.isValid() || recorded.width() < 1 || recorded.height() < 1
        || recorded == live) {
        return crop;
    }
    return QRect(
        qRound(crop.x() * double(live.width()) / double(recorded.width())),
        qRound(crop.y() * double(live.height()) / double(recorded.height())),
        qMax(1, qRound(crop.width() * double(live.width()) / double(recorded.width()))),
        qMax(1, qRound(crop.height() * double(live.height()) / double(recorded.height()))));
}


void ContentXformTest::normalizeTurns()
{
    QCOMPARE(ContentXform::normalizeQuarterTurns(0), 0);
    QCOMPARE(ContentXform::normalizeQuarterTurns(1), 1);
    QCOMPARE(ContentXform::normalizeQuarterTurns(4), 0);
    QCOMPARE(ContentXform::normalizeQuarterTurns(-1), 3);
    QCOMPARE(ContentXform::normalizeQuarterTurns(5), 1);
}

void ContentXformTest::layoutSize_identity()
{
    const QSize native(4000, 3000);
    ContentXform::Value id;
    QCOMPARE(ContentXform::layoutSize(native, id), native);
}

void ContentXformTest::layoutSize_swapsOnOddTurns()
{
    const QSize native(4000, 3000);
    ContentXform::Value t1;
    t1.quarterTurns = 1;
    QCOMPARE(ContentXform::layoutSize(native, t1), QSize(3000, 4000));
    QVERIFY(ContentXform::swapsAspect(t1));

    ContentXform::Value t2;
    t2.quarterTurns = 2;
    QCOMPARE(ContentXform::layoutSize(native, t2), native);

    ContentXform::Value t3;
    t3.quarterTurns = 3;
    QCOMPARE(ContentXform::layoutSize(native, t3), QSize(3000, 4000));
}

void ContentXformTest::layoutSize_cropDefinesBox()
{
    const QSize native(4000, 3000);
    ContentXform::Value c;
    c.hasCrop = true;
    c.cropRect = QRect(100, 50, 800, 600);
    c.cropSourceSize = native;
    QCOMPARE(ContentXform::layoutSize(native, c), QSize(800, 600));
}

void ContentXformTest::layoutSize_cropScaledFromSourceSize()
{
    const QSize soft(400, 300);
    ContentXform::Value c;
    c.hasCrop = true;
    c.cropRect = QRect(1000, 500, 2000, 1500);
    c.cropSourceSize = QSize(4000, 3000);
    QCOMPARE(ContentXform::layoutSize(soft, c), QSize(200, 150));
}

void ContentXformTest::layoutSize_cropThenOddTurn()
{
    const QSize native(4000, 3000);
    ContentXform::Value c;
    c.quarterTurns = 1;
    c.hasCrop = true;
    c.cropRect = QRect(10, 20, 500, 700);
    c.cropSourceSize = QSize(3000, 4000);
    QCOMPARE(ContentXform::layoutSize(native, c), QSize(500, 700));
}

void ContentXformTest::layoutSize_cropWithEvenTurn()
{
    const QSize native(4000, 3000);
    ContentXform::Value c;
    c.quarterTurns = 2;
    c.hasCrop = true;
    c.cropRect = QRect(0, 0, 1000, 800);
    c.cropSourceSize = native;
    QCOMPARE(ContentXform::layoutSize(native, c), QSize(1000, 800));
}

void ContentXformTest::layoutSize_emptyCropFallsBackToOriented()
{
    const QSize native(4000, 3000);
    ContentXform::Value c;
    c.hasCrop = true;
    c.cropRect = QRect();
    c.quarterTurns = 1;
    QCOMPARE(ContentXform::layoutSize(native, c), QSize(3000, 4000));
}

void ContentXformTest::layoutSize_invalidNative()
{
    ContentXform::Value c;
    c.hasCrop = true;
    c.cropRect = QRect(0, 0, 10, 10);
    QCOMPARE(ContentXform::layoutSize(QSize(), c), QSize());
}

void ContentXformTest::scaleToPreserveFootprint_preservesSceneSize()
{
    // Draft selection 256×192 scene units; logical crop 2000×1500 file pixels.
    const QSize logical(2000, 1500);
    const QSizeF s = ContentXform::scaleToPreserveFootprint(256.0, 192.0, logical);
    QCOMPARE(s.width() * logical.width(), 256.0);
    QCOMPARE(s.height() * logical.height(), 192.0);
    // Must not collapse to near-zero (Workspace “tiny tile” failure mode).
    QVERIFY(s.width() > 0.01);
    QVERIFY(s.height() > 0.01);
}

void ContentXformTest::scaleToPreserveFootprint_rejectsDegenerate()
{
    const QSizeF a = ContentXform::scaleToPreserveFootprint(0.0, 100.0, QSize(100, 100));
    QCOMPARE(a, QSizeF(1.0, 1.0));
    const QSizeF b = ContentXform::scaleToPreserveFootprint(100.0, 100.0, QSize());
    QCOMPARE(b, QSizeF(1.0, 1.0));
}

void ContentXformTest::needsRematerialize_cropToFullRequiresRematerialize()
{
    // Re-enter crop: applied has crop, want is orient-only full frame.
    ContentXform::Value applied;
    applied.hasCrop = true;
    applied.cropRect = QRect(0, 0, 100, 100);
    applied.cropSourceSize = QSize(1000, 1000);

    ContentXform::Value want; // identity / no crop
    QVERIFY(ContentXform::needsRematerialize(applied, want, 256, 512));
    QVERIFY(ContentXform::needsRematerialize(applied, want, 512, 512));
}

void ContentXformTest::needsRematerialize_upgradeAndXform()
{
    ContentXform::Value id;
    ContentXform::Value t1;
    t1.quarterTurns = 1;

    QVERIFY(ContentXform::needsRematerialize(id, id, 0, 128));
    QVERIFY(!ContentXform::needsRematerialize(id, id, 512, 512));
    QVERIFY(!ContentXform::needsRematerialize(id, id, 512, 256));
    QVERIFY(ContentXform::needsRematerialize(id, id, 128, 512));
    QVERIFY(ContentXform::needsRematerialize(id, t1, 512, 512));
}

void ContentXformTest::equal_ignoresPlacement()
{
    WorkspaceItemState a;
    a.path = QStringLiteral("/a");
    a.pos = QPointF(10, 20);
    a.scale = 2.0;
    a.contentQuarterTurns = 1;
    a.contentHFlip = true;

    WorkspaceItemState b = a;
    b.path = QStringLiteral("/b");
    b.pos = QPointF(0, 0);
    b.scale = 1.0;

    QVERIFY(ContentXform::equal(ContentXform::Value::fromState(a),
                                ContentXform::Value::fromState(b)));
    b.contentVFlip = true;
    QVERIFY(!ContentXform::equal(ContentXform::Value::fromState(a),
                                 ContentXform::Value::fromState(b)));
}

void ContentXformTest::equal_detectsCrop()
{
    ContentXform::Value a;
    a.hasCrop = true;
    a.cropRect = QRect(0, 0, 100, 100);
    a.cropSourceSize = QSize(1000, 1000);

    ContentXform::Value b = a;
    QVERIFY(ContentXform::equal(a, b));
    b.cropRect = QRect(0, 0, 50, 50);
    QVERIFY(!ContentXform::equal(a, b));
}

void ContentXformTest::fromState_roundTrip()
{
    WorkspaceItemState s;
    s.contentQuarterTurns = 5;
    s.contentHFlip = true;
    s.hasCrop = true;
    s.cropRect = QRect(1, 2, 3, 4);
    s.cropSourceSize = QSize(100, 200);
    s.cropRotation = 12.5;
    s.colorAdjust.brightness = 10;

    ContentXform::Value v = ContentXform::Value::fromState(s);
    QCOMPARE(v.quarterTurns, 1);
    WorkspaceItemState out;
    v.applyToState(out);
    QCOMPARE(out.contentQuarterTurns, 1);
    QCOMPARE(out.hasCrop, true);
    QCOMPARE(out.cropRect, QRect(1, 2, 3, 4));
}


void ContentXformTest::needsRematerialize_fullToCrop()
{
    ContentXform::Value full;
    ContentXform::Value cropped;
    cropped.hasCrop = true;
    cropped.cropRect = QRect(10, 10, 100, 80);
    cropped.cropSourceSize = QSize(400, 300);
    // Soft full on screen, want crop → must rematerialize.
    QVERIFY(ContentXform::needsRematerialize(full, cropped, 256, 256));
}

void ContentXformTest::needsRematerialize_secondCropDifferentRect()
{
    ContentXform::Value a;
    a.hasCrop = true;
    a.cropRect = QRect(0, 0, 200, 150);
    a.cropSourceSize = QSize(4000, 3000);
    ContentXform::Value b = a;
    b.cropRect = QRect(50, 50, 200, 150);
    QVERIFY(!ContentXform::equal(a, b));
    QVERIFY(ContentXform::needsRematerialize(a, b, 200, 200));
}

void ContentXformTest::layoutSize_secondCropIsNotPriorCropBox()
{
    // After first crop, layout is the crop box. Re-enter crop uses orient-only
    // want (hasCrop false) → full oriented size, never the prior crop box.
    const QSize native(4000, 3000);
    ContentXform::Value cropped;
    cropped.hasCrop = true;
    cropped.cropRect = QRect(100, 100, 500, 400);
    cropped.cropSourceSize = native;
    QCOMPARE(ContentXform::layoutSize(native, cropped), QSize(500, 400));

    ContentXform::Value orientOnly;
    orientOnly.quarterTurns = 0;
    QCOMPARE(ContentXform::layoutSize(native, orientOnly), native);
}


void ContentXformTest::scaleCropRect_nativeToSoft()
{
    // Crop recorded on native 4000×3000; soft sample 400×300 (10×).
    const QRect crop(1000, 500, 800, 600);
    const QRect soft = scaleCropRectMirror(crop, QSize(4000, 3000), QSize(400, 300));
    QCOMPARE(soft, QRect(100, 50, 80, 60));
}

void ContentXformTest::scaleCropRect_softToNative()
{
    const QRect softCrop(100, 50, 80, 60);
    const QRect native = scaleCropRectMirror(softCrop, QSize(400, 300), QSize(4000, 3000));
    QCOMPARE(native, QRect(1000, 500, 800, 600));
}

void ContentXformTest::scaleCropRect_identity()
{
    const QRect crop(10, 20, 100, 80);
    QCOMPARE(scaleCropRectMirror(crop, QSize(400, 300), QSize(400, 300)), crop);
}

void ContentXformTest::mapCropRect_oneStepMatchesTrueMatrix()
{
    // QImage::trueMatrix(rotate(90), W, H): (x,y,w,h) → (H-y-h, x, h, w).
    QSize space(4000, 3000);
    const QRect crop(100, 200, 800, 600);
    const QRect mapped =
        ContentXform::mapCropRectThroughContentRotate90(crop, space, 1);
    QCOMPARE(space, QSize(3000, 4000));
    QCOMPARE(mapped, QRect(3000 - 200 - 600, 100, 600, 800)); // (2200, 100, 600, 800)
    QCOMPARE(mapped.size(), QSize(600, 800));
}

void ContentXformTest::mapCropThrough_updatesSourceSizeAndRotation()
{
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(0, 0, 2000, 1000);
    x.cropSourceSize = QSize(4000, 3000);
    x.cropRotation = 0.0;
    ContentXform::mapCropThroughContentRotate90(x, 1);
    QCOMPARE(x.cropSourceSize, QSize(3000, 4000));
    QCOMPARE(x.cropRect.size(), QSize(1000, 2000));
    QCOMPARE(x.cropRotation, -90.0);

    ContentXform::mapCropThroughContentRotate90(x, 1);
    QCOMPARE(x.cropSourceSize, QSize(4000, 3000));
    QCOMPARE(x.cropRect.size(), QSize(2000, 1000));
    QCOMPARE(x.cropRotation, -180.0);
}

void ContentXformTest::layoutSize_cropThenMappedRotate()
{
    // Correct path: crop at turns=0, map crop through +1, then layoutSize.
    const QSize native(4000, 3000);
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(100, 200, 800, 600);
    x.cropSourceSize = native;
    ContentXform::mapCropThroughContentRotate90(x, 1);
    x.quarterTurns = 1;
    QCOMPARE(ContentXform::layoutSize(native, x), QSize(600, 800));
}

void ContentXformTest::layoutSize_staleCropSourceOrientation()
{
    // Bug path: turns=1 but crop still recorded against unoriented native
    // (mapCrop was skipped). layoutSize must not linear-scale — that only
    // yields the correct box size when crop aspect == full aspect.
    const QSize native(4000, 3000);
    ContentXform::Value x;
    x.quarterTurns = 1;
    x.hasCrop = true;
    // Non-matching aspect crop (2:1 on a 4:3 frame).
    x.cropRect = QRect(0, 0, 2000, 1000);
    x.cropSourceSize = native; // stale: should have been 3000×4000 after +1
    // After proper map: size becomes 1000×2000. Linear scale would give
    // 2000*(3000/4000)×1000*(4000/3000) = 1500×1333 — wrong.
    QCOMPARE(ContentXform::layoutSize(native, x), QSize(1000, 2000));
}

void ContentXformTest::mapCropRect_negativeTurnsEqualsPlusThree()
{
    QSize a(4000, 3000);
    QSize b(4000, 3000);
    const QRect crop(100, 200, 800, 600);
    const QRect mNeg = ContentXform::mapCropRectThroughContentRotate90(crop, a, -1);
    const QRect mPos = ContentXform::mapCropRectThroughContentRotate90(crop, b, 3);
    QCOMPARE(a, b);
    QCOMPARE(mNeg, mPos);
    QCOMPARE(mNeg.size(), QSize(600, 800));
}

void ContentXformTest::mapCropRect_fourTurnsIdentity()
{
    QSize space(4000, 3000);
    const QRect crop(100, 200, 800, 600);
    const QRect mapped =
        ContentXform::mapCropRectThroughContentRotate90(crop, space, 4);
    QCOMPARE(space, QSize(4000, 3000));
    QCOMPARE(mapped, crop);
}

void ContentXformTest::layoutSize_freeCropRotationDoesNotChangeSize()
{
    // Free cropRotation samples a rotated window into an axis-aligned output
    // of size cropRect (materializeDisplay freeRot path). layoutSize is the
    // output box — independent of cropRotation angle.
    const QSize native(4000, 3000);
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(100, 200, 800, 600);
    x.cropSourceSize = native;
    x.cropRotation = 0.0;
    QCOMPARE(ContentXform::layoutSize(native, x), QSize(800, 600));

    x.cropRotation = 37.5;
    QCOMPARE(ContentXform::layoutSize(native, x), QSize(800, 600));

    x.cropRotation = -15.0;
    x.quarterTurns = 1;
    // Stale orient: map path yields 600×800; free angle still ignored for size.
    ContentXform::Value y = x;
    y.cropSourceSize = QSize(3000, 4000); // already post-orient for turns=1
    y.cropRect = QRect(2200, 100, 600, 800);
    QCOMPARE(ContentXform::layoutSize(native, y), QSize(600, 800));
}

void ContentXformTest::mapCropThrough_freeRotationAngleTracksContentTurn()
{
    // Content ±90° must keep the free crop window relative to the pixels:
    // AABB maps through trueMatrix; cropRotation shifts by −90° per step.
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(500, 400, 1200, 900);
    x.cropSourceSize = QSize(4000, 3000);
    x.cropRotation = 15.0;
    ContentXform::mapCropThroughContentRotate90(x, 1);
    QCOMPARE(x.cropRotation, -75.0);
    QCOMPARE(x.cropSourceSize, QSize(3000, 4000));
    QCOMPARE(x.cropRect.size(), QSize(900, 1200));

    ContentXform::mapCropThroughContentRotate90(x, -1); // undo
    QCOMPARE(x.cropRotation, 15.0);
    QCOMPARE(x.cropSourceSize, QSize(4000, 3000));
    QCOMPARE(x.cropRect.size(), QSize(1200, 900));
    QCOMPARE(x.cropRect, QRect(500, 400, 1200, 900));
}

void ContentXformTest::layoutSize_freeRotThenContentTurn()
{
    // Apply path: free-rot crop recorded, then content +90° with map.
    const QSize native(4000, 3000);
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(100, 200, 800, 600);
    x.cropSourceSize = native;
    x.cropRotation = 22.5;
    ContentXform::mapCropThroughContentRotate90(x, 1);
    x.quarterTurns = 1;
    QCOMPARE(x.cropRotation, 22.5 - 90.0);
    QCOMPARE(ContentXform::layoutSize(native, x), QSize(600, 800));
}

QTEST_MAIN(ContentXformTest)
#include "contentxform_test.moc"


