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
};

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
    QVERIFY(qAbs(osx - sx) < 1e-6);
    QVERIFY(qAbs(osy - sy) < 1e-6);
    QVERIFY(qAbs(ok - k) < 1e-6);
    // rotation may normalize; compare via unit vectors
    const qreal d = orot - rot;
    const qreal dNorm = d - 360.0 * qRound(d / 360.0);
    QVERIFY(qAbs(dNorm) < 1e-6 || qAbs(qAbs(dNorm) - 360.0) < 1e-6);
}

void PlacementLinearTest::sceneConjugate_uniform()
{
    const QTransform L = PlacementLinear::make(1.2, 0.8, 0.25, 30.0);
    QTransform S;
    S.scale(2.0, 2.0);
    const QTransform Lp = S * L;
    qreal sx, sy, k, rot;
    QVERIFY(PlacementLinear::decompose(Lp, &sx, &sy, &k, &rot));
    QVERIFY(qAbs(sx - 2.4) < 1e-6);
    QVERIFY(qAbs(sy - 1.6) < 1e-6);
    QVERIFY(qAbs(k - 0.25) < 1e-6);
}

void PlacementLinearTest::sceneConjugate_aniso()
{
    // Axis-aligned item under horizontal scene stretch → scaleX grows, shear 0.
    const QTransform L = PlacementLinear::make(1.0, 1.0, 0.0, 0.0);
    QTransform S;
    S.scale(2.0, 1.0);
    qreal sx, sy, k, rot;
    QVERIFY(PlacementLinear::decompose(S * L, &sx, &sy, &k, &rot));
    QVERIFY(qAbs(sx - 2.0) < 1e-6);
    QVERIFY(qAbs(sy - 1.0) < 1e-6);
    QVERIFY(qAbs(k) < 1e-6);
}

QTEST_MAIN(PlacementLinearTest)
#include "placementlinear_test.moc"
