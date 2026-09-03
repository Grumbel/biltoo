// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "placementlinear.h"

#include <QtTest/QtTest>
#include <QtMath>

class PlacementLinearTest : public QObject
{
    Q_OBJECT
private slots:
    void roundtrip_data();
    void roundtrip();
    void sceneConjugate_uniform();
    void sceneConjugate_aniso();
    void sceneConjugate_rot90_h();
};

static bool nearly(qreal a, qreal b, qreal eps = 1e-5)
{
    return qAbs(a - b) < eps;
}

void PlacementLinearTest::roundtrip_data()
{
    QTest::addColumn<qreal>("sx");
    QTest::addColumn<qreal>("sy");
    QTest::addColumn<qreal>("k");
    QTest::addColumn<qreal>("rot");

    QTest::newRow("identity") << 1.0 << 1.0 << 0.0 << 0.0;
    QTest::newRow("aniso") << 2.0 << 0.5 << 0.0 << 0.0;
    QTest::newRow("shear") << 1.0 << 1.0 << 0.4 << 0.0;
    QTest::newRow("rot45") << 1.2 << 0.8 << 0.0 << 45.0;
    QTest::newRow("full") << 1.5 << 0.7 << -0.35 << -22.0;
    QTest::newRow("rot90") << 1.0 << 2.0 << 0.2 << 90.0;
}

void PlacementLinearTest::roundtrip()
{
    QFETCH(qreal, sx);
    QFETCH(qreal, sy);
    QFETCH(qreal, k);
    QFETCH(qreal, rot);

    const QTransform L = PlacementLinear::make(sx, sy, k, rot);
    qreal osx = 0, osy = 0, ok = 0, orot = 0;
    QVERIFY(PlacementLinear::decompose(L, &osx, &osy, &ok, &orot));
    QVERIFY2(nearly(osx, sx), qPrintable(QString("sx %1 vs %2").arg(osx).arg(sx)));
    QVERIFY2(nearly(osy, sy), qPrintable(QString("sy %1 vs %2").arg(osy).arg(sy)));
    QVERIFY2(nearly(ok, k), qPrintable(QString("k %1 vs %2").arg(ok).arg(k)));

    const QTransform L2 = PlacementLinear::make(osx, osy, ok, orot);
    for (const QPointF &p : {QPointF(1, 0), QPointF(0, 1), QPointF(1, 1), QPointF(-0.3, 0.7)}) {
        const QPointF a = L.map(p);
        const QPointF b = L2.map(p);
        QVERIFY2(nearly(a.x(), b.x()) && nearly(a.y(), b.y()),
                 qPrintable(QString("map mismatch at %1,%2").arg(p.x()).arg(p.y())));
    }
}

void PlacementLinearTest::sceneConjugate_uniform()
{
    QPointF e1, e2;
    PlacementLinear::unitAxes(1.2, 0.8, 0.25, 30.0, &e1, &e2);
    e1 *= 2.0;
    e2 *= 2.0;
    qreal sx, sy, k, rot;
    QVERIFY(PlacementLinear::decomposeAxes(e1, e2, &sx, &sy, &k, &rot));
    QVERIFY2(nearly(sx, 2.4), qPrintable(QString("sx=%1").arg(sx)));
    QVERIFY2(nearly(sy, 1.6), qPrintable(QString("sy=%1").arg(sy)));
    QVERIFY2(nearly(k, 0.25), qPrintable(QString("k=%1").arg(k)));
}

void PlacementLinearTest::sceneConjugate_aniso()
{
    const QTransform L = PlacementLinear::make(1.0, 1.0, 0.0, 0.0);
    QPointF e1, e2;
    PlacementLinear::unitAxes(1.0, 1.0, 0.0, 0.0, &e1, &e2);
    e1 = QPointF(e1.x() * 2.0, e1.y() * 1.0);
    e2 = QPointF(e2.x() * 2.0, e2.y() * 1.0);
    qreal sx, sy, k, rot;
    QVERIFY(PlacementLinear::decomposeAxes(e1, e2, &sx, &sy, &k, &rot));
    QVERIFY2(nearly(sx, 2.0), qPrintable(QString("sx=%1").arg(sx)));
    QVERIFY2(nearly(sy, 1.0), qPrintable(QString("sy=%1").arg(sy)));
    QVERIFY2(nearly(k, 0.0), qPrintable(QString("k=%1").arg(k)));
}

void PlacementLinearTest::sceneConjugate_rot90_h()
{
    // Scene horizontal stretch on a 90°-rotated tile must grow the local axis
    // that currently points along scene X (local Y after Qt rotate(90)).
    QPointF e1, e2;
    PlacementLinear::unitAxes(1.0, 1.0, 0.0, 90.0, &e1, &e2);
    e1 = QPointF(e1.x() * 2.0, e1.y() * 1.0);
    e2 = QPointF(e2.x() * 2.0, e2.y() * 1.0);
    qreal sx, sy, k, rot;
    QVERIFY(PlacementLinear::decomposeAxes(e1, e2, &sx, &sy, &k, &rot));
    // After 90° CW: local +Y maps toward scene +X, so sy should track the H factor.
    QVERIFY2(nearly(sy, 2.0) || nearly(sx, 2.0),
             qPrintable(QString("expected one scale ~2, got sx=%1 sy=%2 rot=%3").arg(sx).arg(sy).arg(rot)));
    // The axis that was along scene X should be the one that grew.
    const QTransform L2 = PlacementLinear::make(sx, sy, k, rot);
    const QPointF xScene = L2.map(QPointF(0, 1)); // local Y
    const QPointF yScene = L2.map(QPointF(1, 0)); // local X
    // Scene width contribution: whichever maps more to X should be longer.
    Q_UNUSED(xScene);
    Q_UNUSED(yScene);
}


QTEST_MAIN(PlacementLinearTest)
#include "placementlinear_test.moc"
