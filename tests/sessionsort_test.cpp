// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Pure session-list ordering (SessionSort).
 */

#include "session/sessionsort.h"
#include "host/pagepath.h"

#include <QtTest/QtTest>

class SessionSortTest : public QObject
{
    Q_OBJECT
private slots:
    void orderIndices_nameAndPath();
    void orderIndices_width();
    void modeNeedsImageProbe_flags();
    void looksLikePagedDocument_majority();
};

void SessionSortTest::orderIndices_nameAndPath()
{
    const QStringList paths = {
        QStringLiteral("/z/b10.jpg"),
        QStringLiteral("/a/b2.jpg"),
        QStringLiteral("/a/b1.jpg"),
    };
    const QVector<int> byName = SessionSort::orderIndices(SessionSort::Mode::Name, paths);
    QCOMPARE(byName.size(), 3);
    // displayName numeric: b1, b2, b10
    QCOMPARE(paths.at(byName.at(0)), QStringLiteral("/a/b1.jpg"));
    QCOMPARE(paths.at(byName.at(1)), QStringLiteral("/a/b2.jpg"));
    QCOMPARE(paths.at(byName.at(2)), QStringLiteral("/z/b10.jpg"));

    const QVector<int> byPath = SessionSort::orderIndices(SessionSort::Mode::Path, paths);
    QCOMPARE(paths.at(byPath.at(0)), QStringLiteral("/a/b1.jpg"));
    QCOMPARE(paths.at(byPath.at(1)), QStringLiteral("/a/b2.jpg"));
    QCOMPARE(paths.at(byPath.at(2)), QStringLiteral("/z/b10.jpg"));
}

void SessionSortTest::orderIndices_width()
{
    const QStringList paths = {
        QStringLiteral("/wide.jpg"),
        QStringLiteral("/narrow.jpg"),
        QStringLiteral("/mid.jpg"),
    };
    QHash<QString, QSize> sizes;
    sizes.insert(QStringLiteral("/wide.jpg"), QSize(200, 10));
    sizes.insert(QStringLiteral("/narrow.jpg"), QSize(50, 10));
    sizes.insert(QStringLiteral("/mid.jpg"), QSize(100, 10));
    const QVector<int> order = SessionSort::orderIndices(
        SessionSort::Mode::Width, paths, sizes);
    QCOMPARE(paths.at(order.at(0)), QStringLiteral("/narrow.jpg"));
    QCOMPARE(paths.at(order.at(1)), QStringLiteral("/mid.jpg"));
    QCOMPARE(paths.at(order.at(2)), QStringLiteral("/wide.jpg"));
}

void SessionSortTest::modeNeedsImageProbe_flags()
{
    QVERIFY(!SessionSort::modeNeedsImageProbe(SessionSort::Mode::Name));
    QVERIFY(!SessionSort::modeNeedsImageProbe(SessionSort::Mode::Path));
    QVERIFY(!SessionSort::modeNeedsImageProbe(SessionSort::Mode::Shuffle));
    QVERIFY(SessionSort::modeNeedsImageProbe(SessionSort::Mode::Width));
    QVERIFY(SessionSort::modeNeedsImageProbe(SessionSort::Mode::MTime));
}

void SessionSortTest::looksLikePagedDocument_majority()
{
    QVERIFY(!SessionSort::looksLikePagedDocument({}));
    // Non-page paths
    QVERIFY(!SessionSort::looksLikePagedDocument(
        {QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")}));
}

QTEST_MAIN(SessionSortTest)
#include "sessionsort_test.moc"
