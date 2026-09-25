// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textsearchpolicy.h"

#include <QtTest/QtTest>

class TextSearchPolicyTest : public QObject
{
    Q_OBJECT
private slots:
    void singleRegion_partialFrac();
    void crossRegion_phrase();
    void crossRegion_noFalseJoin();
    void fuzzy_fullBox();
    void midWordSplit_tightBoxes();
    void midWordSplit_overlappingBoxes();
    void multiOccurrence_sameRegion();
    void emptyQuery_noHits();
    void threeColumns_noCrossJoin();
};

void TextSearchPolicyTest::singleRegion_partialFrac()
{
    QVector<QString> texts{QStringLiteral("hello beautiful world")};
    QVector<QRectF> boxes{QRectF(0, 0, 100, 10)};
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("beautiful"), false);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).regionIndex, 0);
    QVERIFY(hits.at(0).startFrac > 0.2);
    QVERIFY(hits.at(0).endFrac < 0.9);
    QVERIFY(hits.at(0).endFrac > hits.at(0).startFrac);
}

void TextSearchPolicyTest::crossRegion_phrase()
{
    QVector<QString> texts{QStringLiteral("hello"), QStringLiteral("world")};
    QVector<QRectF> boxes{QRectF(0, 0, 50, 10), QRectF(60, 0, 50, 10)};
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("hello world"), false);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits.at(0).regionIndex, 0);
    QCOMPARE(hits.at(1).regionIndex, 1);
    QVERIFY(hits.at(0).endFrac - hits.at(0).startFrac > 0.9);
    QVERIFY(hits.at(1).endFrac - hits.at(1).startFrac > 0.9);
}

void TextSearchPolicyTest::crossRegion_noFalseJoin()
{
    QVector<QString> texts{QStringLiteral("cat"), QStringLiteral("dog")};
    QVector<QRectF> boxes{QRectF(0, 0, 40, 10), QRectF(50, 0, 40, 10)};
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("catfish"), false);
    QCOMPARE(hits.size(), 0);
}

void TextSearchPolicyTest::fuzzy_fullBox()
{
    QVector<QString> texts{QStringLiteral("he11o")};
    QVector<QRectF> boxes{QRectF(0, 0, 40, 10)};
    const auto exact = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("hello"), false);
    QCOMPARE(exact.size(), 0);
}

void TextSearchPolicyTest::midWordSplit_tightBoxes()
{
    // "hel" + "lo" with almost no gap → stream "hello"
    QVector<QString> texts{QStringLiteral("hel"), QStringLiteral("lo")};
    QVector<QRectF> boxes{QRectF(0, 0, 30, 10), QRectF(31, 0, 20, 10)};
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("hello"), false);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits.at(0).regionIndex, 0);
    QCOMPARE(hits.at(1).regionIndex, 1);
}

void TextSearchPolicyTest::midWordSplit_overlappingBoxes()
{
    QVector<QString> texts{QStringLiteral("hel"), QStringLiteral("lo")};
    QVector<QRectF> boxes{QRectF(0, 0, 30, 10), QRectF(28, 0, 20, 10)}; // overlap
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("hello"), false);
    QCOMPARE(hits.size(), 2);
}

void TextSearchPolicyTest::multiOccurrence_sameRegion()
{
    QVector<QString> texts{QStringLiteral("foo bar foo")};
    QVector<QRectF> boxes{QRectF(0, 0, 100, 10)};
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("foo"), false);
    QCOMPARE(hits.size(), 2);
    QVERIFY(hits.at(0).startFrac < hits.at(1).startFrac);
}

void TextSearchPolicyTest::emptyQuery_noHits()
{
    QVector<QString> texts{QStringLiteral("hello")};
    QVector<QRectF> boxes{QRectF(0, 0, 40, 10)};
    QCOMPARE(TextSearchPolicy::findHits(texts, boxes, QString(), false).size(), 0);
    QCOMPARE(TextSearchPolicy::findHits(texts, boxes, QStringLiteral("   "), false).size(), 0);
}

void TextSearchPolicyTest::threeColumns_noCrossJoin()
{
    // Three "lines" at same Y, different MuPDF blocks (columns).
    QVector<QString> texts{QStringLiteral("alpha"), QStringLiteral("beta"),
                           QStringLiteral("gamma")};
    QVector<QRectF> boxes{QRectF(0, 0, 40, 10), QRectF(100, 0, 40, 10),
                          QRectF(200, 0, 40, 10)};
    QVector<int> blocks{0, 1, 2};
    // Must not form "alpha beta gamma" as one phrase across columns.
    const auto cross = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("alpha beta"), false, blocks);
    QCOMPARE(cross.size(), 0);
    // Within one column still works.
    const auto one = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("beta"), false, blocks);
    QCOMPARE(one.size(), 1);
    QCOMPARE(one.at(0).regionIndex, 1);
}

QTEST_MAIN(TextSearchPolicyTest)
#include "textsearchpolicy_test.moc"
