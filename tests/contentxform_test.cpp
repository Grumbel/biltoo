// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"

#include <QtTest/QtTest>

/**
 * Pure ContentXform contract tests.
 *
 * layoutSize(native, want) is the sole geometry rule for attachDisplaySample
 * (docs/CONTENT_PIPELINE.md): orient full frame, then crop box in post-orient
 * space. Ignoring crop left cropped pixels in a full-frame intrinsic → stretch.
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
    void equal_ignoresPlacement();
    void equal_detectsCrop();
    void needsRematerialize_upgradeAndXform();
    void fromState_roundTrip();
};

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
    WorkspaceItemState st;
    QCOMPARE(ContentXform::layoutSize(native, st), native);
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
    QVERIFY(!ContentXform::swapsAspect(t2));

    ContentXform::Value t3;
    t3.quarterTurns = 3;
    QCOMPARE(ContentXform::layoutSize(native, t3), QSize(3000, 4000));
}

void ContentXformTest::layoutSize_cropDefinesBox()
{
    // Cropped intrinsic must be crop size, not full frame (stretch root cause).
    const QSize native(4000, 3000);
    ContentXform::Value c;
    c.hasCrop = true;
    c.cropRect = QRect(100, 50, 800, 600);
    c.cropSourceSize = native;
    QCOMPARE(ContentXform::layoutSize(native, c), QSize(800, 600));
}

void ContentXformTest::layoutSize_cropScaledFromSourceSize()
{
    // Soft sample native 400×300; crop recorded on full 4000×3000.
    const QSize soft(400, 300);
    ContentXform::Value c;
    c.hasCrop = true;
    c.cropRect = QRect(1000, 500, 2000, 1500);
    c.cropSourceSize = QSize(4000, 3000);
    // scale: 2000*(400/4000)=200, 1500*(300/3000)=150
    QCOMPARE(ContentXform::layoutSize(soft, c), QSize(200, 150));
}

void ContentXformTest::layoutSize_cropThenOddTurn()
{
    // materialize: orient then crop in post-orient space.
    // cropRect is already post-orient; layoutSize uses oriented native as basis.
    const QSize native(4000, 3000);
    ContentXform::Value c;
    c.quarterTurns = 1; // oriented = 3000×4000
    c.hasCrop = true;
    c.cropRect = QRect(10, 20, 500, 700);
    c.cropSourceSize = QSize(3000, 4000);
    QCOMPARE(ContentXform::layoutSize(native, c), QSize(500, 700));
}

void ContentXformTest::layoutSize_cropWithEvenTurn()
{
    const QSize native(4000, 3000);
    ContentXform::Value c;
    c.quarterTurns = 2; // oriented = same as native
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
    c.cropRect = QRect(); // empty
    c.quarterTurns = 1;
    QCOMPARE(ContentXform::layoutSize(native, c), QSize(3000, 4000));

    ContentXform::Value c2;
    c2.hasCrop = false;
    c2.cropRect = QRect(0, 0, 100, 100); // hasCrop false → ignore rect
    QCOMPARE(ContentXform::layoutSize(native, c2), native);
}

void ContentXformTest::layoutSize_invalidNative()
{
    ContentXform::Value c;
    c.hasCrop = true;
    c.cropRect = QRect(0, 0, 10, 10);
    QCOMPARE(ContentXform::layoutSize(QSize(), c), QSize());
    QCOMPARE(ContentXform::layoutSize(QSize(0, 100), c), QSize(0, 100));
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
    QVERIFY(!ContentXform::needsRematerialize(id, t1, 512, 0));

    ContentXform::Value crop = id;
    crop.hasCrop = true;
    crop.cropRect = QRect(0, 0, 10, 10);
    QVERIFY(ContentXform::needsRematerialize(id, crop, 512, 512));
}

void ContentXformTest::fromState_roundTrip()
{
    WorkspaceItemState s;
    s.contentQuarterTurns = 5; // normalizes to 1
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
    QCOMPARE(out.contentHFlip, true);
    QCOMPARE(out.hasCrop, true);
    QCOMPARE(out.cropRect, QRect(1, 2, 3, 4));
    QCOMPARE(out.cropSourceSize, QSize(100, 200));
    QCOMPARE(out.colorAdjust.brightness, 10);
}

QTEST_MAIN(ContentXformTest)
#include "contentxform_test.moc"
