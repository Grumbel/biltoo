// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for the path-order dual model (docs/PATH_ORDER.md).
 *
 * SessionDocument (membership + appearance key) and SessionPathOrder
 * (Gallery-local pack / LoadAdd slots) must stay independent until Tier 4
 * replaces the view book. These tests lock the invariants any merge must
 * preserve — including the open → Gallery → crop → Image scenario's pure
 * session side.
 *
 * Full ImageView offscreen harness (decode + framing) still requires a build
 * environment; see TODO.md tip biltoo-1696.
 */

#include "sessiondocument.h"
#include "sessionpathorder.h"
#include "sessionappearance.h"

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
    book.setOrder(doc.pathList(),
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
    doc.append(QStringLiteral("/a.jpg"));
    const SessionImageId id = doc.idAt(0);
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(5, 5, 80, 60);
    doc.appearance().set(id, st);
    book.appendRow(QStringLiteral("/a.jpg"), id);

    book.clear();
    QVERIFY(doc.appearance().contains(id));
    QCOMPARE(doc.appearance().get(id)->cropRect, QRect(5, 5, 80, 60));
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
    doc.appearance().set(id0, crop0);
    doc.appearance().set(id1, crop1);
    QCOMPARE(doc.appearance().get(id0)->cropRect, QRect(0, 0, 10, 10));
    QCOMPARE(doc.appearance().get(id1)->cropRect, QRect(20, 20, 10, 10));
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
    book.setOrder(doc.pathList(), {idOne, idTwo});
    QCOMPARE(book.size(), 2);

    // crop on the focused session image (id-keyed appearance)
    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(10, 20, 200, 150);
    crop.cropSourceSize = QSize(800, 600);
    doc.appearance().set(idOne, crop);

    // return to Image: book may clear or stay; appearance must stick on id
    book.clear();
    QVERIFY(doc.appearance().contains(idOne));
    QCOMPARE(doc.appearance().get(idOne)->cropRect, QRect(10, 20, 200, 150));
    QVERIFY(!doc.appearance().contains(idTwo));

    // sibling path unchanged; duplicate path would keep independent slots
    doc.append(QStringLiteral("/one.jpg"));
    const SessionImageId idOneB = doc.idAt(2);
    QVERIFY(idOneB != idOne);
    QVERIFY(!doc.appearance().contains(idOneB));
    QCOMPARE(doc.appearance().get(idOne)->cropRect, QRect(10, 20, 200, 150));
}

QTEST_MAIN(PathOrderDualModelTest)
#include "pathorder_dual_model_test.moc"
