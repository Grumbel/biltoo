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
    world.setAppearance(1, st);
    QCOMPARE(world.cropCount(), 1);
    QCOMPARE(world.attentionCount(), 1);

    world.clearAppearance();
    QVERIFY(!store.contains(1));
    QCOMPARE(world.cropCount(), 0);
    QCOMPARE(world.attentionCount(), 0);
}

QTEST_MAIN(ItemWorldTest)
#include "itemworld_test.moc"
