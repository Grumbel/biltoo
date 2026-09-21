#include "sessionseedbook.h"
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
    void crop_setAppearanceWritesTable();
    void setCrop_updatesSparseTable();
    void setCrop_clearRemovesPresence();
    void attention_setAndClear();
    void removeAppearance_clearsComponents();
    void crop_fallbackWhenDtoWrittenDirectly();
    void clearAppearance_clearsSparseTables();
    void contentBake_setAndClear();
    void color_setAndClear();
    void setAppearance_writesBakeAndColor();
    void placement_setAppearanceWritesPose();
    void setPlacement_updatesDto();
    void placementNearlyEqual_detectsNoOp();
    // Stage 2: Placement ↔ WorkspaceItemState bridge
    void placementFromState_roundTrip();
    void applyPlacementToState_preservesNonPose();
    void sparseWrite_stampsSessionIdOnDto();
    void setPathState_stripsContentWhenBound();
    // Stage 2 residual: durable presence (fat | sparse)
    void hasDurableAppearance_emptyAndAfterSparse();
    void hasDurableAppearance_seedOnlyNotDurable();
    void appearanceValue_sparseCropWins();
    // Stage 4b readiness: sparse survives fat removal (dual-write lag)
    void hasDurableAppearance_sparseOnly();
    void setAppearance_preservesExistingPlacement();
    void mergeContentFromState_doesNotClearSiblings();
    void clearContentComponents_keepsAttentionAndPlacement();
};



void ItemWorldTest::unbound_gettersAreSafe()
{
    ItemWorld world;
    QVERIFY(!world.hasPathBookBound());
    QVERIFY(!world.hasSizeBookBound());
    QCOMPARE(world.getPathState(QStringLiteral("/a.png")), nullptr);
    QCOMPARE(world.knownSize(QStringLiteral("/a.png")), QSize());
    QVERIFY(world.crop(1).isEmpty());
    QVERIFY(world.attention(1).isEmpty());
    QVERIFY(!world.hasDurableAppearance(1));
}

void ItemWorldTest::appearance_roundTripById()
{
    ItemWorld world;

    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(2, 4, 60, 40);
    world.setAppearance(11, st);

    QVERIFY(world.hasAppearance(11)); // durable sparse
    QVERIFY(world.hasDurableAppearance(11));
    const WorkspaceItemState got = world.appearanceValue(11);
    QVERIFY(got.hasCrop);
    QCOMPARE(got.sessionId, SessionImageId(11));
    QCOMPARE(got.cropRect, QRect(2, 4, 60, 40));

    world.removeAppearance(11);
    QVERIFY(!world.hasDurableAppearance(11));
}

void ItemWorldTest::pathBook_independentOfAppearance()
{
    PathItemStateBook paths;
    ItemWorld world;
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
    PathItemStateBook paths;
    ImageSizeBook sizes;
    ItemWorld world;
    world.bindPathBook(&paths);
    world.bindSizeBook(&sizes);

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

void ItemWorldTest::crop_setAppearanceWritesTable()
{
    ItemWorld world;

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

void ItemWorldTest::setCrop_updatesSparseTable()
{
    ItemWorld world;

    ItemComponents::Crop c;
    c.rect = QRect(5, 5, 40, 30);
    c.sourceSize = QSize(200, 100);
    world.setCrop(9, c);

    QVERIFY(world.hasCrop(9));
    QCOMPARE(world.crop(9).rect, QRect(5, 5, 40, 30));
    QCOMPARE(world.crop(9).sourceSize, QSize(200, 100));
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
    ItemWorld world;

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
    QVERIFY(!world.hasDurableAppearance(7));
}

void ItemWorldTest::crop_fallbackWhenDtoWrittenDirectly()
{
    // Seed book is not content; sparse empty until setAppearance.
    SessionSeedBook seeds;
    ItemWorld world;
    seeds.markSeedAttempted(8);
    QCOMPARE(world.cropCount(), 0);
    QVERIFY(world.crop(8).isEmpty());
    QVERIFY(!world.hasCrop(8));
    QVERIFY(!world.appearanceValue(8).hasCrop);
    QVERIFY(seeds.seedAttempted(8));
}

void ItemWorldTest::clearAppearance_clearsSparseTables()
{
    ItemWorld world;

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

void ItemWorldTest::setAppearance_writesBakeAndColor()
{
    ItemWorld world;

    WorkspaceItemState st;
    st.contentVFlip = true;
    st.colorAdjust.saturation = 80;
    world.setAppearance(15, st);

    QVERIFY(world.hasContentBake(15));
    QVERIFY(world.contentBake(15).vFlip);
    QVERIFY(world.hasColor(15));
    QCOMPARE(world.color(15).grade.saturation, 80);
}


void ItemWorldTest::placement_setAppearanceWritesPose()
{
    ItemWorld world;

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
    // Stage 4b residual: sparse Color alone is durable; hasAppearance aliases it.
    QVERIFY(world.hasDurableAppearance(1));
    QVERIFY(world.hasColor(1));
    QVERIFY(world.hasAppearance(1));

    world.clearAppearance();
    QVERIFY(!world.hasDurableAppearance(1));

    ItemComponents::Placement pl;
    pl.pos = QPointF(5, 6);
    world.setPlacement(2, pl);
    QVERIFY(world.hasDurableAppearance(2));
    QVERIFY(world.hasPlacement(2));
}

void ItemWorldTest::hasDurableAppearance_seedOnlyNotDurable()
{
    // Seed flag alone is not durable content appearance.
    SessionSeedBook seeds;
    ItemWorld world;
    seeds.markSeedAttempted(9);
    QCOMPARE(world.cropCount(), 0);
    QVERIFY(!world.hasAppearance(9));
    QVERIFY(!world.hasDurableAppearance(9));
    QVERIFY(!world.appearanceValue(9).hasCrop);
    QVERIFY(seeds.seedAttempted(9));
}

void ItemWorldTest::appearanceValue_sparseCropWins()
{
    ItemWorld world;

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


void ItemWorldTest::hasDurableAppearance_sparseOnly()
{
    // Store-read uses sparse tables only.
    ItemWorld world;

    ItemComponents::Crop c;
    c.rect = QRect(1, 2, 30, 40);
    c.sourceSize = QSize(100, 80);
    world.setCrop(11, c);
    QVERIFY(world.hasAppearance(11)); // durable sparse
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


void ItemWorldTest::setAppearance_preservesExistingPlacement()
{
    ItemWorld world;
    ItemComponents::Placement pl;
    pl.pos = QPointF(40, 50);
    pl.scale = 1.5;
    world.setPlacement(7, pl);
    QVERIFY(world.hasPlacement(7));

    // Content-only setAppearance with identity pose must not wipe Workspace pose.
    WorkspaceItemState content;
    content.hasCrop = true;
    content.cropRect = QRect(1, 2, 30, 40);
    content.contentHFlip = true;
    world.setAppearance(7, content);

    QVERIFY(world.hasCrop(7));
    QCOMPARE(world.crop(7).rect, QRect(1, 2, 30, 40));
    QVERIFY(world.hasContentBake(7));
    QVERIFY(world.hasPlacement(7));
    QCOMPARE(world.placement(7).pos, QPointF(40, 50));
    QCOMPARE(world.placement(7).scale, 1.5);

    // Non-identity pose still writes.
    content.pos = QPointF(9, 8);
    content.scale = 2.0;
    world.setAppearance(7, content);
    QCOMPARE(world.placement(7).pos, QPointF(9, 8));
    QCOMPARE(world.placement(7).scale, 2.0);
}

void ItemWorldTest::sparseWrite_stampsSessionIdOnDto()
{
    // Stage 4b: sparse writes stamp sessionId on assembled appearanceValue only.
    ItemWorld world;

    ItemComponents::Color c;
    c.grade.brightness = 10;
    world.setColor(42, c);

    QVERIFY(world.hasAppearance(42)); // durable sparse
    QCOMPARE(world.appearanceValue(42).sessionId, SessionImageId(42));
    QVERIFY(world.hasColor(42));

    ItemComponents::Placement pl;
    pl.pos = QPointF(10, 20);
    world.setPlacement(42, pl);
    QCOMPARE(world.appearanceValue(42).sessionId, SessionImageId(42));
    QCOMPARE(world.appearanceValue(42).pos, QPointF(10, 20));
}

void ItemWorldTest::setPathState_stripsContentWhenBound()
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
    bound.contentQuarterTurns = 1;
    bound.colorAdjust.brightness = 12;
    world.setPathState(QStringLiteral("/dup.png"), bound);

    const WorkspaceItemState *got = world.getPathState(QStringLiteral("/dup.png"));
    QVERIFY(got);
    QVERIFY(!got->hasCrop);
    QVERIFY(got->cropRect.isEmpty());
    // Bound: all content stripped (IDENTITY — sparse/XDG own content).
    QVERIFY(!got->contentHFlip);
    QCOMPARE(got->contentQuarterTurns, 0);
    QVERIFY(got->colorAdjust.isIdentity());

    WorkspaceItemState unbound;
    unbound.path = QStringLiteral("/solo.png");
    unbound.hasCrop = true;
    unbound.cropRect = QRect(5, 5, 10, 10);
    unbound.contentHFlip = true;
    world.setPathState(QStringLiteral("/solo.png"), unbound);
    const WorkspaceItemState *u = world.getPathState(QStringLiteral("/solo.png"));
    QVERIFY(u);
    QVERIFY(u->hasCrop);
    QCOMPARE(u->cropRect, QRect(5, 5, 10, 10));
    QVERIFY(u->contentHFlip);
}


void ItemWorldTest::mergeContentFromState_doesNotClearSiblings()
{
    ItemWorld world;
    ItemComponents::Attention att;
    att.points = {QPointF(0.25, 0.75)};
    world.setAttention(3, att);
    ItemComponents::Placement pl;
    pl.pos = QPointF(11, 22);
    world.setPlacement(3, pl);

    WorkspaceItemState seed;
    seed.contentHFlip = true;
    seed.colorAdjust.brightness = 15;
    world.mergeContentFromState(3, seed);

    QVERIFY(world.hasContentBake(3));
    QVERIFY(world.contentBake(3).hFlip);
    QVERIFY(world.hasColor(3));
    QCOMPARE(world.color(3).grade.brightness, 15);
    // Siblings preserved
    QVERIFY(world.hasAttention(3));
    QCOMPARE(world.attention(3).points.size(), 1);
    QVERIFY(world.hasPlacement(3));
    QCOMPARE(world.placement(3).pos, QPointF(11, 22));

    // Empty merge does not clear bake
    world.mergeContentFromState(3, WorkspaceItemState{});
    QVERIFY(world.hasContentBake(3));
    QVERIFY(world.hasColor(3));
}


void ItemWorldTest::clearContentComponents_keepsAttentionAndPlacement()
{
    ItemWorld world;
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(1, 1, 10, 10);
    st.contentHFlip = true;
    st.colorAdjust.brightness = 20;
    st.pos = QPointF(5, 6);
    st.scale = 2.0;
    world.setAppearance(4, st);
    ItemComponents::Attention att;
    att.points = {QPointF(0.5, 0.5)};
    world.setAttention(4, att);

    world.clearContentComponents(4);
    QVERIFY(!world.hasCrop(4));
    QVERIFY(!world.hasContentBake(4));
    QVERIFY(!world.hasColor(4));
    QVERIFY(world.hasAttention(4));
    QVERIFY(world.hasPlacement(4));
    QCOMPARE(world.placement(4).pos, QPointF(5, 6));
}

QTEST_MAIN(ItemWorldTest)
#include "itemworld_test.moc"
