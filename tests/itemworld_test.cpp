// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Phase 7: ItemWorld facade (Stage 0) + Crop/Attention components (Stage 1).
 */

#include "itemworld.h"
#include "itemcomponents.h"

#include <QtTest/QtTest>

class ItemWorldTest : public QObject
{
    Q_OBJECT
private slots:
    void unbound_gettersAreSafe();
    void appearance_roundTripById();
    void pathBook_independentOfAppearance();
    void sizeBook_noteDefinitive();
    void bind_pointsAtSameStore();
    // Stage 1
    void cropFromState_emptyWhenNoCrop();
    void crop_setAppearanceDualWritesTable();
    void setCrop_updatesDtoAndTable();
    void setCrop_clearRemovesPresence();
    void attention_setAndClear();
    void removeAppearance_clearsComponents();
    void crop_fallbackWhenDtoWrittenDirectly();
    void clearAppearance_clearsDtoAndTables();
    void contentBake_setAndClear();
    void color_setAndClear();
    void setAppearance_dualWritesBakeAndColor();
    void placement_setAppearanceDualWrites();
    void setPlacement_updatesDto();
    void placementNearlyEqual_detectsNoOp();
};



void ItemWorldTest::unbound_gettersAreSafe()
{
    ItemWorld world;
    QVERIFY(!world.hasAppearanceBound());
    QVERIFY(!world.hasPathBookBound());
    QVERIFY(!world.hasSizeBookBound());
    QCOMPARE(world.getAppearance(1), nullptr);
    QCOMPARE(world.getPathState(QStringLiteral("/a.png")), nullptr);
    QCOMPARE(world.knownSize(QStringLiteral("/a.png")), QSize());
    QVERIFY(world.crop(1).isEmpty());
    QVERIFY(world.attention(1).isEmpty());
}

void ItemWorldTest::appearance_roundTripById()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(2, 4, 60, 40);
    world.setAppearance(11, st);

    const WorkspaceItemState *got = world.getAppearance(11);
    QVERIFY(got);
    QVERIFY(got->hasCrop);
    QCOMPARE(got->cropRect, QRect(2, 4, 60, 40));
    QCOMPARE(store.get(11)->cropRect, QRect(2, 4, 60, 40));

    world.removeAppearance(11);
    QVERIFY(!store.contains(11));
}

void ItemWorldTest::pathBook_independentOfAppearance()
{
    SessionAppearanceStore store;
    PathItemStateBook paths;
    ItemWorld world;
    world.bindAppearance(&store);
    world.bindPathBook(&paths);

    WorkspaceItemState app;
    app.hasCrop = true;
    app.cropRect = QRect(1, 1, 10, 10);
    world.setAppearance(5, app);

    WorkspaceItemState place;
    place.pos = QPointF(100, 200);
    place.scale = 1.5;
    world.setPathState(QStringLiteral("/dup.png"), place);

    QVERIFY(world.getAppearance(5));
    QVERIFY(world.getPathState(QStringLiteral("/dup.png")));
    QCOMPARE(world.getPathState(QStringLiteral("/dup.png"))->pos, QPointF(100, 200));
    QCOMPARE(world.getAppearance(5)->cropRect, QRect(1, 1, 10, 10));
}

void ItemWorldTest::sizeBook_noteDefinitive()
{
    ImageSizeBook sizes;
    ItemWorld world;
    world.bindSizeBook(&sizes);

    QVERIFY(world.noteDefinitiveSize(QStringLiteral("/a.png"), QSize(800, 600)));
    QCOMPARE(world.knownSize(QStringLiteral("/a.png")), QSize(800, 600));
    QCOMPARE(sizes.known(QStringLiteral("/a.png")), QSize(800, 600));
}

void ItemWorldTest::bind_pointsAtSameStore()
{
    SessionAppearanceStore store;
    PathItemStateBook paths;
    ImageSizeBook sizes;
    ItemWorld world;
    world.bindAppearance(&store);
    world.bindPathBook(&paths);
    world.bindSizeBook(&sizes);

    QCOMPARE(&world.appearance(), &store);
    QCOMPARE(&world.pathBook(), &paths);
    QCOMPARE(&world.sizeBook(), &sizes);
}

void ItemWorldTest::cropFromState_emptyWhenNoCrop()
{
    WorkspaceItemState s;
    QVERIFY(ItemComponents::cropFromState(s).isEmpty());
    s.hasCrop = true;
    s.cropRect = QRect(); // empty rect still empty component
    QVERIFY(ItemComponents::cropFromState(s).isEmpty());
}

void ItemWorldTest::crop_setAppearanceDualWritesTable()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(10, 20, 100, 80);
    st.cropSourceSize = QSize(800, 600);
    st.cropRotation = 15.0;
    world.setAppearance(3, st);

    QCOMPARE(world.cropCount(), 1);
    QVERIFY(world.hasCrop(3));
    const ItemComponents::Crop c = world.crop(3);
    QCOMPARE(c.rect, QRect(10, 20, 100, 80));
    QCOMPARE(c.sourceSize, QSize(800, 600));
    QCOMPARE(c.rotation, 15.0);
}

void ItemWorldTest::setCrop_updatesDtoAndTable()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::Crop c;
    c.rect = QRect(5, 5, 40, 30);
    c.sourceSize = QSize(200, 100);
    world.setCrop(9, c);

    QVERIFY(world.hasCrop(9));
    QCOMPARE(world.crop(9).rect, QRect(5, 5, 40, 30));
    const WorkspaceItemState *s = store.get(9);
    QVERIFY(s);
    QVERIFY(s->hasCrop);
    QCOMPARE(s->cropRect, QRect(5, 5, 40, 30));
    QCOMPARE(s->cropSourceSize, QSize(200, 100));
}

void ItemWorldTest::setCrop_clearRemovesPresence()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::Crop c;
    c.rect = QRect(1, 1, 10, 10);
    world.setCrop(2, c);
    QCOMPARE(world.cropCount(), 1);

    world.setCrop(2, ItemComponents::Crop{});
    QVERIFY(!world.hasCrop(2));
    QCOMPARE(world.cropCount(), 0);
    const WorkspaceItemState *s = store.get(2);
    QVERIFY(s);
    QVERIFY(!s->hasCrop);
}

void ItemWorldTest::attention_setAndClear()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::Attention a;
    a.points = {QPointF(0.2, 0.3), QPointF(0.8, 0.7)};
    world.setAttention(4, a);

    QVERIFY(world.hasAttention(4));
    QCOMPARE(world.attention(4).points.size(), 2);
    const WorkspaceItemState *s = store.get(4);
    QVERIFY(s);
    QVERIFY(s->hasAttention);
    QCOMPARE(s->attentionNorm, QPointF(0.2, 0.3));

    world.setAttention(4, ItemComponents::Attention{});
    QVERIFY(!world.hasAttention(4));
    QCOMPARE(world.attentionCount(), 0);
}

void ItemWorldTest::removeAppearance_clearsComponents()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(1, 1, 5, 5);
    st.hasAttention = true;
    st.attentionPoints = {QPointF(0.5, 0.5)};
    world.setAppearance(7, st);
    QCOMPARE(world.cropCount(), 1);
    QCOMPARE(world.attentionCount(), 1);

    world.removeAppearance(7);
    QCOMPARE(world.cropCount(), 0);
    QCOMPARE(world.attentionCount(), 0);
    QVERIFY(!store.contains(7));
}

void ItemWorldTest::crop_fallbackWhenDtoWrittenDirectly()
{
    // Legacy path: DTO written without ItemWorld::setAppearance.
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(3, 3, 20, 20);
    store.set(8, st);
    QCOMPARE(world.cropCount(), 0); // table not dual-written
    // Fallback extract still works.
    QCOMPARE(world.crop(8).rect, QRect(3, 3, 20, 20));
}


void ItemWorldTest::clearAppearance_clearsDtoAndTables()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(1, 1, 8, 8);
    st.attentionPoints = {QPointF(0.1, 0.2)};
    st.hasAttention = true;
    st.contentHFlip = true;
    st.contentQuarterTurns = 1;
    st.colorAdjust.brightness = 20;
    st.pos = QPointF(9, 9);
    world.setAppearance(1, st);
    QCOMPARE(world.cropCount(), 1);
    QCOMPARE(world.attentionCount(), 1);
    QCOMPARE(world.contentBakeCount(), 1);
    QCOMPARE(world.colorCount(), 1);
    QCOMPARE(world.placementCount(), 1);

    world.clearAppearance();
    QVERIFY(!store.contains(1));
    QCOMPARE(world.cropCount(), 0);
    QCOMPARE(world.attentionCount(), 0);
    QCOMPARE(world.contentBakeCount(), 0);
    QCOMPARE(world.colorCount(), 0);
    QCOMPARE(world.placementCount(), 0);
}

void ItemWorldTest::contentBake_setAndClear()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::ContentBake b;
    b.quarterTurns = 2;
    b.hFlip = true;
    world.setContentBake(6, b);

    QVERIFY(world.hasContentBake(6));
    QCOMPARE(world.contentBake(6).quarterTurns, 2);
    QVERIFY(world.contentBake(6).hFlip);
    const WorkspaceItemState *s = store.get(6);
    QVERIFY(s);
    QCOMPARE(s->contentQuarterTurns, 2);
    QVERIFY(s->contentHFlip);

    world.setContentBake(6, ItemComponents::ContentBake{});
    QVERIFY(!world.hasContentBake(6));
    QCOMPARE(world.contentBakeCount(), 0);
    QVERIFY(store.get(6)->contentQuarterTurns == 0);
    QVERIFY(!store.get(6)->contentHFlip);
}

void ItemWorldTest::color_setAndClear()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::Color c;
    c.grade.brightness = -10;
    c.grade.contrast = 110;
    world.setColor(12, c);

    QVERIFY(world.hasColor(12));
    QCOMPARE(world.color(12).grade.brightness, -10);
    QCOMPARE(store.get(12)->colorAdjust.brightness, -10);

    world.setColor(12, ItemComponents::Color{});
    QVERIFY(!world.hasColor(12));
    QCOMPARE(world.colorCount(), 0);
    QVERIFY(store.get(12)->colorAdjust.isIdentity());
}

void ItemWorldTest::setAppearance_dualWritesBakeAndColor()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.contentVFlip = true;
    st.colorAdjust.saturation = 80;
    world.setAppearance(15, st);

    QVERIFY(world.hasContentBake(15));
    QVERIFY(world.contentBake(15).vFlip);
    QVERIFY(world.hasColor(15));
    QCOMPARE(world.color(15).grade.saturation, 80);
}


void ItemWorldTest::placement_setAppearanceDualWrites()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.pos = QPointF(12, 34);
    st.scale = 2.0;
    st.rotation = 45.0;
    st.hFlip = true;
    world.setAppearance(20, st);

    QCOMPARE(world.placementCount(), 1);
    QVERIFY(world.hasPlacement(20));
    QCOMPARE(world.placement(20).pos, QPointF(12, 34));
    QCOMPARE(world.placement(20).scale, 2.0);
    QVERIFY(world.placement(20).hFlip);
}

void ItemWorldTest::setPlacement_updatesDto()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::Placement pl;
    pl.pos = QPointF(5, 6);
    pl.scaleY = 1.5;
    pl.shear = 0.25;
    world.setPlacement(21, pl);

    QVERIFY(world.hasPlacement(21));
    const WorkspaceItemState *s = store.get(21);
    QVERIFY(s);
    QCOMPARE(s->pos, QPointF(5, 6));
    QCOMPARE(s->scaleY, 1.5);
    QCOMPARE(s->shear, 0.25);
}


void ItemWorldTest::placementNearlyEqual_detectsNoOp()
{
    ItemComponents::Placement a;
    a.pos = QPointF(1, 2);
    a.scale = 1.5;
    a.rotation = 10.0;
    ItemComponents::Placement b = a;
    QVERIFY(ItemComponents::placementNearlyEqual(a, b));
    b.pos += QPointF(0.001, 0);
    QVERIFY(!ItemComponents::placementNearlyEqual(a, b));
}

QTEST_MAIN(ItemWorldTest)
#include "itemworld_test.moc"
