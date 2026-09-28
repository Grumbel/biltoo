// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Pure Spread helpers (docs/SPREAD.md): layoutSpread, buildFixedNMembers,
 * advanceSpreadAnchor. No Qt widgets / ImageView.
 */

#include "session/spreadstate.h"

#include <QtTest/QtTest>

class SpreadStateTest : public QObject
{
    Q_OBJECT
private slots:
    void rtlMirrorsLtr();
    void verticalStacks();
    void layout_empty();
    void layout_twoPages_heightMatch();
    void fixedN_strictPairs();
    void fixedN_coverAlone();
    void advance_bySpreadAndByPage();
};

void SpreadStateTest::layout_empty()
{
    const SpreadLayoutResult r = layoutSpread({});
    QCOMPARE(r.memberSlots.size(), 0);
    QVERIFY(r.unionRect.isEmpty());
}

void SpreadStateTest::layout_twoPages_heightMatch()
{
    QVector<QSizeF> sizes;
    sizes << QSizeF(200, 400) << QSizeF(300, 300);
    const SpreadLayoutResult r = layoutSpread(sizes, /*gutter=*/10.0, /*heightMatch=*/true);
    QCOMPARE(r.memberSlots.size(), 2);
    // Min height is 300 → first page scales 300/400.
    QCOMPARE(r.memberSlots.at(0).rect.height(), 300.0);
    QCOMPARE(r.memberSlots.at(1).rect.height(), 300.0);
    QVERIFY(r.memberSlots.at(1).rect.left() > r.memberSlots.at(0).rect.right());
    QVERIFY(r.unionRect.width() > 0);
    QVERIFY(r.unionRect.height() >= 300.0 - 0.01);
}

void SpreadStateTest::fixedN_strictPairs()
{
    QVector<SessionImageId> ids;
    for (int i = 1; i <= 5; ++i) {
        ids.append(SessionImageId(i));
    }
    // Anchor index 2 (id 3) → window [2,3] for n=2.
    const QVector<SessionImageId> m =
        buildFixedNMembers(ids, /*anchorIndex=*/2, /*n=*/2, SpreadBindingHint::StrictPairs);
    QCOMPARE(m.size(), 2);
    QCOMPARE(m.at(0), SessionImageId(3));
    QCOMPARE(m.at(1), SessionImageId(4));
    // Anchor 0 → [1,2]
    const QVector<SessionImageId> m0 =
        buildFixedNMembers(ids, 0, 2, SpreadBindingHint::StrictPairs);
    QCOMPARE(m0.at(0), SessionImageId(1));
    QCOMPARE(m0.at(1), SessionImageId(2));
}

void SpreadStateTest::fixedN_coverAlone()
{
    QVector<SessionImageId> ids;
    for (int i = 1; i <= 5; ++i) {
        ids.append(SessionImageId(i));
    }
    const QVector<SessionImageId> cover =
        buildFixedNMembers(ids, 0, 2, SpreadBindingHint::CoverAlone);
    QCOMPARE(cover.size(), 1);
    QCOMPARE(cover.at(0), SessionImageId(1));
    // Index 1 → start at 1, pair (2,3)
    const QVector<SessionImageId> p =
        buildFixedNMembers(ids, 1, 2, SpreadBindingHint::CoverAlone);
    QCOMPARE(p.size(), 2);
    QCOMPARE(p.at(0), SessionImageId(2));
    QCOMPARE(p.at(1), SessionImageId(3));
}

void SpreadStateTest::advance_bySpreadAndByPage()
{
    QCOMPARE(advanceSpreadAnchor(2, 10, 2, SpreadStride::BySpread, +1), 4);
    QCOMPARE(advanceSpreadAnchor(2, 10, 2, SpreadStride::BySpread, -1), 0);
    QCOMPARE(advanceSpreadAnchor(2, 10, 2, SpreadStride::ByPage, +1), 3);
    QCOMPARE(advanceSpreadAnchor(0, 10, 2, SpreadStride::BySpread, -1), -1);
    QCOMPARE(advanceSpreadAnchor(9, 10, 2, SpreadStride::BySpread, +1), -1);
}

void SpreadStateTest::rtlMirrorsLtr()
{
    QVector<QSizeF> sizes{QSizeF(100, 200), QSizeF(80, 200)};
    const auto ltr = layoutSpread(sizes, 10.0, true, SpreadDirection::Ltr);
    const auto rtl = layoutSpread(sizes, 10.0, true, SpreadDirection::Rtl);
    QCOMPARE(ltr.memberSlots.size(), 2);
    QCOMPARE(rtl.memberSlots.size(), 2);
    QVERIFY(qAbs(ltr.unionRect.width() - rtl.unionRect.width()) < 0.01);
    QVERIFY(rtl.memberSlots.at(0).rect.left() > rtl.memberSlots.at(1).rect.left());
}

void SpreadStateTest::verticalStacks()
{
    QVector<QSizeF> sizes{QSizeF(100, 200), QSizeF(50, 100)};
    const auto v = layoutSpread(sizes, 10.0, true, SpreadDirection::Vertical);
    QCOMPARE(v.memberSlots.size(), 2);
    QVERIFY(v.memberSlots.at(1).rect.top() > v.memberSlots.at(0).rect.bottom() - 0.01);
    // width-match to min width 50
    QCOMPARE(v.memberSlots.at(0).rect.width(), 50.0);
}

QTEST_APPLESS_MAIN(SpreadStateTest)
#include "spreadstate_test.moc"
