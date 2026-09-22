// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for SessionAppearance helpers + seed book (Stage 4b).
 * Content appearance is ItemWorld sparse tables keyed by SessionImageId.
 */

#include "sessionappearance.h"
#include "sessionseedbook.h"
#include "sessiondocument.h"
#include "itemworld.h"

#include <QtTest/QtTest>

class SessionAppearanceTest : public QObject
{
    Q_OBJECT
private slots:
    void seedBook_keyedById();
    void duplicatePath_independentCrop();
    void seed_remove_clearsFlag();
    void materialize_identityWhenNoCrop();
    void materialize_appliesCrop();
    void materialize_quarterTurnSwapsAspect();
    void documentRemove_clearsSeed();
    void replaceAll_preservesSeedById();
    // Orient authority (2208–2211)
    void withoutContentOrient_stripsOrientKeepsGrade();
    void orientAuthorityWant_passthroughWhenHasOrient();
    void orientAuthorityWant_stripsWhenPlacementOnly();
    void clearedContentOps_stripsOrientAndGrade();
};

void SessionAppearanceTest::seedBook_keyedById()
{
    SessionSeedBook store;
    store.markSeedAttempted(7);
    QVERIFY(store.seedAttempted(7));
    QVERIFY(!store.seedAttempted(8));
}

void SessionAppearanceTest::duplicatePath_independentCrop()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/same.jpg"));
    doc.append(QStringLiteral("/same.jpg"));
    const SessionImageId id0 = doc.idAt(0);
    const SessionImageId id1 = doc.idAt(1);
    QVERIFY(id0 != id1);

    ItemWorld world;
    WorkspaceItemState a;
    a.hasCrop = true;
    a.cropRect = QRect(0, 0, 50, 50);
    WorkspaceItemState b;
    b.hasCrop = true;
    b.cropRect = QRect(100, 100, 50, 50);
    world.setAppearance(id0, a);
    world.setAppearance(id1, b);

    QCOMPARE(world.appearanceValue(id0).cropRect, QRect(0, 0, 50, 50));
    QCOMPARE(world.appearanceValue(id1).cropRect, QRect(100, 100, 50, 50));
}

void SessionAppearanceTest::seed_remove_clearsFlag()
{
    SessionSeedBook store;
    store.markSeedAttempted(3);
    store.remove(3);
    QVERIFY(!store.seedAttempted(3));
}

void SessionAppearanceTest::materialize_identityWhenNoCrop()
{
    QImage raw(32, 24, QImage::Format_RGB32);
    raw.fill(Qt::red);
    WorkspaceItemState empty;
    const QImage out = SessionAppearance::materializeDisplay(
        raw, empty, SessionAppearance::PixelKind::FullSource);
    QCOMPARE(out.size(), raw.size());
}

void SessionAppearanceTest::materialize_appliesCrop()
{
    QImage raw(100, 80, QImage::Format_RGB32);
    raw.fill(Qt::blue);
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(10, 10, 40, 30);
    st.cropSourceSize = QSize(100, 80);
    const QImage out = SessionAppearance::materializeDisplay(
        raw, st, SessionAppearance::PixelKind::FullSource);
    QCOMPARE(out.width(), 40);
    QCOMPARE(out.height(), 30);
}

void SessionAppearanceTest::materialize_quarterTurnSwapsAspect()
{
    QImage raw(40, 20, QImage::Format_RGB32);
    raw.fill(Qt::green);
    WorkspaceItemState st;
    st.contentQuarterTurns = 1; // 90°
    const QImage out = SessionAppearance::materializeDisplay(
        raw, st, SessionAppearance::PixelKind::FullSource);
    QCOMPARE(out.width(), 20);
    QCOMPARE(out.height(), 40);
}

void SessionAppearanceTest::documentRemove_clearsSeed()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/a.jpg"));
    const SessionImageId id = doc.idAt(0);
    doc.seedBook().markSeedAttempted(id);
    QVERIFY(doc.seedBook().seedAttempted(id));
    doc.removeAt(0);
    QVERIFY(!doc.seedBook().seedAttempted(id));
}

void SessionAppearanceTest::replaceAll_preservesSeedById()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    const SessionImageId idA = doc.idAt(0);
    const SessionImageId idB = doc.idAt(1);
    doc.seedBook().markSeedAttempted(idA);
    doc.replaceAll({QStringLiteral("/b.jpg"), QStringLiteral("/a.jpg")}, {idB, idA});
    QVERIFY(doc.seedBook().seedAttempted(idA));
    QVERIFY(!doc.seedBook().seedAttempted(idB));
}

void SessionAppearanceTest::withoutContentOrient_stripsOrientKeepsGrade()
{
    WorkspaceItemState st;
    st.contentQuarterTurns = 1;
    st.contentHFlip = true;
    st.contentVFlip = true;
    st.hasCrop = true;
    st.cropRect = QRect(1, 2, 3, 4);
    st.cropSourceSize = QSize(100, 80);
    st.colorAdjust.brightness = 12;
    st.pos = QPointF(9, 8);
    st.scale = 1.5;

    const WorkspaceItemState out = SessionAppearance::withoutContentOrient(st);
    QCOMPARE(out.contentQuarterTurns, 0);
    QVERIFY(!out.contentHFlip);
    QVERIFY(!out.contentVFlip);
    QVERIFY(!out.hasCrop);
    QVERIFY(out.cropRect.isEmpty());
    QCOMPARE(out.colorAdjust.brightness, 12);
    QCOMPARE(out.pos, QPointF(9, 8));
    QCOMPARE(out.scale, 1.5);
}

void SessionAppearanceTest::orientAuthorityWant_passthroughWhenHasOrient()
{
    WorkspaceItemState st;
    st.contentQuarterTurns = 3;
    st.contentHFlip = true;
    st.hasCrop = true;
    st.cropRect = QRect(5, 5, 10, 10);

    const WorkspaceItemState out =
        SessionAppearance::orientAuthorityWant(true, st);
    QCOMPARE(out.contentQuarterTurns, 3);
    QVERIFY(out.contentHFlip);
    QVERIFY(out.hasCrop);
    QCOMPARE(out.cropRect, QRect(5, 5, 10, 10));
}

void SessionAppearanceTest::orientAuthorityWant_stripsWhenPlacementOnly()
{
    WorkspaceItemState st;
    st.contentQuarterTurns = 2;
    st.contentVFlip = true;
    st.hasCrop = true;
    st.cropRect = QRect(0, 0, 20, 20);
    st.colorAdjust.contrast = 110;
    st.pos = QPointF(1, 2);

    const WorkspaceItemState out =
        SessionAppearance::orientAuthorityWant(false, st);
    QCOMPARE(out.contentQuarterTurns, 0);
    QVERIFY(!out.contentVFlip);
    QVERIFY(!out.hasCrop);
    QCOMPARE(out.colorAdjust.contrast, 110);
    QCOMPARE(out.pos, QPointF(1, 2));
}

void SessionAppearanceTest::clearedContentOps_stripsOrientAndGrade()
{
    WorkspaceItemState st;
    st.contentQuarterTurns = 1;
    st.hasCrop = true;
    st.cropRect = QRect(1, 1, 2, 2);
    st.colorAdjust.brightness = 5;
    st.pos = QPointF(3, 4);

    const WorkspaceItemState out = SessionAppearance::clearedContentOps(st);
    QCOMPARE(out.contentQuarterTurns, 0);
    QVERIFY(!out.hasCrop);
    QVERIFY(out.colorAdjust.isIdentity());
    QCOMPARE(out.pos, QPointF(3, 4));
}

QTEST_MAIN(SessionAppearanceTest)
#include "sessionappearance_test.moc"
