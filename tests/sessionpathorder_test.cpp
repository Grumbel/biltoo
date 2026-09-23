// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for SessionPathOrder (Gallery-local path order book).
 * See docs/PATH_ORDER.md — multiplicity is independent of SessionDocument.
 */

#include "session/sessionpathorder.h"

#include <QtTest/QtTest>

class SessionPathOrderTest : public QObject
{
    Q_OBJECT
private slots:
    void empty_initial();
    void append_and_countOccurrences();
    void setOrder_alignsIds();
    void firstIdForPath_skipsInvalid();
    void clear_empties();
    void pathList_and_idList_parallel();
    void setOrder_trimsExtraIds();
    void pathAt_idAt_outOfRange();
    void duplicatePaths_independentSlots();
};

void SessionPathOrderTest::empty_initial()
{
    SessionPathOrder book;
    QVERIFY(book.isEmpty());
    QCOMPARE(book.size(), 0);
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/a.jpg")), 0);
    QCOMPARE(book.firstIdForPath(QStringLiteral("/a.jpg")), kInvalidSessionImageId);
}

void SessionPathOrderTest::append_and_countOccurrences()
{
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/a.jpg"), 10);
    book.appendRow(QStringLiteral("/b.jpg"), 20);
    book.appendRow(QStringLiteral("/a.jpg"), 30); // second tile, same path
    QCOMPARE(book.size(), 3);
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/a.jpg")), 2);
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/b.jpg")), 1);
    QCOMPARE(book.pathAt(2), QStringLiteral("/a.jpg"));
    QCOMPARE(book.idAt(2), SessionImageId(30));
}

void SessionPathOrderTest::setOrder_alignsIds()
{
    SessionPathOrder book;
    book.setOrder({QStringLiteral("/x.jpg"), QStringLiteral("/y.jpg")},
                  {SessionImageId(1)}); // short ids → pad invalid
    QCOMPARE(book.size(), 2);
    QCOMPARE(book.idAt(0), SessionImageId(1));
    QCOMPARE(book.idAt(1), kInvalidSessionImageId);
}

void SessionPathOrderTest::firstIdForPath_skipsInvalid()
{
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/a.jpg"), kInvalidSessionImageId);
    book.appendRow(QStringLiteral("/a.jpg"), 42);
    QCOMPARE(book.firstIdForPath(QStringLiteral("/a.jpg")), SessionImageId(42));
}

void SessionPathOrderTest::clear_empties()
{
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/a.jpg"), 1);
    book.clear();
    QVERIFY(book.isEmpty());
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/a.jpg")), 0);
}

void SessionPathOrderTest::pathList_and_idList_parallel()
{
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/a.jpg"), 10);
    book.appendRow(QStringLiteral("/b.jpg"), 20);
    QCOMPARE(book.pathList().size(), book.idList().size());
    QCOMPARE(book.pathList().at(0), QStringLiteral("/a.jpg"));
    QCOMPARE(book.idList().at(1), SessionImageId(20));
}

void SessionPathOrderTest::setOrder_trimsExtraIds()
{
    SessionPathOrder book;
    book.setOrder({QStringLiteral("/only.jpg")},
                  {SessionImageId(1), SessionImageId(2), SessionImageId(3)});
    QCOMPARE(book.size(), 1);
    QCOMPARE(book.idAt(0), SessionImageId(1));
    QCOMPARE(book.idList().size(), 1);
}

void SessionPathOrderTest::pathAt_idAt_outOfRange()
{
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/a.jpg"), 7);
    QCOMPARE(book.pathAt(-1), QString());
    QCOMPARE(book.pathAt(99), QString());
    QCOMPARE(book.idAt(-1), kInvalidSessionImageId);
    QCOMPARE(book.idAt(99), kInvalidSessionImageId);
}

void SessionPathOrderTest::duplicatePaths_independentSlots()
{
    // Multiplicity is Gallery-local: same path, distinct rows/ids.
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/dup.jpg"), 1);
    book.appendRow(QStringLiteral("/dup.jpg"), 2);
    book.appendRow(QStringLiteral("/dup.jpg"), 3);
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/dup.jpg")), 3);
    QCOMPARE(book.idAt(0), SessionImageId(1));
    QCOMPARE(book.idAt(2), SessionImageId(3));
    QCOMPARE(book.firstIdForPath(QStringLiteral("/dup.jpg")), SessionImageId(1));
}

QTEST_MAIN(SessionPathOrderTest)
#include "sessionpathorder_test.moc"
