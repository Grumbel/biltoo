// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for SessionAppearanceStore (Phase 6 Tier 4 prerequisite).
 *
 * Appearance is keyed by SessionImageId, not path — duplicate paths must keep
 * independent crop/orient state (the correctness driver for Tier 4).
 */

#include "sessionappearance.h"
#include "sessiondocument.h"

#include <QtTest/QtTest>

class SessionAppearanceTest : public QObject
{
    Q_OBJECT
private slots:
    void store_keyedById_notPath();
    void duplicatePath_independentCrop();
    void remove_clearsSlot();
    void materialize_identityWhenNoCrop();
    void materialize_appliesCrop();
    void materialize_quarterTurnSwapsAspect();
    void documentRemove_appearanceOrphanAllowed();
    void replaceAll_preservesAppearanceById();
};

void SessionAppearanceTest::store_keyedById_notPath()
{
    SessionAppearanceStore store;
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(10, 10, 100, 80);
    store.set(7, st);
    QVERIFY(store.contains(7));
    QVERIFY(!store.contains(8));
    const WorkspaceItemState *got = store.get(7);
    QVERIFY(got);
    QCOMPARE(got->cropRect, QRect(10, 10, 100, 80));
}

void SessionAppearanceTest::duplicatePath_independentCrop()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/same.jpg"));
    doc.append(QStringLiteral("/same.jpg"));
    const SessionImageId id0 = doc.idAt(0);
    const SessionImageId id1 = doc.idAt(1);
    QVERIFY(id0 != id1);

    SessionAppearanceStore store;
    WorkspaceItemState a;
    a.hasCrop = true;
    a.cropRect = QRect(0, 0, 50, 50);
    WorkspaceItemState b;
    b.hasCrop = true;
    b.cropRect = QRect(100, 100, 50, 50);
    store.set(id0, a);
    store.set(id1, b);

    QCOMPARE(store.get(id0)->cropRect, QRect(0, 0, 50, 50));
    QCOMPARE(store.get(id1)->cropRect, QRect(100, 100, 50, 50));
}

void SessionAppearanceTest::remove_clearsSlot()
{
    SessionAppearanceStore store;
    WorkspaceItemState st;
    st.hasCrop = true;
    store.set(3, st);
    store.remove(3);
    QVERIFY(!store.contains(3));
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

void SessionAppearanceTest::documentRemove_appearanceOrphanAllowed()
{
    // Tier 4 will own both; today store can retain orphans after doc remove.
    SessionDocument doc;
    doc.append(QStringLiteral("/x.jpg"));
    const SessionImageId id = doc.idAt(0);
    SessionAppearanceStore store;
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(1, 1, 2, 2);
    store.set(id, st);
    doc.removeAt(0);
    QVERIFY(doc.isEmpty());
    QVERIFY(store.contains(id)); // current dual-model behaviour
}

void SessionAppearanceTest::replaceAll_preservesAppearanceById()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    const SessionImageId idA = doc.idAt(0);
    const SessionImageId idB = doc.idAt(1);
    SessionAppearanceStore store;
    WorkspaceItemState a;
    a.hasCrop = true;
    a.cropRect = QRect(0, 0, 10, 10);
    WorkspaceItemState b;
    b.hasCrop = true;
    b.cropRect = QRect(5, 5, 10, 10);
    store.set(idA, a);
    store.set(idB, b);
    doc.replaceAll({QStringLiteral("/b.jpg"), QStringLiteral("/a.jpg")}, {idB, idA});
    QCOMPARE(store.get(idA)->cropRect, QRect(0, 0, 10, 10));
    QCOMPARE(store.get(idB)->cropRect, QRect(5, 5, 10, 10));
}

QTEST_MAIN(SessionAppearanceTest)
#include "sessionappearance_test.moc"
