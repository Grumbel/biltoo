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
    // Stage 2: Placement ↔ WorkspaceItemState bridge
    void placementFromState_roundTrip();
    void applyPlacementToState_preservesNonPose();
    void sparseWrite_stampsSessionIdOnDto();
    void setPathState_stripsCropWhenBound();
    // Stage 2 residual: durable presence (fat | sparse)
    void hasDurableAppearance_emptyAndAfterSparse();
    void hasDurableAppearance_fatOnlyCrop();
    void appearanceValue_sparseCropWinsOverStaleFat();
    // Stage 4b readiness: sparse survives fat removal (dual-write lag)
    void hasDurableAppearance_sparseOnlyAfterFatRemoved();
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

    QVERIFY(world.hasAppearance(11));
    const WorkspaceItemState got = world.appearanceValue(11);
    QVERIFY(got.hasCrop);
    QCOMPARE(got.sessionId, SessionImageId(11));
    QCOMPARE(got.cropRect, QRect(2, 4, 60, 40));
    QCOMPARE(store.get(11)->cropRect, QRect(2, 4, 60, 40));
    QCOMPARE(store.get(11)->sessionId, SessionImageId(11));

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

    QVERIFY(world.hasAppearance(5));
    QVERIFY(world.getPathState(QStringLiteral("/dup.png")));
    QCOMPARE(world.getPathState(QStringLiteral("/dup.png"))->pos, QPointF(100, 200));
    QCOMPARE(world.appearanceValue(5).cropRect, QRect(1, 1, 10, 10));
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
    // Stage 4b: setCrop writes sparse only; fat DTO stays untouched.
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::Crop c;
    c.rect = QRect(5, 5, 40, 30);
    c.sourceSize = QSize(200, 100);
    world.setCrop(9, c);

    QVERIFY(world.hasCrop(9));
    QCOMPARE(world.crop(9).rect, QRect(5, 5, 40, 30));
    QCOMPARE(world.crop(9).sourceSize, QSize(200, 100));
    QCOMPARE(store.get(9), nullptr); // no dual-write
    const WorkspaceItemState v = world.appearanceValue(9);
    QVERIFY(v.hasCrop);
    QCOMPARE(v.cropRect, QRect(5, 5, 40, 30));
    QCOMPARE(v.sessionId, SessionImageId(9));
}

void ItemWorldTest::setCrop_clearRemovesPresence()
{
    ItemWorld world;

    ItemComponents::Crop c;
    c.rect = QRect(1, 1, 10, 10);
    world.setCrop(2, c);
    QCOMPARE(world.cropCount(), 1);

    world.setCrop(2, ItemComponents::Crop{});
    QVERIFY(!world.hasCrop(2));
    QCOMPARE(world.cropCount(), 0);
}

void ItemWorldTest::attention_setAndClear()
{
    ItemWorld world;

    ItemComponents::Attention a;
    a.points = {QPointF(0.2, 0.3), QPointF(0.8, 0.7)};
    world.setAttention(4, a);

    QVERIFY(world.hasAttention(4));
    QCOMPARE(world.attention(4).points.size(), 2);
    QCOMPARE(world.appearanceValue(4).attentionNorm, QPointF(0.2, 0.3));

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
    // Stage 4b: fat-only rows are not authoritative; sparse table is required.
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(3, 3, 20, 20);
    store.set(8, st);
    QCOMPARE(world.cropCount(), 0);
    QVERIFY(world.crop(8).isEmpty());
    QVERIFY(!world.hasCrop(8));
    // appearanceValue ignores fat-only crop.
    QVERIFY(!world.appearanceValue(8).hasCrop);
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
    ItemWorld world;

    ItemComponents::ContentBake b;
    b.quarterTurns = 2;
    b.hFlip = true;
    world.setContentBake(6, b);

    QVERIFY(world.hasContentBake(6));
    QCOMPARE(world.contentBake(6).quarterTurns, 2);
    QVERIFY(world.contentBake(6).hFlip);
    QCOMPARE(world.appearanceValue(6).contentQuarterTurns, 2);

    world.setContentBake(6, ItemComponents::ContentBake{});
    QVERIFY(!world.hasContentBake(6));
    QCOMPARE(world.contentBakeCount(), 0);
}

void ItemWorldTest::color_setAndClear()
{
    ItemWorld world;

    ItemComponents::Color c;
    c.grade.brightness = -10;
    c.grade.contrast = 110;
    world.setColor(12, c);

    QVERIFY(world.hasColor(12));
    QCOMPARE(world.color(12).grade.brightness, -10);
    QCOMPARE(world.appearanceValue(12).colorAdjust.brightness, -10);

    world.setColor(12, ItemComponents::Color{});
    QVERIFY(!world.hasColor(12));
    QCOMPARE(world.colorCount(), 0);
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
    // Stage 4b: setPlacement sparse-only; appearanceValue carries pose.
    ItemWorld world;

    ItemComponents::Placement pl;
    pl.pos = QPointF(1, 2);
    pl.scale = 1.5;
    world.setPlacement(21, pl);

    QVERIFY(world.hasPlacement(21));
    QCOMPARE(world.placement(21).pos, QPointF(1, 2));
    QCOMPARE(world.appearanceValue(21).pos, QPointF(1, 2));
    QCOMPARE(world.appearanceValue(21).scale, 1.5);
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

void ItemWorldTest::placementFromState_roundTrip()
{
    WorkspaceItemState s;
    s.pos = QPointF(3, 4);
    s.scale = 1.25;
    s.scaleY = 0.8;
    s.shear = 0.15;
    s.rotation = 33.0;
    s.opacity = 0.7;
    s.z = 2.5;
    s.hFlip = true;
    s.vFlip = false;
    s.hasCrop = true;
    s.cropRect = QRect(1, 2, 10, 20);

    const ItemComponents::Placement pl = ItemComponents::placementFromState(s);
    QCOMPARE(pl.pos, QPointF(3, 4));
    QCOMPARE(pl.scale, 1.25);
    QCOMPARE(pl.scaleY, 0.8);
    QCOMPARE(pl.shear, 0.15);
    QCOMPARE(pl.rotation, 33.0);
    QCOMPARE(pl.opacity, 0.7);
    QCOMPARE(pl.z, 2.5);
    QVERIFY(pl.hFlip);
    QVERIFY(!pl.vFlip);

    WorkspaceItemState out;
    out.hasCrop = true;
    out.cropRect = QRect(9, 9, 1, 1);
    ItemComponents::applyPlacementToState(out, pl);
    QCOMPARE(out.pos, pl.pos);
    QCOMPARE(out.scale, pl.scale);
    QCOMPARE(out.scaleY, pl.scaleY);
    QCOMPARE(out.shear, pl.shear);
    QCOMPARE(out.rotation, pl.rotation);
    QCOMPARE(out.opacity, pl.opacity);
    QCOMPARE(out.z, pl.z);
    QCOMPARE(out.hFlip, pl.hFlip);
    QCOMPARE(out.vFlip, pl.vFlip);
    // Non-pose fields untouched by applyPlacementToState
    QVERIFY(out.hasCrop);
    QCOMPARE(out.cropRect, QRect(9, 9, 1, 1));
}

void ItemWorldTest::applyPlacementToState_preservesNonPose()
{
    WorkspaceItemState s;
    s.path = QStringLiteral("/x.png");
    s.sessionId = 42;
    s.contentQuarterTurns = 1;
    s.contentHFlip = true;
    s.hasCrop = true;
    s.cropRect = QRect(0, 0, 5, 5);
    s.colorAdjust.saturation = 40;

    ItemComponents::Placement pl;
    pl.pos = QPointF(10, 20);
    pl.scale = 2.0;
    ItemComponents::applyPlacementToState(s, pl);

    QCOMPARE(s.pos, QPointF(10, 20));
    QCOMPARE(s.scale, 2.0);
    QCOMPARE(s.path, QStringLiteral("/x.png"));
    QCOMPARE(s.sessionId, SessionImageId(42));
    QCOMPARE(s.contentQuarterTurns, 1);
    QVERIFY(s.contentHFlip);
    QVERIFY(s.hasCrop);
    QCOMPARE(s.cropRect, QRect(0, 0, 5, 5));
    QCOMPARE(s.colorAdjust.saturation, 40);
}


void ItemWorldTest::hasDurableAppearance_emptyAndAfterSparse()
{
    ItemWorld world;

    QVERIFY(!world.hasDurableAppearance(1));

    ItemComponents::Color c;
    c.grade.brightness = 12;
    world.setColor(1, c);
    // Stage 4b: sparse Color alone is durable; fat may be empty.
    QVERIFY(world.hasDurableAppearance(1));
    QVERIFY(world.hasColor(1));
    QVERIFY(!world.hasAppearance(1)); // fat not dual-written

    world.clearAppearance();
    QVERIFY(!world.hasDurableAppearance(1));

    ItemComponents::Placement pl;
    pl.pos = QPointF(5, 6);
    world.setPlacement(2, pl);
    QVERIFY(world.hasDurableAppearance(2));
    QVERIFY(world.hasPlacement(2));
}

void ItemWorldTest::hasDurableAppearance_fatOnlyCrop()
{
    // Stage 4b: fat-only rows without sparse components are not durable content.
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(4, 5, 30, 20);
    store.set(9, st);
    QCOMPARE(world.cropCount(), 0);
    QVERIFY(world.hasAppearance(9)); // fat row exists
    QVERIFY(!world.hasDurableAppearance(9)); // no sparse tables
    QVERIFY(!world.appearanceValue(9).hasCrop);
}

void ItemWorldTest::appearanceValue_sparseCropWinsOverStaleFat()
{
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    WorkspaceItemState fat;
    fat.hasCrop = true;
    fat.cropRect = QRect(0, 0, 10, 10);
    fat.sessionId = 11;
    world.setAppearance(11, fat);

    ItemComponents::Crop sparse;
    sparse.rect = QRect(2, 3, 40, 50);
    sparse.sourceSize = QSize(100, 100);
    world.setCrop(11, sparse);

    const WorkspaceItemState v = world.appearanceValue(11);
    QVERIFY(v.hasCrop);
    QCOMPARE(v.cropRect, QRect(2, 3, 40, 50));
    QCOMPARE(v.cropSourceSize, QSize(100, 100));
    QVERIFY(world.hasDurableAppearance(11));
}


void ItemWorldTest::hasDurableAppearance_sparseOnlyAfterFatRemoved()
{
    // Stage 4b readiness: store-read must not require the fat dual-write mirror.
    SessionAppearanceStore store;
    ItemWorld world;
    world.bindAppearance(&store);

    ItemComponents::Crop c;
    c.rect = QRect(1, 2, 30, 40);
    c.sourceSize = QSize(100, 80);
    world.setCrop(11, c);
    QVERIFY(!world.hasAppearance(11)); // no dual-write
    QVERIFY(world.hasDurableAppearance(11));
    QVERIFY(world.hasCrop(11));

    // Fat never written; sparse remains the authority.
    QVERIFY(world.hasCrop(11));
    QVERIFY(world.hasDurableAppearance(11));

    const WorkspaceItemState v = world.appearanceValue(11);
    QVERIFY(v.hasCrop);
    QCOMPARE(v.cropRect, QRect(1, 2, 30, 40));
    QCOMPARE(v.sessionId, SessionImageId(11));
}

QTEST_MAIN(ItemWorldTest)

void ItemWorldTest::sparseWrite_stampsSessionIdOnDto()
{
    // Stage 4b: sparse writes stamp sessionId on assembled appearanceValue only.
    ItemWorld world;

    ItemComponents::Color c;
    c.grade.brightness = 10;
    world.setColor(42, c);

    QVERIFY(!world.hasAppearance(42)); // no fat dual-write
    QCOMPARE(world.appearanceValue(42).sessionId, SessionImageId(42));
    QVERIFY(world.hasColor(42));

    ItemComponents::Placement pl;
    pl.pos = QPointF(10, 20);
    world.setPlacement(42, pl);
    QCOMPARE(world.appearanceValue(42).sessionId, SessionImageId(42));
    QCOMPARE(world.appearanceValue(42).pos, QPointF(10, 20));
}

void ItemWorldTest::setPathState_stripsCropWhenBound()
{
    PathItemStateBook paths;
    ItemWorld world;
    world.bindPathBook(&paths);

    WorkspaceItemState bound;
    bound.sessionId = 7;
    bound.path = QStringLiteral("/dup.png");
    bound.hasCrop = true;
    bound.cropRect = QRect(1, 2, 30, 40);
    bound.contentHFlip = true;
    world.setPathState(QStringLiteral("/dup.png"), bound);

    const WorkspaceItemState *got = world.getPathState(QStringLiteral("/dup.png"));
    QVERIFY(got);
    QVERIFY(!got->hasCrop);
    QVERIFY(got->cropRect.isEmpty());
    QVERIFY(got->contentHFlip); // orient path hint may remain

    WorkspaceItemState unbound;
    unbound.path = QStringLiteral("/solo.png");
    unbound.hasCrop = true;
    unbound.cropRect = QRect(5, 5, 10, 10);
    world.setPathState(QStringLiteral("/solo.png"), unbound);
    const WorkspaceItemState *u = world.getPathState(QStringLiteral("/solo.png"));
    QVERIFY(u);
    QVERIFY(u->hasCrop);
    QCOMPARE(u->cropRect, QRect(5, 5, 10, 10));
}

#include "itemworld_test.moc"
