// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for PackOrderOverlay (Tier 4 residual design type).
 *
 * Locks resolve semantics before ImageView adopts the overlay in place of
 * the view pack-order overlay. Critical invariant: Explicit-empty must suppress pack
 * even when SessionDocument still has membership (mode-leave / pathOrderClear).
 */

#include "packorderoverlay.h"

#include <QtTest/QtTest>

class PackOrderOverlayTest : public QObject
{
    Q_OBJECT
private slots:
    void default_followDocument_emptyWithoutDoc();
    void followDocument_usesMembership();
    void clearExplicit_suppressesDocument();
    void setExplicit_multiplicity();
    void appendExplicitRow_promotesFromFollow();
    void setExplicit_fromPackOrderView();
    void countPathOccurrences_respectsMode();
    void followDocument_afterExplicit_dropsHeldOrder();
};

void PackOrderOverlayTest::default_followDocument_emptyWithoutDoc()
{
    PackOrderOverlay o;
    QCOMPARE(o.mode(), PackOrderOverlay::Mode::FollowDocument);
    QVERIFY(!o.isExplicit());
    QVERIFY(o.resolve(nullptr).isEmpty());
}

void PackOrderOverlayTest::followDocument_usesMembership()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});

    PackOrderOverlay o;
    o.followDocument();
    const PackOrderView v = o.resolve(&doc);
    QCOMPARE(v.size(), 2);
    QCOMPARE(v.pathAt(0), QStringLiteral("/a.jpg"));
    QCOMPARE(v.idAt(0), doc.idAt(0));
    QVERIFY(v.alignsWithDocument(doc));
}

void PackOrderOverlayTest::clearExplicit_suppressesDocument()
{
    // Dual-model reason the book still exists: after clear, pack must stay
    // blank while document membership remains.
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});

    PackOrderOverlay o;
    o.setExplicit(doc.paths(), doc.ids());
    QCOMPARE(o.resolve(&doc).size(), 2);

    o.clearExplicit();
    QVERIFY(o.isExplicit());
    QVERIFY(o.isExplicitEmpty());
    QVERIFY(o.resolve(&doc).isEmpty());
    QVERIFY(!doc.isEmpty());
}

void PackOrderOverlayTest::setExplicit_multiplicity()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/solo.jpg"));
    const SessionImageId sid = doc.idAt(0);

    PackOrderOverlay o;
    SessionPathOrder book;
    book.appendRow(QStringLiteral("/solo.jpg"), sid);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);
    o.setExplicit(book);

    const PackOrderView v = o.resolve(&doc);
    QCOMPARE(v.size(), 3);
    QCOMPARE(v.countPathOccurrences(QStringLiteral("/solo.jpg")), 3);
    QVERIFY(!v.alignsWithDocument(doc));
}

void PackOrderOverlayTest::appendExplicitRow_promotesFromFollow()
{
    PackOrderOverlay o;
    QVERIFY(!o.isExplicit());
    o.appendExplicitRow(QStringLiteral("/x.jpg"), kInvalidSessionImageId);
    QVERIFY(o.isExplicit());
    QCOMPARE(o.explicitSize(), 1);
    QCOMPARE(o.resolve(nullptr).pathAt(0), QStringLiteral("/x.jpg"));
}

void PackOrderOverlayTest::setExplicit_fromPackOrderView()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/p.jpg")});
    const PackOrderView seed = PackOrderView::fromDocument(doc);

    PackOrderOverlay o;
    o.setExplicit(seed);
    QVERIFY(o.isExplicit());
    QCOMPARE(o.resolve(&doc), seed);
}

void PackOrderOverlayTest::countPathOccurrences_respectsMode()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/a.jpg"));

    PackOrderOverlay o;
    o.followDocument();
    QCOMPARE(o.countPathOccurrences(QStringLiteral("/a.jpg"), &doc), 1);

    o.clearExplicit();
    QCOMPARE(o.countPathOccurrences(QStringLiteral("/a.jpg"), &doc), 0);

    o.setExplicit({QStringLiteral("/a.jpg"), QStringLiteral("/a.jpg")},
                  {doc.idAt(0), doc.idAt(0)});
    QCOMPARE(o.countPathOccurrences(QStringLiteral("/a.jpg"), &doc), 2);
}

void PackOrderOverlayTest::followDocument_afterExplicit_dropsHeldOrder()
{
    PackOrderOverlay o;
    o.setExplicit({QStringLiteral("/z.jpg")}, {kInvalidSessionImageId});
    QVERIFY(o.isExplicit());
    o.followDocument();
    QCOMPARE(o.mode(), PackOrderOverlay::Mode::FollowDocument);
    QVERIFY(o.explicitOrder().isEmpty());
}

QTEST_MAIN(PackOrderOverlayTest)
#include "packorderoverlay_test.moc"
