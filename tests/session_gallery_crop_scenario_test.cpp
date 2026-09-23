// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Pure-session narrative for open → Gallery → crop → return → Image.
 * Complements pathorder_dual_model and docs/IMAGEVIEW_CHARACTERIZATION.md.
 * Decode/framing still need the offscreen ImageView harness.
 *
 * Phase 7: crop / bake writes go through ItemWorld so sparse component
 * presence is asserted alongside the DTO.
 */

#include "session/sessiondocument.h"
#include "session/sessionpathorder.h"
#include "session/sessionappearance.h"
#include "session/packorderview.h"
#include "contentxform.h"
#include "item/itemworld.h"
#include "item/itemcomponents.h"

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
    void itemWorld_cropComponentSurvivesBookClear();
    void itemWorld_contentBakeIndependentPerId();
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

    ItemWorld world;

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(10, 20, 200, 100);
    crop.cropSourceSize = QSize(800, 600);
    world.setAppearance(focus, crop);

    QVERIFY(world.hasCrop(focus));
    QCOMPARE(world.crop(focus).rect, QRect(10, 20, 200, 100));
    QCOMPARE(world.crop(focus).sourceSize, QSize(800, 600));

    QVERIFY(world.hasAppearance(focus));
    const WorkspaceItemState got = world.appearanceValue(focus);
    QVERIFY(got.hasCrop);

    // Post-crop layout long edge is the crop rect long edge (no orient).
    const QSize layout = ContentXform::layoutSize(QSize(800, 600), got);
    QCOMPARE(layout, QSize(200, 100));
    QVERIFY(layout.width() < 800);
}

void SessionGalleryCropScenarioTest::returnImage_cropSurvivesBookClear()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/one.png"), QStringLiteral("/two.png")});
    const SessionImageId focus = doc.idAt(0);

    ItemWorld world;

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(5, 5, 40, 30);
    crop.cropSourceSize = QSize(100, 100);
    world.setAppearance(focus, crop);

    SessionPathOrder book;
    book.setOrder(doc.paths(), doc.ids());
    // Mode leave / blank Workspace
    book.clear();

    QVERIFY(book.isEmpty());
    QVERIFY(world.hasCrop(focus));
    QCOMPARE(world.crop(focus).rect, QRect(5, 5, 40, 30));
    QVERIFY(world.hasDurableAppearance(focus));
    // Pack must not use document after clear
    QVERIFY(!PackOrderView::fromBook(book).alignsWithDocument(doc));
}

void SessionGalleryCropScenarioTest::siblingUnchanged_andDuplicatePathIndependent()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/one.png"), QStringLiteral("/two.png")});
    const SessionImageId idOne = doc.idAt(0);
    const SessionImageId idTwo = doc.idAt(1);

    ItemWorld world;

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(0, 0, 10, 10);
    world.setAppearance(idOne, crop);

    QVERIFY(world.hasCrop(idOne));
    QVERIFY(!world.hasCrop(idTwo));
    QVERIFY(!world.hasDurableAppearance(idTwo));

    doc.append(QStringLiteral("/one.png"));
    const SessionImageId idOneB = doc.idAt(2);
    QVERIFY(idOneB != idOne);
    QVERIFY(!world.hasCrop(idOneB));
    QCOMPARE(world.crop(idOne).rect, QRect(0, 0, 10, 10));
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

void SessionGalleryCropScenarioTest::itemWorld_cropComponentSurvivesBookClear()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/x.png")});
    const SessionImageId id = doc.idAt(0);

    ItemWorld world;

    ItemComponents::Crop c;
    c.rect = QRect(1, 2, 30, 40);
    c.sourceSize = QSize(400, 300);
    world.setCrop(id, c);

    SessionPathOrder book;
    book.setOrder(doc.paths(), doc.ids());
    book.clear();

    QVERIFY(world.hasCrop(id));
    QCOMPARE(world.cropCount(), 1);
    QCOMPARE(world.crop(id).rect, QRect(1, 2, 30, 40));
}

void SessionGalleryCropScenarioTest::itemWorld_contentBakeIndependentPerId()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/same.png"), QStringLiteral("/same.png")});
    const SessionImageId a = doc.idAt(0);
    const SessionImageId b = doc.idAt(1);
    QVERIFY(a != b);

    ItemWorld world;

    ItemComponents::ContentBake bake;
    bake.quarterTurns = 1;
    bake.hFlip = true;
    world.setContentBake(a, bake);

    QVERIFY(world.hasContentBake(a));
    QVERIFY(!world.hasContentBake(b));
    QCOMPARE(world.contentBake(a).quarterTurns, 1);
    QVERIFY(world.contentBake(a).hFlip);
    QVERIFY(world.contentBake(b).isIdentity());
}

QTEST_MAIN(SessionGalleryCropScenarioTest)
#include "session_gallery_crop_scenario_test.moc"
