// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for PackOrderView (Tier 4 pack-order snapshot).
 */

#include "packorderview.h"

#include <QtTest/QtTest>

class PackOrderViewTest : public QObject
{
    Q_OBJECT
private slots:
    void empty_initial();
    void fromDocument_matchesMembership();
    void fromBook_preservesMultiplicity();
    void alignsWithDocument_falseWhenLoadAdd();
    void fromBook_equals_fromDocument_whenAligned();
};

void PackOrderViewTest::empty_initial()
{
    PackOrderView v;
    QVERIFY(v.isEmpty());
    QCOMPARE(v.size(), 0);
}

void PackOrderViewTest::fromDocument_matchesMembership()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    const PackOrderView v = PackOrderView::fromDocument(doc);
    QCOMPARE(v.size(), 2);
    QCOMPARE(v.pathAt(0), QStringLiteral("/a.jpg"));
    QCOMPARE(v.idAt(0), doc.idAt(0));
    QCOMPARE(v.idAt(1), doc.idAt(1));
    QVERIFY(v.alignsWithDocument(doc));
}

void PackOrderViewTest::fromBook_preservesMultiplicity()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/solo.jpg"));
    SessionPathOrder book;
    const SessionImageId sid = doc.idAt(0);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);

    const PackOrderView v = PackOrderView::fromBook(book);
    QCOMPARE(v.size(), 3);
    QCOMPARE(v.countPathOccurrences(QStringLiteral("/solo.jpg")), 3);
    QVERIFY(!v.alignsWithDocument(doc));
}

void PackOrderViewTest::alignsWithDocument_falseWhenLoadAdd()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/x.jpg"));
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/x.jpg"), doc.idAt(0));
    book.appendRow(QStringLiteral("/x.jpg"), doc.idAt(0));
    QVERIFY(!PackOrderView::fromBook(book).alignsWithDocument(doc));
}

void PackOrderViewTest::fromBook_equals_fromDocument_whenAligned()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/p.jpg"), QStringLiteral("/q.jpg")});
    SessionPathOrder book;
    book.setOrder(doc.paths(), doc.ids());

    const PackOrderView fromDoc = PackOrderView::fromDocument(doc);
    const PackOrderView fromBook = PackOrderView::fromBook(book);
    QCOMPARE(fromDoc, fromBook);
    QVERIFY(fromBook.alignsWithDocument(doc));
}

QTEST_MAIN(PackOrderViewTest)
#include "packorderview_test.moc"
