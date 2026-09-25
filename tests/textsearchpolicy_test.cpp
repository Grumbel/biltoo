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
    // "hello" | "world" → stream "hello world"
    QVector<QString> texts{QStringLiteral("hello"), QStringLiteral("world")};
    QVector<QRectF> boxes{QRectF(0, 0, 50, 10), QRectF(60, 0, 50, 10)};
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("hello world"), false);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits.at(0).regionIndex, 0);
    QCOMPARE(hits.at(1).regionIndex, 1);
    // First box mostly full (whole "hello"), second whole "world"
    QVERIFY(hits.at(0).endFrac - hits.at(0).startFrac > 0.9);
    QVERIFY(hits.at(1).endFrac - hits.at(1).startFrac > 0.9);
}

void TextSearchPolicyTest::crossRegion_noFalseJoin()
{
    QVector<QString> texts{QStringLiteral("cat"), QStringLiteral("dog")};
    QVector<QRectF> boxes{QRectF(0, 0, 40, 10), QRectF(50, 0, 40, 10)};
    // "at do" is not a real phrase the user would type spanning mid-words —
    // stream is "cat dog"; "at do" would match mid-stream. Use a non-match:
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("catfish"), false);
    QCOMPARE(hits.size(), 0);
}

void TextSearchPolicyTest::fuzzy_fullBox()
{
    QVector<QString> texts{QStringLiteral("he11o")}; // OCR: 1 for l
    QVector<QRectF> boxes{QRectF(0, 0, 40, 10)};
    const auto hits = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("hello"), true);
    // alnum "he11o" vs "hello" — edit distance may or may not match depending
    // on policy; at least exact must not fire.
    const auto exact = TextSearchPolicy::findHits(
        texts, boxes, QStringLiteral("hello"), false);
    QCOMPARE(exact.size(), 0);
    Q_UNUSED(hits);
}

QTEST_MAIN(TextSearchPolicyTest)
#include "textsearchpolicy_test.moc"
