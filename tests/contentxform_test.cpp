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

QTEST_MAIN(ContentXformTest)
#include "contentxform_test.moc"
