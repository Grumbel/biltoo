// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * BatchTargets::sessionIndices — pure index selection for range / even / odd.
 */

#include "item/batchtargets.h"

#include <QtTest/QtTest>

class BatchTargetsTest : public QObject
{
    Q_OBJECT
private slots:
    void emptySession();
    void currentAndSelection_noIndices();
    void indexRange_inclusive();
    void indexRange_swappedBounds();
    void indexRange_negativeToMeansLast();
    void indexRange_clamped();
    void evenIndices();
    void oddIndices();
    void evenOdd_singleItem();
};

void BatchTargetsTest::emptySession()
{
    QCOMPARE(BatchTargets::sessionIndices(BatchTargets::Mode::IndexRange, 0).size(), 0);
    QCOMPARE(BatchTargets::sessionIndices(BatchTargets::Mode::EvenIndices, 0).size(), 0);
}

void BatchTargetsTest::currentAndSelection_noIndices()
{
    // View-dependent modes return empty without a session walk.
    QCOMPARE(BatchTargets::sessionIndices(BatchTargets::Mode::Current, 5).size(), 0);
    QCOMPARE(BatchTargets::sessionIndices(BatchTargets::Mode::Selection, 5).size(), 0);
}

void BatchTargetsTest::indexRange_inclusive()
{
    const QList<int> idx = BatchTargets::sessionIndices(
        BatchTargets::Mode::IndexRange, 10, 2, 5);
    QCOMPARE(idx, QList<int>({2, 3, 4, 5}));
}

void BatchTargetsTest::indexRange_swappedBounds()
{
    const QList<int> idx = BatchTargets::sessionIndices(
        BatchTargets::Mode::IndexRange, 10, 5, 2);
    QCOMPARE(idx, QList<int>({2, 3, 4, 5}));
}

void BatchTargetsTest::indexRange_negativeToMeansLast()
{
    const QList<int> idx = BatchTargets::sessionIndices(
        BatchTargets::Mode::IndexRange, 4, 1, -1);
    QCOMPARE(idx, QList<int>({1, 2, 3}));
}

void BatchTargetsTest::indexRange_clamped()
{
    const QList<int> idx = BatchTargets::sessionIndices(
        BatchTargets::Mode::IndexRange, 5, -10, 100);
    QCOMPARE(idx, QList<int>({0, 1, 2, 3, 4}));
}

void BatchTargetsTest::evenIndices()
{
    const QList<int> idx = BatchTargets::sessionIndices(
        BatchTargets::Mode::EvenIndices, 6);
    QCOMPARE(idx, QList<int>({0, 2, 4}));
}

void BatchTargetsTest::oddIndices()
{
    const QList<int> idx = BatchTargets::sessionIndices(
        BatchTargets::Mode::OddIndices, 6);
    QCOMPARE(idx, QList<int>({1, 3, 5}));
}

void BatchTargetsTest::evenOdd_singleItem()
{
    QCOMPARE(BatchTargets::sessionIndices(BatchTargets::Mode::EvenIndices, 1),
             QList<int>({0}));
    QCOMPARE(BatchTargets::sessionIndices(BatchTargets::Mode::OddIndices, 1).size(), 0);
}

QTEST_MAIN(BatchTargetsTest)
#include "batchtargets_test.moc"
