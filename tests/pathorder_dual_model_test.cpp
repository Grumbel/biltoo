// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for the path-order dual model (docs/PATH_ORDER.md).
 *
 * SessionDocument (membership + appearance key) and pack-order storage
 * (SessionPathOrder / PackOrderOverlay — Gallery LoadAdd / mode-leave) must
 * stay independent until Tier 4 residual is fully trusted. These tests lock
 * the invariants any merge must preserve — including the open → Gallery →
 * crop → Image scenario's pure session side.
 *
 * Full ImageView offscreen harness (decode + framing) still requires a build
 * environment; see docs/IMAGEVIEW_CHARACTERIZATION.md.
 */

#include "session/sessiondocument.h"
#include "itemworld.h"
#include "session/sessionpathorder.h"
#include "session/sessionappearance.h"
#include "session/packorderoverlay.h"
#include "session/packorderview.h"

#include <QtTest/QtTest>

class PathOrderDualModelTest : public QObject
{
    Q_OBJECT
private slots:
    void independent_empty();
    void documentSetPaths_doesNotTouchBook();
    void bookSetOrder_doesNotTouchDocument();
    void loadAdd_multiplicity_bookOnly();
    void pathOrderClear_leavesDocumentIntact();
    void appearance_survivesBookClear();
    void duplicatePath_idsIndependentAcrossModels();
    void firstId_documentVsBook();
    void openGalleryCropScenario_sessionSide();
    void galleryDelete_prunesBookKeepsOtherAppearance();
    void alignedPackOrder_documentMatchesBook();
    void loadAdd_bookExceedsDocument_packUsesBook();
    void modeLeave_clearBook_documentPackWouldRegenerateIncorrectly();
    // PackOrderOverlay as the view pack side (post-1883 storage)
    void overlay_modeLeave_clearSuppressesDocumentPack();
    void overlay_loadAdd_exceedsDocument();
    void overlay_aligned_collapseFollowsDocument();
    void overlay_collapseRejectedWhenClearedAgainstPopulatedDoc();
};

void PathOrderDualModelTest::independent_empty()
{
    SessionDocument doc;
    SessionPathOrder book;
    QVERIFY(doc.isEmpty());
    QVERIFY(book.isEmpty());
    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/a.jpg")), 0);
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/a.jpg")), 0);
}

void PathOrderDualModelTest::documentSetPaths_doesNotTouchBook()
{
    SessionDocument doc;
    SessionPathOrder book;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    QCOMPARE(doc.size(), 2);
    QVERIFY(book.isEmpty());
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/a.jpg")), 0);
}

void PathOrderDualModelTest::bookSetOrder_doesNotTouchDocument()
{
    SessionDocument doc;
    SessionPathOrder book;
    book.setOrder({QStringLiteral("/x.jpg"), QStringLiteral("/y.jpg")},
                  {SessionImageId(10), SessionImageId(20)});
    QCOMPARE(book.size(), 2);
    QVERIFY(doc.isEmpty());
    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/x.jpg")), 0);
}

void PathOrderDualModelTest::loadAdd_multiplicity_bookOnly()
{
    // Gallery LoadAdd / paste may want N pack slots for one session path.
    SessionDocument doc;
    SessionPathOrder book;
    doc.append(QStringLiteral("/photo.jpg"));
    const SessionImageId sid = doc.idAt(0);

    book.appendRow(QStringLiteral("/photo.jpg"), sid);
    book.appendRow(QStringLiteral("/photo.jpg"), sid); // second tile, same id ok for pack slots
    book.appendRow(QStringLiteral("/photo.jpg"), sid);

    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/photo.jpg")), 1);
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/photo.jpg")), 3);
    QCOMPARE(doc.size(), 1);
    QCOMPARE(book.size(), 3);
}

void PathOrderDualModelTest::pathOrderClear_leavesDocumentIntact()
{
    // Mode leave / blank Workspace: clear the book only — never consult
    // document to recreate tiles (PATH_ORDER.md §Why the view book remains).
    SessionDocument doc;
    SessionPathOrder book;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    book.setOrder(doc.paths(),
                  {doc.idAt(0), doc.idAt(1)});

    book.clear();
    QVERIFY(book.isEmpty());
    QCOMPARE(doc.size(), 2);
    QCOMPARE(doc.pathAt(0), QStringLiteral("/a.jpg"));
    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/a.jpg")), 1);
}

void PathOrderDualModelTest::appearance_survivesBookClear()
{
    SessionDocument doc;
    SessionPathOrder book;
    ItemWorld world;
    doc.append(QStringLiteral("/a.jpg"));
    const SessionImageId id = doc.idAt(0);
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(5, 5, 80, 60);
    world.setAppearance(id, st);
    book.appendRow(QStringLiteral("/a.jpg"), id);

    book.clear();
    QVERIFY(world.hasCrop(id));
    QCOMPARE(world.appearanceValue(id).cropRect, QRect(5, 5, 80, 60));
}

void PathOrderDualModelTest::duplicatePath_idsIndependentAcrossModels()
{
    // Session membership may hold two rows for the same path (distinct ids).
    // Gallery book may hold a different multiplicity / id pairing.
    SessionDocument doc;
    SessionPathOrder book;
    doc.append(QStringLiteral("/dup.jpg"));
    doc.append(QStringLiteral("/dup.jpg"));
    const SessionImageId id0 = doc.idAt(0);
    const SessionImageId id1 = doc.idAt(1);
    QVERIFY(id0 != id1);

    book.appendRow(QStringLiteral("/dup.jpg"), id0);
    book.appendRow(QStringLiteral("/dup.jpg"), id1);
    book.appendRow(QStringLiteral("/dup.jpg"), id0); // third pack slot reuses id0

    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/dup.jpg")), 2);
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/dup.jpg")), 3);
    QCOMPARE(book.idAt(0), id0);
    QCOMPARE(book.idAt(1), id1);
    QCOMPARE(book.idAt(2), id0);

    WorkspaceItemState crop0;
    crop0.hasCrop = true;
    crop0.cropRect = QRect(0, 0, 10, 10);
    WorkspaceItemState crop1;
    crop1.hasCrop = true;
    crop1.cropRect = QRect(20, 20, 10, 10);
    ItemWorld world;
    world.setAppearance(id0, crop0);
    world.setAppearance(id1, crop1);
    QCOMPARE(world.appearanceValue(id0).cropRect, QRect(0, 0, 10, 10));
    QCOMPARE(world.appearanceValue(id1).cropRect, QRect(20, 20, 10, 10));
}

void PathOrderDualModelTest::firstId_documentVsBook()
{
    SessionDocument doc;
    SessionPathOrder book;
    doc.append(QStringLiteral("/a.jpg"));
    doc.append(QStringLiteral("/a.jpg"));
    const SessionImageId docFirst = doc.firstIdForPath(QStringLiteral("/a.jpg"));
    QCOMPARE(docFirst, doc.idAt(0));

    // Book may list a different first id for the same path (unaligned dual model).
    book.appendRow(QStringLiteral("/a.jpg"), doc.idAt(1));
    book.appendRow(QStringLiteral("/a.jpg"), doc.idAt(0));
    QCOMPARE(book.firstIdForPath(QStringLiteral("/a.jpg")), doc.idAt(1));
    // Identity queries must prefer the document when bound (ImageView rule).
    QCOMPARE(doc.firstIdForPath(QStringLiteral("/a.jpg")), doc.idAt(0));
    QVERIFY(doc.firstIdForPath(QStringLiteral("/a.jpg"))
            != book.firstIdForPath(QStringLiteral("/a.jpg")));
}

void PathOrderDualModelTest::openGalleryCropScenario_sessionSide()
{
    // Pure-session side of open → Gallery → crop → return → Image.
    // (Decode, framing, and mode transitions need the ImageView harness.)
    SessionDocument doc;
    SessionPathOrder book;

    // open: load two files into the session
    doc.setPaths({QStringLiteral("/one.jpg"), QStringLiteral("/two.jpg")});
    const SessionImageId idOne = doc.idAt(0);
    const SessionImageId idTwo = doc.idAt(1);

    // Gallery: view book mirrors membership for pack
    book.setOrder(doc.paths(), {idOne, idTwo});
    QCOMPARE(book.size(), 2);

    // crop on the focused session image (id-keyed appearance)
    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(10, 20, 200, 150);
    crop.cropSourceSize = QSize(800, 600);
    ItemWorld world;
    world.setAppearance(idOne, crop);

    // return to Image: book may clear or stay; appearance must stick on id
    book.clear();
    QVERIFY(world.hasCrop(idOne));
    QCOMPARE(world.appearanceValue(idOne).cropRect, QRect(10, 20, 200, 150));
    QVERIFY(!world.hasCrop(idTwo));

    // sibling path unchanged; duplicate path would keep independent slots
    doc.append(QStringLiteral("/one.jpg"));
    const SessionImageId idOneB = doc.idAt(2);
    QVERIFY(idOneB != idOne);
    QVERIFY(!world.hasCrop(idOneB));
    QCOMPARE(world.appearanceValue(idOne).cropRect, QRect(10, 20, 200, 150));
}


void PathOrderDualModelTest::galleryDelete_prunesBookKeepsOtherAppearance()
{
    // Gallery Delete: MainWindow removes from document; view prunes book rows.
    SessionDocument doc;
    SessionPathOrder book;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg"), QStringLiteral("/c.jpg")});
    const SessionImageId idA = doc.idAt(0);
    const SessionImageId idB = doc.idAt(1);
    const SessionImageId idC = doc.idAt(2);
    book.setOrder(doc.paths(), {idA, idB, idC});

    WorkspaceItemState cropB;
    cropB.hasCrop = true;
    cropB.cropRect = QRect(1, 2, 30, 40);
    ItemWorld world;
    world.setAppearance(idB, cropB);

    // Remove middle session row (b)
    doc.removeAt(1);
    book.setOrder(doc.paths(), {doc.idAt(0), doc.idAt(1)});

    QCOMPARE(doc.size(), 2);
    QCOMPARE(book.size(), 2);
    QCOMPARE(doc.pathAt(0), QStringLiteral("/a.jpg"));
    QCOMPARE(doc.pathAt(1), QStringLiteral("/c.jpg"));
    QCOMPARE(book.pathAt(0), QStringLiteral("/a.jpg"));
    QCOMPARE(book.pathAt(1), QStringLiteral("/c.jpg"));
    // Sparse appearance for removed id may remain as orphan until clearAppearance —
    // crop on B must not transfer to C
    QVERIFY(!world.hasCrop(idC));
    QCOMPARE(world.appearanceValue(idB).cropRect, QRect(1, 2, 30, 40));
    QCOMPARE(doc.idAt(1), idC);
}

void PathOrderDualModelTest::alignedPackOrder_documentMatchesBook()
{
    // Tier 4 target case: when multiplicities match, document paths/ids are a
    // valid pack order (view could query document instead of the book).
    SessionDocument doc;
    SessionPathOrder book;
    doc.setPaths({QStringLiteral("/x.jpg"), QStringLiteral("/y.jpg")});
    book.setOrder(doc.paths(), doc.ids());

    QCOMPARE(book.pathList(), doc.paths());
    QCOMPARE(book.idList(), doc.ids());
    QCOMPARE(book.size(), doc.size());
}

void PathOrderDualModelTest::loadAdd_bookExceedsDocument_packUsesBook()
{
    // After LoadAdd multiplicity, document cannot replace the book for pack.
    SessionDocument doc;
    SessionPathOrder book;
    doc.append(QStringLiteral("/solo.jpg"));
    const SessionImageId sid = doc.idAt(0);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);
    book.appendRow(QStringLiteral("/solo.jpg"), sid);

    QCOMPARE(doc.size(), 1);
    QCOMPARE(book.size(), 3);
    QVERIFY(book.size() != doc.size());
    // Pack must walk book, not document
    QCOMPARE(book.countPathOccurrences(QStringLiteral("/solo.jpg")), 3);
    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/solo.jpg")), 1);
}

void PathOrderDualModelTest::modeLeave_clearBook_documentPackWouldRegenerateIncorrectly()
{
    // Blank Workspace / mode leave: pathOrderClear. If pack consulted the
    // document, tiles would come back — the dual-model reason the book stays.
    SessionDocument doc;
    SessionPathOrder book;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    book.setOrder(doc.paths(), doc.ids());
    book.clear();

    QVERIFY(book.isEmpty());
    QVERIFY(!doc.isEmpty());
    // Document still has membership — must not be used as pack source here
    QCOMPARE(doc.size(), 2);
    QCOMPARE(book.size(), 0);
}


void PathOrderDualModelTest::overlay_modeLeave_clearSuppressesDocumentPack()
{
    // Same dual-model reason as the bare book: after clearExplicit, pack must
    // stay blank while SessionDocument still lists open files.
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    QCOMPARE(overlay.resolve(&doc).size(), 2);

    overlay.clearExplicit();
    QVERIFY(overlay.isExplicitEmpty());
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);
    QVERIFY(!PackOrderView::fromDocument(doc).isEmpty());
}

void PathOrderDualModelTest::overlay_loadAdd_exceedsDocument()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/solo.jpg"));
    const SessionImageId sid = doc.idAt(0);

    PackOrderOverlay overlay;
    overlay.setExplicit({QStringLiteral("/solo.jpg"), QStringLiteral("/solo.jpg"),
                         QStringLiteral("/solo.jpg")},
                        {sid, sid, sid});
    QVERIFY(!overlay.tryCollapseToFollowDocument(&doc));
    QCOMPARE(overlay.resolve(&doc).size(), 3);
    QCOMPARE(doc.size(), 1);
}

void PathOrderDualModelTest::overlay_aligned_collapseFollowsDocument()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/x.jpg"), QStringLiteral("/y.jpg")});

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    QVERIFY(overlay.tryCollapseToFollowDocument(&doc));
    QCOMPARE(overlay.mode(), PackOrderOverlay::Mode::FollowDocument);
    QVERIFY(overlay.resolve(&doc).alignsWithDocument(doc));

    // Document membership remains the pack source while FollowDocument.
    doc.setPaths({QStringLiteral("/x.jpg")});
    QCOMPARE(overlay.resolve(&doc).size(), 1);
    QCOMPARE(overlay.resolve(&doc).pathAt(0), QStringLiteral("/x.jpg"));
}

void PathOrderDualModelTest::overlay_collapseRejectedWhenClearedAgainstPopulatedDoc()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg")});

    PackOrderOverlay overlay;
    overlay.clearExplicit();
    QVERIFY(!overlay.tryCollapseToFollowDocument(&doc));
    QVERIFY(overlay.isExplicitEmpty());
    QVERIFY(overlay.resolve(&doc).isEmpty());
}

QTEST_MAIN(PathOrderDualModelTest)
#include "pathorder_dual_model_test.moc"
