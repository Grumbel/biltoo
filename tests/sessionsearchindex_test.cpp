// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/sessionsearchindex.h"

#include <QtTest/QtTest>

class SessionSearchIndexTest : public QObject
{
    Q_OBJECT
private slots:
    void clearBumpsGeneration();
    void commitRejectsStaleGeneration();
    void idAndPathLookup();
    void orderedIdsStable();
};

void SessionSearchIndexTest::clearBumpsGeneration()
{
    SessionSearchIndex idx;
    const quint64 g0 = idx.generation();
    idx.beginQuery(QStringLiteral("foo"));
    QVERIFY(idx.generation() != g0);
    QCOMPARE(idx.query(), QStringLiteral("foo"));
}

void SessionSearchIndexTest::commitRejectsStaleGeneration()
{
    SessionSearchIndex idx;
    const quint64 g = idx.beginQuery(QStringLiteral("x"));
    idx.beginQuery(QStringLiteral("y"));
    QVector<SessionSearchIndex::Hit> hits;
    hits.append({SessionImageId(1), QStringLiteral("/a"), 2});
    QVERIFY(!idx.commit(g, QStringLiteral("x"), hits));
    QVERIFY(idx.isEmpty());
}

void SessionSearchIndexTest::idAndPathLookup()
{
    SessionSearchIndex idx;
    const quint64 g = idx.beginQuery(QStringLiteral("q"));
    QVector<SessionSearchIndex::Hit> hits;
    hits.append({SessionImageId(42), QStringLiteral("/p/1"), 3});
    hits.append({kInvalidSessionImageId, QStringLiteral("/p/2"), 1});
    QVERIFY(idx.commit(g, QStringLiteral("q"), hits));
    QCOMPARE(idx.matchCount(SessionImageId(42)), quint16(3));
    QCOMPARE(idx.matchCount(kInvalidSessionImageId, QStringLiteral("/p/2")), quint16(1));
    QVERIFY(idx.hasHit(SessionImageId(42)));
    QVERIFY(!idx.hasHit(SessionImageId(99)));
}

void SessionSearchIndexTest::orderedIdsStable()
{
    SessionSearchIndex idx;
    const quint64 g = idx.beginQuery(QStringLiteral("q"));
    QVector<SessionSearchIndex::Hit> hits;
    hits.append({SessionImageId(2), QString(), 1});
    hits.append({SessionImageId(5), QString(), 1});
    hits.append({SessionImageId(2), QString(), 1}); // merge counts, one ordered slot
    QVERIFY(idx.commit(g, QStringLiteral("q"), hits));
    QCOMPARE(idx.orderedIds().size(), 2);
    QCOMPARE(idx.orderedIds().at(0), SessionImageId(2));
    QCOMPARE(idx.orderedIds().at(1), SessionImageId(5));
    QCOMPARE(idx.matchCount(SessionImageId(2)), quint16(2));
}

QTEST_MAIN(SessionSearchIndexTest)
#include "sessionsearchindex_test.moc"
