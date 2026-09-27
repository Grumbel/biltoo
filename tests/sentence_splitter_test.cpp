// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "speech/SentenceSplitter.h"

class TestSentenceSplitter : public QObject {
    Q_OBJECT
private slots:
    void offsetsRoundTrip_data();
    void offsetsRoundTrip();
    void handlesAbbreviationsAndDecimals();
    void handlesInitials();
    void newlinesBreakSegments();
    void blankLineBreaksParagraphs();
    void longUnpunctuatedTextGetsChunked();
    void unbreakableRunGetsHardCut();
    void skipsUnspeakableJunkSegments();
};

void TestSentenceSplitter::offsetsRoundTrip_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<int>("expectedCount");

    QTest::newRow("basic") << "Chapter 3. The spacecraft entered orbit around Mars. "
                              "The first images arrived twelve minutes later."
                           << 3;
    QTest::newRow("punctuation-mix") << "Is this it? Yes! Finally." << 3;
    QTest::newRow("empty") << "" << 0;
}

void TestSentenceSplitter::offsetsRoundTrip()
{
    QFETCH(QString, input);
    QFETCH(int, expectedCount);

    auto sentences = SentenceSplitter::split(input);
    QCOMPARE(sentences.size(), expectedCount);
    for (const auto &s : sentences) {
        // Every sentence's recorded offsets must round-trip back to its text --
        // this is what DocumentView relies on for QTextCursor placement.
        QCOMPARE(input.mid(s.start, s.end - s.start), s.text);
    }
}

void TestSentenceSplitter::handlesAbbreviationsAndDecimals()
{
    auto sentences = SentenceSplitter::split(
        "Dr. Smith went to Mars at 3.5 km/s. It was a long trip.");
    QCOMPARE(sentences.size(), 2);
    QCOMPARE(sentences[0].text, QStringLiteral("Dr. Smith went to Mars at 3.5 km/s."));
    QCOMPARE(sentences[1].text, QStringLiteral("It was a long trip."));
}

void TestSentenceSplitter::handlesInitials()
{
    auto sentences = SentenceSplitter::split("J. R. R. Tolkien wrote it. Everyone loved it.");
    QCOMPARE(sentences.size(), 2);
    QCOMPARE(sentences[0].text, QStringLiteral("J. R. R. Tolkien wrote it."));
}

void TestSentenceSplitter::newlinesBreakSegments()
{
    QString input = "Line one\nLine two\nLine three";
    auto sentences = SentenceSplitter::split(input);
    QCOMPARE(sentences.size(), 3);
    QCOMPARE(sentences[0].text, QStringLiteral("Line one"));
    QCOMPARE(sentences[1].text, QStringLiteral("Line two"));
    QCOMPARE(sentences[2].text, QStringLiteral("Line three"));
    for (const auto &s : sentences) {
        QCOMPARE(input.mid(s.start, s.end - s.start), s.text);
    }
}

void TestSentenceSplitter::blankLineBreaksParagraphs()
{
    QString input = "Para one still going\n\nPara two.";
    auto sentences = SentenceSplitter::split(input);
    QCOMPARE(sentences.size(), 2);
    QCOMPARE(sentences[0].text, QStringLiteral("Para one still going"));
    QCOMPARE(sentences[1].text, QStringLiteral("Para two."));
}

void TestSentenceSplitter::longUnpunctuatedTextGetsChunked()
{
    QStringList words;
    for (int i = 0; i < 100; ++i) {
        words << QStringLiteral("word%1").arg(i);
    }
    QString input = words.join(' ');

    auto sentences = SentenceSplitter::split(input, 0, /*maxLen=*/50);
    QVERIFY(sentences.size() > 1);
    for (const auto &s : sentences) {
        QVERIFY(s.text.size() <= 50);
        QCOMPARE(input.mid(s.start, s.end - s.start), s.text);
    }
}

void TestSentenceSplitter::unbreakableRunGetsHardCut()
{
    QString input(500, QLatin1Char('x')); // no whitespace anywhere
    auto sentences = SentenceSplitter::split(input, 0, /*maxLen=*/100);
    QCOMPARE(sentences.size(), 5);
    for (const auto &s : sentences) {
        QCOMPARE(s.text.size(), 100);
        QCOMPARE(input.mid(s.start, s.end - s.start), s.text);
    }
}

void TestSentenceSplitter::skipsUnspeakableJunkSegments()
{
    // Simulates leftover HTML-derived punctuation/symbols between real
    // sentences (the crash this guards against: Piper reduces text with no
    // letters/digits to zero phonemes and then fails writing a WAV header).
    QString input = "Real sentence here.\n---\n***\nAnother real one.";
    auto sentences = SentenceSplitter::split(input);
    QCOMPARE(sentences.size(), 2);
    QCOMPARE(sentences[0].text, QStringLiteral("Real sentence here."));
    QCOMPARE(sentences[1].text, QStringLiteral("Another real one."));
}

QTEST_APPLESS_MAIN(TestSentenceSplitter)
#include "sentence_splitter_test.moc"
