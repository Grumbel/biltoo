// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"

#include <QtTest/QtTest>
#include <QtMath>
#include <QImage>
#include <QTransform>
#include <QColor>

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
    void mapSourceRect_identity();
    void mapSourceRect_turn90();
    void mapDisplayRect_cropRoundTrip();
    void mapDisplayRect_freeRotExpands();
    void sourceToDisplayTransform_matchesMapCorners();
    void sourceToDisplayTransform_hFlipMovesAndOrients();
    void layoutSize_freeCropRotationDoesNotChangeSize();
    void mapCropThrough_freeRotationAngleTracksContentTurn();
    void layoutSize_freeRotThenContentTurn();
    // --- Combinatorial rotate × flip × crop (bulletproof contracts) ---
    void combo_layoutSize_allTurnsWithAndWithoutCrop();
    void combo_mapCrop_axisAlignedRotationStaysZero_allTurns();
    void combo_mapCrop_roundTrip_plusK_minusK();
    void combo_mapCrop_fourTurnsIdentity_withFlipsIrrelevant();
    void combo_sequence_cropThenEachRotate();
    void combo_sequence_rotateThenCropInOrientedSpace();
    void combo_flipFlags_doNotChangeLayoutSize();
    void combo_pureMaterialize_sizeMatchesLayoutSize();
    void combo_pureMaterialize_cropPixelsSurviveAxisAlignedRotate();
    void softPreview_scaledCropAspectMatchesLayout();
    void combo_axisAligned_mapDoesNotArmFreeRotation();
    void combo_equal_detectsAllContentFields();
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


/**
 * Pure geometry mirror of SessionAppearance::materializeDisplay for
 * axis-aligned crops (no free cropRotation, no colour grade).
 * Order: flips → trueMatrix quarter turns → axis-aligned crop copy.
 */
static QImage pureMaterializeAxisAligned(const QImage &raw, const ContentXform::Value &x)
{
    QImage out = raw;
    if (out.isNull()) {
        return {};
    }
    if (x.hFlip || x.vFlip) {
        Qt::Orientations axes;
        if (x.hFlip) {
            axes |= Qt::Horizontal;
        }
        if (x.vFlip) {
            axes |= Qt::Vertical;
        }
        if (axes) {
            out = out.flipped(axes);
        }
    }
    const int turns = ContentXform::normalizeQuarterTurns(x.quarterTurns);
    if (turns != 0) {
        QTransform rot;
        rot.rotate(90.0 * turns);
        out = out.transformed(rot, Qt::FastTransformation);
    }
    if (x.hasCrop && !x.cropRect.isEmpty()) {
        // Free-rot path is intentionally not simulated here — axis-aligned only.
        if (qAbs(x.cropRotation) > 0.05) {
            return {}; // free-rot not simulated in this helper
        }
        const QSize live = out.size();
        QRect crop = x.cropRect.normalized();
        if (x.cropSourceSize.isValid() && x.cropSourceSize.width() > 0
            && x.cropSourceSize != live) {
            crop = scaleCropRectMirror(crop, x.cropSourceSize, live);
        }
        const QRect bounds(0, 0, out.width(), out.height());
        const QRect src = crop.intersected(bounds);
        if (src.width() >= 1 && src.height() >= 1) {
            out = out.copy(src);
        }
    }
    return out;
}

/** Encode a unique RGB fingerprint at (x,y) for pixel identity checks. */
static void stampPixel(QImage *img, int x, int y, int tag)
{
    Q_ASSERT(img);
    if (x < 0 || y < 0 || x >= img->width() || y >= img->height()) {
        return;
    }
    img->setPixelColor(x, y, QColor((tag * 17) & 255, (tag * 31) & 255, (tag * 47) & 255));
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
    // Axis-aligned: AABB maps; cropRotation stays 0 (not −90).
    QCOMPARE(x.cropRotation, 0.0);

    ContentXform::mapCropThroughContentRotate90(x, 1);
    QCOMPARE(x.cropSourceSize, QSize(4000, 3000));
    QCOMPARE(x.cropRect.size(), QSize(2000, 1000));
    QCOMPARE(x.cropRotation, 0.0);
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


void ContentXformTest::combo_layoutSize_allTurnsWithAndWithoutCrop()
{
    const QSize native(4000, 3000);
    const QRect crop(500, 400, 1200, 900);
    for (int turns = 0; turns < 4; ++turns) {
        ContentXform::Value full;
        full.quarterTurns = turns;
        const QSize oriented = ContentXform::swapsAspect(full)
                                   ? QSize(native.height(), native.width())
                                   : native;
        QCOMPARE(ContentXform::layoutSize(native, full), oriented);

        ContentXform::Value c;
        c.quarterTurns = turns;
        c.hasCrop = true;
        c.cropRect = crop;
        c.cropSourceSize = native;
        c.cropRotation = 0.0;
        if (turns != 0) {
            ContentXform::mapCropThroughContentRotate90(c, turns);
        }
        const QSize lay = ContentXform::layoutSize(native, c);
        QCOMPARE(lay, c.cropRect.size());
        QVERIFY(lay.width() > 1 && lay.height() > 1);
        if ((turns % 2) != 0) {
            QCOMPARE(lay, QSize(900, 1200));
        } else {
            QCOMPARE(lay, QSize(1200, 900));
        }
    }
}

void ContentXformTest::combo_mapCrop_axisAlignedRotationStaysZero_allTurns()
{
    for (int turns = 0; turns < 4; ++turns) {
        ContentXform::Value x;
        x.hasCrop = true;
        x.cropRect = QRect(100, 200, 800, 600);
        x.cropSourceSize = QSize(4000, 3000);
        x.cropRotation = 0.0;
        ContentXform::mapCropThroughContentRotate90(x, turns);
        QCOMPARE(x.cropRotation, 0.0);
        QVERIFY(x.cropSourceSize.width() > 0);
    }
}

void ContentXformTest::combo_mapCrop_roundTrip_plusK_minusK()
{
    const QRect orig(500, 400, 1200, 900);
    const QSize native(4000, 3000);
    for (int k = 1; k <= 3; ++k) {
        ContentXform::Value x;
        x.hasCrop = true;
        x.cropRect = orig;
        x.cropSourceSize = native;
        x.cropRotation = 0.0;
        ContentXform::mapCropThroughContentRotate90(x, k);
        ContentXform::mapCropThroughContentRotate90(x, -k);
        QCOMPARE(x.cropSourceSize, native);
        QCOMPARE(x.cropRect, orig);
        QCOMPARE(x.cropRotation, 0.0);

        ContentXform::Value f = x;
        f.cropRotation = 27.5;
        ContentXform::mapCropThroughContentRotate90(f, k);
        ContentXform::mapCropThroughContentRotate90(f, -k);
        QCOMPARE(f.cropRect, orig);
        QCOMPARE(f.cropSourceSize, native);
        QCOMPARE(f.cropRotation, 27.5);
    }
}

void ContentXformTest::combo_mapCrop_fourTurnsIdentity_withFlipsIrrelevant()
{
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(10, 20, 300, 400);
    x.cropSourceSize = QSize(2000, 1000);
    x.hFlip = true;
    x.vFlip = true;
    x.cropRotation = 0.0;
    ContentXform::mapCropThroughContentRotate90(x, 4);
    QCOMPARE(x.cropRect, QRect(10, 20, 300, 400));
    QCOMPARE(x.cropSourceSize, QSize(2000, 1000));
    QCOMPARE(x.cropRotation, 0.0);
}

void ContentXformTest::combo_sequence_cropThenEachRotate()
{
    const QSize native(4000, 3000);
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(200, 100, 1000, 500);
    x.cropSourceSize = native;
    x.cropRotation = 0.0;
    x.quarterTurns = 0;
    QCOMPARE(ContentXform::layoutSize(native, x), QSize(1000, 500));

    for (int step = 1; step <= 4; ++step) {
        ContentXform::mapCropThroughContentRotate90(x, 1);
        x.quarterTurns = ContentXform::normalizeQuarterTurns(x.quarterTurns + 1);
        QCOMPARE(x.cropRotation, 0.0);
        const QSize lay = ContentXform::layoutSize(native, x);
        QCOMPARE(lay, x.cropRect.size());
        if ((step % 2) != 0) {
            QCOMPARE(lay, QSize(500, 1000));
        } else {
            QCOMPARE(lay, QSize(1000, 500));
        }
    }
}

void ContentXformTest::combo_sequence_rotateThenCropInOrientedSpace()
{
    const QSize native(4000, 3000);
    ContentXform::Value x;
    x.quarterTurns = 1;
    x.hasCrop = true;
    x.cropSourceSize = QSize(3000, 4000);
    x.cropRect = QRect(100, 200, 600, 800);
    x.cropRotation = 0.0;
    QCOMPARE(ContentXform::layoutSize(native, x), QSize(600, 800));
}

void ContentXformTest::combo_flipFlags_doNotChangeLayoutSize()
{
    const QSize native(4000, 3000);
    for (int turns = 0; turns < 4; ++turns) {
        for (int hi = 0; hi < 2; ++hi) {
            for (int vi = 0; vi < 2; ++vi) {
                ContentXform::Value a;
                a.quarterTurns = turns;
                ContentXform::Value b = a;
                b.hFlip = (hi != 0);
                b.vFlip = (vi != 0);
                QCOMPARE(ContentXform::layoutSize(native, a),
                         ContentXform::layoutSize(native, b));

                ContentXform::Value ca = a;
                ca.hasCrop = true;
                ca.cropRect = QRect(0, 0, 800, 600);
                ca.cropSourceSize = native;
                if (turns != 0) {
                    ContentXform::mapCropThroughContentRotate90(ca, turns);
                }
                ContentXform::Value cb = ca;
                cb.hFlip = (hi != 0);
                cb.vFlip = (vi != 0);
                QCOMPARE(ContentXform::layoutSize(native, ca),
                         ContentXform::layoutSize(native, cb));
            }
        }
    }
}

void ContentXformTest::combo_pureMaterialize_sizeMatchesLayoutSize()
{
    const QSize native(40, 30);
    QImage raw(native, QImage::Format_RGB32);
    raw.fill(Qt::black);

    const QRect crops[] = {
        QRect(5, 5, 20, 10),
        QRect(0, 0, 40, 30),
        QRect(10, 8, 12, 16),
    };

    for (int turns = 0; turns < 4; ++turns) {
        for (int hi = 0; hi < 2; ++hi) {
            for (int vi = 0; vi < 2; ++vi) {
                ContentXform::Value full;
                full.quarterTurns = turns;
                full.hFlip = (hi != 0);
                full.vFlip = (vi != 0);
                {
                    const QImage out = pureMaterializeAxisAligned(raw, full);
                    QCOMPARE(out.size(), ContentXform::layoutSize(native, full));
                }
                for (const QRect &crop : crops) {
                    ContentXform::Value c;
                    c.quarterTurns = turns;
                    c.hFlip = (hi != 0);
                    c.vFlip = (vi != 0);
                    c.hasCrop = true;
                    c.cropRect = crop;
                    c.cropSourceSize = native;
                    c.cropRotation = 0.0;
                    if (turns != 0) {
                        ContentXform::mapCropThroughContentRotate90(c, turns);
                    }
                    const QImage out = pureMaterializeAxisAligned(raw, c);
                    QCOMPARE(out.size(), ContentXform::layoutSize(native, c));
                }
            }
        }
    }
}

void ContentXformTest::combo_pureMaterialize_cropPixelsSurviveAxisAlignedRotate()
{
    const QSize native(40, 30);
    QImage raw(native, QImage::Format_RGB32);
    raw.fill(QColor(10, 10, 10));
    const int mx = 15;
    const int my = 12;
    stampPixel(&raw, mx, my, 99);
    const QColor marker = raw.pixelColor(mx, my);

    const QRect crop(10, 8, 20, 14);
    QVERIFY(crop.contains(mx, my));

    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = crop;
    x.cropSourceSize = native;
    x.cropRotation = 0.0;
    x.quarterTurns = 0;

    {
        const QImage out = pureMaterializeAxisAligned(raw, x);
        QCOMPARE(out.size(), QSize(20, 14));
        QCOMPARE(out.pixelColor(mx - crop.x(), my - crop.y()), marker);
    }

    ContentXform::mapCropThroughContentRotate90(x, 1);
    x.quarterTurns = 1;
    QCOMPARE(x.cropRotation, 0.0);
    {
        const QImage out = pureMaterializeAxisAligned(raw, x);
        QCOMPARE(out.size(), ContentXform::layoutSize(native, x));
        bool found = false;
        for (int y = 0; y < out.height() && !found; ++y) {
            for (int xpix = 0; xpix < out.width(); ++xpix) {
                if (out.pixelColor(xpix, y) == marker) {
                    found = true;
                    break;
                }
            }
        }
        QVERIFY2(found, "marker missing after crop+rotate");
    }

    for (int step = 0; step < 2; ++step) {
        ContentXform::mapCropThroughContentRotate90(x, 1);
        x.quarterTurns = ContentXform::normalizeQuarterTurns(x.quarterTurns + 1);
        QCOMPARE(x.cropRotation, 0.0);
        const QImage out = pureMaterializeAxisAligned(raw, x);
        QCOMPARE(out.size(), ContentXform::layoutSize(native, x));
        bool found = false;
        for (int y = 0; y < out.height() && !found; ++y) {
            for (int xpix = 0; xpix < out.width(); ++xpix) {
                if (out.pixelColor(xpix, y) == marker) {
                    found = true;
                    break;
                }
            }
        }
        QVERIFY(found);
    }
}

void ContentXformTest::softPreview_scaledCropAspectMatchesLayout()
{
    // SoftPreview path: host is scaled down first, then crop is scaled into
    // soft space (same order as SessionAppearance::materializeDisplay SoftPreview).
    // layoutSize stays the native crop box — paint stretches soft into that
    // geometry. Soft crop sample aspect must match layout aspect.
    const QSize native(4000, 3000);
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(1000, 500, 2000, 1500);
    x.cropSourceSize = native;
    const QSize layout = ContentXform::layoutSize(native, x);
    QCOMPARE(layout, QSize(2000, 1500));

    QImage raw(native, QImage::Format_RGB32);
    raw.fill(Qt::black);
    stampPixel(&raw, 1000, 500, 1);   // crop TL
    stampPixel(&raw, 2999, 1999, 2);  // crop BR

    // Soft long edge 512 → 512×384
    const int softEdge = 512;
    const qreal s = qreal(softEdge) / qreal(qMax(native.width(), native.height()));
    const QSize softSize(qMax(1, qRound(native.width() * s)),
                         qMax(1, qRound(native.height() * s)));
    QImage soft = raw.scaled(softSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QCOMPARE(soft.size(), softSize);

    const QImage out = pureMaterializeAxisAligned(soft, x);
    QVERIFY(!out.isNull());
    // Aspect must match layout (within 1px rounding)
    QVERIFY(out.width() > 1 && out.height() > 1);
    const qreal layoutAspect = qreal(layout.width()) / qreal(layout.height());
    const qreal softAspect = qreal(out.width()) / qreal(out.height());
    QVERIFY2(qAbs(layoutAspect - softAspect) < 0.02,
             qPrintable(QStringLiteral("layout %1x%2 softCrop %3x%4")
                            .arg(layout.width()).arg(layout.height())
                            .arg(out.width()).arg(out.height())));
}


void ContentXformTest::combo_axisAligned_mapDoesNotArmFreeRotation()
{
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(0, 0, 100, 50);
    x.cropSourceSize = QSize(400, 300);
    x.cropRotation = 0.0;
    for (int k = 1; k <= 4; ++k) {
        ContentXform::mapCropThroughContentRotate90(x, 1);
        QVERIFY2(qAbs(x.cropRotation) <= 0.05,
                 "axis-aligned cropRotation must stay ~0 after content turn");
    }
}

void ContentXformTest::combo_equal_detectsAllContentFields()
{
    ContentXform::Value base;
    base.quarterTurns = 1;
    base.hFlip = true;
    base.vFlip = false;
    base.hasCrop = true;
    base.cropRect = QRect(1, 2, 3, 4);
    base.cropSourceSize = QSize(10, 20);
    base.cropRotation = 0.0;

    QVERIFY(ContentXform::equal(base, base));

    ContentXform::Value t = base;
    t.quarterTurns = 2;
    QVERIFY(!ContentXform::equal(base, t));
    t = base; t.hFlip = false; QVERIFY(!ContentXform::equal(base, t));
    t = base; t.vFlip = true; QVERIFY(!ContentXform::equal(base, t));
    t = base; t.cropRect = QRect(9, 9, 1, 1); QVERIFY(!ContentXform::equal(base, t));
    t = base; t.cropSourceSize = QSize(11, 20); QVERIFY(!ContentXform::equal(base, t));
    t = base; t.cropRotation = 12.0; QVERIFY(!ContentXform::equal(base, t));
    t = base; t.hasCrop = false; QVERIFY(!ContentXform::equal(base, t));
}


QTEST_MAIN(ContentXformTest)

void ContentXformTest::mapSourceRect_identity()
{
    ContentXform::Value x;
    QSize n(100, 50);
    QRectF src(10, 5, 20, 10);
    QRectF d = ContentXform::mapSourceRectToDisplay(src, n, x);
    QVERIFY(qAbs(d.x() - 10) < 1e-6);
    QVERIFY(qAbs(d.y() - 5) < 1e-6);
    QVERIFY(qAbs(d.width() - 20) < 1e-6);
    QVERIFY(qAbs(d.height() - 10) < 1e-6);
    QRectF back = ContentXform::mapDisplayRectToSource(d, n, x);
    QVERIFY(qAbs(back.x() - src.x()) < 1e-4);
    QVERIFY(qAbs(back.y() - src.y()) < 1e-4);
}

void ContentXformTest::mapSourceRect_turn90()
{
    ContentXform::Value x;
    x.quarterTurns = 1;
    QSize n(100, 50); // oriented becomes 50x100
    QRectF src(0, 0, 100, 50); // full
    QRectF d = ContentXform::mapSourceRectToDisplay(src, n, x);
    QVERIFY(d.isValid());
    // Full frame after 90 should cover oriented size
    QVERIFY(d.width() > 40 && d.height() > 40);
}

void ContentXformTest::mapDisplayRect_cropRoundTrip()
{
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(10, 10, 40, 30);
    x.cropSourceSize = QSize(100, 100);
    QSize n(100, 100);
    QRectF disp(0, 0, 40, 30);
    QRectF src = ContentXform::mapDisplayRectToSource(disp, n, x);
    QVERIFY(qAbs(src.x() - 10) < 1e-4);
    QVERIFY(qAbs(src.y() - 10) < 1e-4);
    QRectF back = ContentXform::mapSourceRectToDisplay(src, n, x);
    QVERIFY(qAbs(back.x() - 0) < 1e-4);
    QVERIFY(qAbs(back.width() - 40) < 1e-4);
}


void ContentXformTest::mapDisplayRect_freeRotExpands()
{
    ContentXform::Value x;
    x.hasCrop = true;
    x.cropRect = QRect(20, 20, 60, 40);
    x.cropSourceSize = QSize(100, 100);
    x.cropRotation = 30.0;
    QSize n(100, 100);
    QRectF disp(0, 0, 60, 40);
    QRectF src = ContentXform::mapDisplayRectToSource(disp, n, x);
    QVERIFY(!src.isEmpty());
    // Rotated window should cover more than the axis-aligned crop box in source.
    QVERIFY(src.width() >= 40 || src.height() >= 40);
}

void ContentXformTest::sourceToDisplayTransform_matchesMapCorners()
{
    // Transform AABB of corners must match mapSourceRectToDisplay when the
    // mapped rect is not clipped by crop (transform does not intersect; map
    // does). QVERIFY must stay in the test body (not a nested lambda).
    const QSize n(80, 60);
    const QRectF src(10, 8, 16, 12);

    ContentXform::Value orient;
    orient.hFlip = true;
    orient.quarterTurns = 1;

    {
        const QTransform T = ContentXform::sourceToDisplayTransform(n, orient);
        const QRectF mapped = ContentXform::mapSourceRectToDisplay(src, n, orient);
        QVERIFY2(!mapped.isEmpty(), "orient-only: source must land in oriented frame");
        // hFlip then +90 on 80x60: (x,y) → (60 - y, 80 - x).
        // TL (10,8) → (52,70); AABB of corners → [40,52] x [54,70].
        const QPointF tl = T.map(src.topLeft());
        QVERIFY2(qAbs(tl.x() - 52.0) < 1e-4 && qAbs(tl.y() - 70.0) < 1e-4,
                 qPrintable(QStringLiteral("orient TL %1,%2 (want 52,70)").arg(tl.x()).arg(tl.y())));
        const QPointF corners[4] = {
            src.topLeft(), src.topRight(), src.bottomRight(), src.bottomLeft(),
        };
        qreal minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const QPointF &c : corners) {
            const QPointF p = T.map(c);
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y());
            maxY = qMax(maxY, p.y());
        }
        QVERIFY2(qAbs(minX - 40.0) < 1e-4 && qAbs(maxX - 52.0) < 1e-4
                     && qAbs(minY - 54.0) < 1e-4 && qAbs(maxY - 70.0) < 1e-4,
                 qPrintable(QStringLiteral("orient AABB %1..%2 x %3..%4")
                                .arg(minX).arg(maxX).arg(minY).arg(maxY)));
        QVERIFY2(qAbs(minX - mapped.left()) < 1e-4,
                 qPrintable(QStringLiteral("orient left %1 vs %2").arg(minX).arg(mapped.left())));
        QVERIFY2(qAbs(maxX - mapped.right()) < 1e-4,
                 qPrintable(QStringLiteral("orient right %1 vs %2").arg(maxX).arg(mapped.right())));
        QVERIFY2(qAbs(minY - mapped.top()) < 1e-4,
                 qPrintable(QStringLiteral("orient top %1 vs %2").arg(minY).arg(mapped.top())));
        QVERIFY2(qAbs(maxY - mapped.bottom()) < 1e-4,
                 qPrintable(QStringLiteral("orient bottom %1 vs %2").arg(maxY).arg(mapped.bottom())));
    }

    // Crop in oriented space after hFlip+90: derive crop from oriented AABB so
    // map's intersect is a no-op (matches transform).
    const QRectF oriented = ContentXform::mapSourceRectToOriented(src, n, orient);
    QVERIFY2(!oriented.isEmpty(), "orient AABB for crop fixture");
    ContentXform::Value cropped = orient;
    cropped.hasCrop = true;
    cropped.cropRect = oriented.toRect().adjusted(-4, -4, 4, 4);
    QVERIFY2(cropped.cropRect.contains(oriented.toRect()),
             "crop must fully contain oriented source AABB");

    {
        const QTransform T = ContentXform::sourceToDisplayTransform(n, cropped);
        const QRectF mapped = ContentXform::mapSourceRectToDisplay(src, n, cropped);
        QVERIFY2(!mapped.isEmpty(), "cropped: source must land inside crop window");
        const QPointF corners[4] = {
            src.topLeft(), src.topRight(), src.bottomRight(), src.bottomLeft(),
        };
        qreal minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const QPointF &c : corners) {
            const QPointF p = T.map(c);
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y());
            maxY = qMax(maxY, p.y());
        }
        QVERIFY2(qAbs(minX - mapped.left()) < 1e-4,
                 qPrintable(QStringLiteral("crop left %1 vs %2").arg(minX).arg(mapped.left())));
        QVERIFY2(qAbs(maxX - mapped.right()) < 1e-4,
                 qPrintable(QStringLiteral("crop right %1 vs %2").arg(maxX).arg(mapped.right())));
        QVERIFY2(qAbs(minY - mapped.top()) < 1e-4,
                 qPrintable(QStringLiteral("crop top %1 vs %2").arg(minY).arg(mapped.top())));
        QVERIFY2(qAbs(maxY - mapped.bottom()) < 1e-4,
                 qPrintable(QStringLiteral("crop bottom %1 vs %2").arg(maxY).arg(mapped.bottom())));
    }
}

void ContentXformTest::sourceToDisplayTransform_hFlipMovesAndOrients()
{
    // Left-edge source cell must land on the right in display under hFlip;
    // transform (not dest AABB alone) is what orienting UV relies on.
    ContentXform::Value x;
    x.hFlip = true;
    const QSize n(200, 100);
    const QTransform T = ContentXform::sourceToDisplayTransform(n, x);
    const QPointF left(10, 50);
    const QPointF right(190, 50);
    const QPointF leftD = T.map(left);
    const QPointF rightD = T.map(right);
    QVERIFY(leftD.x() > rightD.x()); // flipped
    QVERIFY(qAbs(leftD.x() - (200.0 - 10.0)) < 1e-4);
    QVERIFY(qAbs(rightD.x() - (200.0 - 190.0)) < 1e-4);
}

#include "contentxform_test.moc"


