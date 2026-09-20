// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Pure-session narrative for open → Gallery → crop → return → Image.
 * Complements pathorder_dual_model and docs/IMAGEVIEW_CHARACTERIZATION.md.
 * Decode/framing still need the offscreen ImageView harness.
 */

#include "sessiondocument.h"
#include "sessionpathorder.h"
#include "sessionappearance.h"
#include "packorderview.h"
#include "contentxform.h"

#include <QtTest/QtTest>

class SessionGalleryCropScenarioTest : public QObject
{
    Q_OBJECT
private slots:
    void open_twoFiles_uniqueIds();
    void enterGallery_packAlignedWithDocument();
    void cropFocused_layoutSizeShrinks();
    void returnImage_cropSurvivesBookClear();
    void siblingUnchanged_andDuplicatePathIndependent();
    void loadAdd_packFromBookNotDocument();
};

void SessionGalleryCropScenarioTest::open_twoFiles_uniqueIds()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/one.png"), QStringLiteral("/two.png")});
    QCOMPARE(doc.size(), 2);
    QVERIFY(doc.idAt(0) != doc.idAt(1));
    QVERIFY(doc.idAt(0) != kInvalidSessionImageId);
}

void SessionGalleryCropScenarioTest::enterGallery_packAlignedWithDocument()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/one.png"), QStringLiteral("/two.png")});
    SessionPathOrder book;
    book.setOrder(doc.paths(), doc.ids());

    const PackOrderView pack = PackOrderView::fromBook(book);
    QVERIFY(pack.alignsWithDocument(doc));
    QCOMPARE(pack, PackOrderView::fromDocument(doc));
    QCOMPARE(pack.size(), 2);
}

void SessionGalleryCropScenarioTest::cropFocused_layoutSizeShrinks()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/one.png"), QStringLiteral("/two.png")});
    const SessionImageId focus = doc.idAt(0);

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(10, 20, 200, 100);
    crop.cropSourceSize = QSize(800, 600);
    doc.appearance().set(focus, crop);

    const WorkspaceItemState *got = doc.appearance().get(focus);
    QVERIFY(got);
    QVERIFY(got->hasCrop);

    // Post-crop layout long edge is the crop rect long edge (no orient).
    const QSize layout = ContentXform::layoutSize(QSize(800, 600), *got);
    QCOMPARE(layout, QSize(200, 100));
    QVERIFY(layout.width() < 800);
}

void SessionGalleryCropScenarioTest::returnImage_cropSurvivesBookClear()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/one.png"), QStringLiteral("/two.png")});
    const SessionImageId focus = doc.idAt(0);
    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(5, 5, 40, 30);
    crop.cropSourceSize = QSize(100, 100);
    doc.appearance().set(focus, crop);

    SessionPathOrder book;
    book.setOrder(doc.paths(), doc.ids());
    // Mode leave / blank Workspace
    book.clear();

    QVERIFY(book.isEmpty());
    QVERIFY(doc.appearance().contains(focus));
    QCOMPARE(doc.appearance().get(focus)->cropRect, QRect(5, 5, 40, 30));
    // Pack must not use document after clear
    QVERIFY(!PackOrderView::fromBook(book).alignsWithDocument(doc));
}

void SessionGalleryCropScenarioTest::siblingUnchanged_andDuplicatePathIndependent()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/one.png"), QStringLiteral("/two.png")});
    const SessionImageId idOne = doc.idAt(0);
    const SessionImageId idTwo = doc.idAt(1);

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(0, 0, 10, 10);
    doc.appearance().set(idOne, crop);

    QVERIFY(!doc.appearance().contains(idTwo));

    doc.append(QStringLiteral("/one.png"));
    const SessionImageId idOneB = doc.idAt(2);
    QVERIFY(idOneB != idOne);
    QVERIFY(!doc.appearance().contains(idOneB));
    QCOMPARE(doc.appearance().get(idOne)->cropRect, QRect(0, 0, 10, 10));
}

void SessionGalleryCropScenarioTest::loadAdd_packFromBookNotDocument()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/solo.png"));
    SessionPathOrder book;
    const SessionImageId sid = doc.idAt(0);
    book.appendRow(QStringLiteral("/solo.png"), sid);
    book.appendRow(QStringLiteral("/solo.png"), sid);

    const PackOrderView fromBook = PackOrderView::fromBook(book);
    const PackOrderView fromDoc = PackOrderView::fromDocument(doc);
    QCOMPARE(fromBook.size(), 2);
    QCOMPARE(fromDoc.size(), 1);
    QVERIFY(!fromBook.alignsWithDocument(doc));
}

QTEST_MAIN(SessionGalleryCropScenarioTest)
#include "session_gallery_crop_scenario_test.moc"
