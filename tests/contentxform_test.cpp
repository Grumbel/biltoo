// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"

#include <QtTest/QtTest>

class ContentXformTest : public QObject
{
    Q_OBJECT
private slots:
    void normalizeTurns();
    void layoutSize_swapsOnOddTurns();
    void equal_ignoresPlacement();
    void needsRematerialize_upgradeAndXform();
};

void ContentXformTest::normalizeTurns()
{
    QCOMPARE(ContentXform::normalizeQuarterTurns(0), 0);
    QCOMPARE(ContentXform::normalizeQuarterTurns(1), 1);
    QCOMPARE(ContentXform::normalizeQuarterTurns(4), 0);
    QCOMPARE(ContentXform::normalizeQuarterTurns(-1), 3);
    QCOMPARE(ContentXform::normalizeQuarterTurns(5), 1);
}

void ContentXformTest::layoutSize_swapsOnOddTurns()
{
    const QSize native(4000, 3000);
    ContentXform::Value id;
    QCOMPARE(ContentXform::layoutSize(native, id), native);

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
}

QTEST_MAIN(ContentXformTest)
#include "contentxform_test.moc"
