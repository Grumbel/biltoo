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
#include "itemcomponents.h"
#include "thumtoocache.h"

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
    // Workspace pose-only snapshot contract (2212–2214)
    void clearedContentOps_keepsFullPlacement();
    void poseMerge_snapshotOntoStoreContent();
    // Path-XDG fill (2228)
    void applyStoredContentAppearance_orientAndGrade();
    void applyStoredContentAppearance_orientOnlySkipsGrade();
    void fillStoredContentAppearance_roundTripOrient();
    void fillStoredContentAppearance_boundSkipsCrop();
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

void SessionAppearanceTest::clearedContentOps_keepsFullPlacement()
{
    // Workspace leave snapshot (2213): bound slots are clearedContentOps(freeze).
    WorkspaceItemState st;
    st.contentQuarterTurns = 2;
    st.contentHFlip = true;
    st.hasCrop = true;
    st.cropRect = QRect(2, 2, 8, 8);
    st.colorAdjust.saturation = 120;
    st.pos = QPointF(10, 20);
    st.scale = 1.75;
    st.scaleY = 1.25;
    st.shear = 0.15;
    st.rotation = 12.5;
    st.opacity = 0.8;
    st.z = 3.0;
    st.hFlip = true;
    st.vFlip = true;
    st.sessionId = 42;
    st.path = QStringLiteral("/a.jpg");
    st.sessionIndex = 3;

    const WorkspaceItemState out = SessionAppearance::clearedContentOps(st);
    QCOMPARE(out.contentQuarterTurns, 0);
    QVERIFY(!out.contentHFlip);
    QVERIFY(!out.hasCrop);
    QVERIFY(out.colorAdjust.isIdentity());
    QCOMPARE(out.pos, QPointF(10, 20));
    QCOMPARE(out.scale, 1.75);
    QCOMPARE(out.scaleY, 1.25);
    QCOMPARE(out.shear, 0.15);
    QCOMPARE(out.rotation, 12.5);
    QCOMPARE(out.opacity, 0.8);
    QCOMPARE(out.z, 3.0);
    QVERIFY(out.hFlip);
    QVERIFY(out.vFlip);
    QCOMPARE(out.sessionId, SessionImageId(42));
    QCOMPARE(out.path, QStringLiteral("/a.jpg"));
    QCOMPARE(out.sessionIndex, 3);
}

void SessionAppearanceTest::poseMerge_snapshotOntoStoreContent()
{
    // restore / completeLoadRestore (2214): ItemWorld content + snapshot pose.
    WorkspaceItemState store;
    store.contentQuarterTurns = 1;
    store.hasCrop = true;
    store.cropRect = QRect(0, 0, 50, 40);
    store.colorAdjust.brightness = 8;
    store.pos = QPointF(0, 0);
    store.scale = 1.0;

    WorkspaceItemState snapshot;
    snapshot.pos = QPointF(7, 9);
    snapshot.scale = 2.0;
    snapshot.scaleY = 1.5;
    snapshot.shear = 0.05;
    snapshot.rotation = 30.0;
    snapshot.opacity = 0.9;
    snapshot.z = 1.0;
    snapshot.hFlip = true;
    snapshot.vFlip = false;

    ItemComponents::applyPlacementToState(
        store, ItemComponents::placementFromState(snapshot));

    QCOMPARE(store.pos, QPointF(7, 9));
    QCOMPARE(store.scale, 2.0);
    QCOMPARE(store.scaleY, 1.5);
    QCOMPARE(store.shear, 0.05);
    QCOMPARE(store.rotation, 30.0);
    QCOMPARE(store.opacity, 0.9);
    QCOMPARE(store.z, 1.0);
    QVERIFY(store.hFlip);
    QVERIFY(!store.vFlip);
    // Content from store preserved.
    QCOMPARE(store.contentQuarterTurns, 1);
    QVERIFY(store.hasCrop);
    QCOMPARE(store.cropRect, QRect(0, 0, 50, 40));
    QCOMPARE(store.colorAdjust.brightness, 8);
}


void SessionAppearanceTest::applyStoredContentAppearance_orientAndGrade()
{
    ThumtooCache::StoredContentAppearance stored;
    stored.contentQuarterTurns = 1;
    stored.contentHFlip = true;
    stored.hasCrop = true;
    stored.cropRect = QRect(1, 2, 3, 4);
    stored.cropSourceSize = QSize(100, 80);
    stored.hasGrade = true;
    stored.gradeBrightness = 10;
    stored.gradeContrast = 0; // identity 100
    stored.gradeGamma = 150; // 1.5

    WorkspaceItemState st;
    st.pos = QPointF(5, 6);
    SessionAppearance::applyStoredContentAppearance(&st, stored);
    QCOMPARE(st.contentQuarterTurns, 1);
    QVERIFY(st.contentHFlip);
    QVERIFY(st.hasCrop);
    QCOMPARE(st.cropRect, QRect(1, 2, 3, 4));
    QCOMPARE(st.colorAdjust.brightness, 10);
    QCOMPARE(st.colorAdjust.contrast, 100);
    QCOMPARE(st.colorAdjust.gamma, 1.5);
    QCOMPARE(st.pos, QPointF(5, 6)); // other fields preserved
}

void SessionAppearanceTest::applyStoredContentAppearance_orientOnlySkipsGrade()
{
    ThumtooCache::StoredContentAppearance stored;
    stored.contentVFlip = true;
    stored.hasGrade = true;
    stored.gradeBrightness = 20;

    WorkspaceItemState st;
    st.colorAdjust.brightness = 3;
    SessionAppearance::applyStoredContentAppearance(&st, stored, false);
    QVERIFY(st.contentVFlip);
    QCOMPARE(st.colorAdjust.brightness, 3); // grade not applied
}


void SessionAppearanceTest::fillStoredContentAppearance_roundTripOrient()
{
    WorkspaceItemState st;
    st.contentQuarterTurns = 2;
    st.contentHFlip = true;
    st.colorAdjust.brightness = 7;
    ThumtooCache::StoredContentAppearance stored;
    QVERIFY(SessionAppearance::fillStoredContentAppearance(&stored, st, true));
    QCOMPARE(stored.contentQuarterTurns, 2);
    QVERIFY(stored.contentHFlip);
    QVERIFY(stored.hasGrade);
    QCOMPARE(stored.gradeBrightness, 7);

    WorkspaceItemState back;
    SessionAppearance::applyStoredContentAppearance(&back, stored);
    QCOMPARE(back.contentQuarterTurns, 2);
    QVERIFY(back.contentHFlip);
    QCOMPARE(back.colorAdjust.brightness, 7);
}

void SessionAppearanceTest::fillStoredContentAppearance_boundSkipsCrop()
{
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(0, 0, 10, 10);
    st.contentQuarterTurns = 1;
    ThumtooCache::StoredContentAppearance stored;
    QVERIFY(SessionAppearance::fillStoredContentAppearance(&stored, st, false));
    QVERIFY(!stored.hasCrop);
    QCOMPARE(stored.contentQuarterTurns, 1);
}

QTEST_MAIN(SessionAppearanceTest)
#include "sessionappearance_test.moc"
