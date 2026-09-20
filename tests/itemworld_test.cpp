// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Phase 7 Stage 0: ItemWorld is a pure facade — same storage, id-keyed API.
 */

#include "itemworld.h"

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
    // Same object as the bound store.
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
    // Appearance is not path-keyed.
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

QTEST_MAIN(ItemWorldTest)
#include "itemworld_test.moc"
