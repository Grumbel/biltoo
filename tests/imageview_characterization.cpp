// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ImageView characterization harness (Tier 4 prerequisite).
 *
 * docs/IMAGEVIEW_CHARACTERIZATION.md
 *
 * Default build: pure session + PackOrderOverlay host-mutator simulation.
 * -DBILTOO_IMAGEVIEW_CHARACTERIZATION=ON links biltoo_lib and defines
 * BILTOO_HAVE_IMAGEVIEW_HARNESS — exercises a real offscreen ImageView for
 * pack-order / appearance invariants (decode/framing still soft).
 */

#include "sessiondocument.h"
#include "sessionpathorder.h"
#include "sessionappearance.h"
#include "packorderview.h"
#include "packorderoverlay.h"
#include "itemworld.h"
#include "itemcomponents.h"
#include "contentxform.h"
#include "imageview_types.h"

#if defined(BILTOO_HAVE_IMAGEVIEW_HARNESS)
#  include "imageview.h"
#endif

#include <QtTest/QtTest>
#include <QImage>
#include <QTemporaryDir>
#include <QFile>

class ImageViewCharacterizationTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void fixture_twoPngsOnDisk();
    void afterOpen_docSizeAndUniqueIds();
    void afterGalleryEnter_overlayAligns_whenNoLoadAdd();
    void afterCropCommit_appearanceLayoutSizeAndSibling();
    void afterPathOrderClear_explicitEmptyDocUnchanged();
    void loadAdd_multiplicityExceedsDocument();
    void hostMutators_setOrderCollapsesWhenAligned();
    void hostMutators_clearStaysExplicitEmpty();
    void hostMutators_appendAfterCollapse_seedsMembership();
    void returnToImage_cropSurvivesPathOrderClear();
    void returnToImage_placementSurvivesPathOrderClear();
    void returnToImage_contentBakeSurvivesPathOrderClear();
    void returnToImage_colorSurvivesPathOrderClear();
    void returnToImage_attentionSurvivesPathOrderClear();

    void imageView_openGalleryCropReturn();

private:
    QTemporaryDir m_tmp;
    QString m_pathA;
    QString m_pathB;

    static bool writeSolidPng(const QString &path, int w, int h, QRgb rgb);
};

bool ImageViewCharacterizationTest::writeSolidPng(const QString &path, int w, int h,
                                                   QRgb rgb)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(rgb);
    return img.save(path, "PNG");
}

void ImageViewCharacterizationTest::initTestCase()
{
    QVERIFY2(m_tmp.isValid(), "temp dir");
    m_pathA = m_tmp.filePath(QStringLiteral("a.png"));
    m_pathB = m_tmp.filePath(QStringLiteral("b.png"));
    QVERIFY(writeSolidPng(m_pathA, 64, 48, qRgb(200, 40, 40)));
    QVERIFY(writeSolidPng(m_pathB, 80, 60, qRgb(40, 40, 200)));
    QVERIFY(QFile::exists(m_pathA));
    QVERIFY(QFile::exists(m_pathB));
}

void ImageViewCharacterizationTest::cleanupTestCase()
{
}

void ImageViewCharacterizationTest::fixture_twoPngsOnDisk()
{
    QImage a(m_pathA);
    QImage b(m_pathB);
    QCOMPARE(a.size(), QSize(64, 48));
    QCOMPARE(b.size(), QSize(80, 60));
}

void ImageViewCharacterizationTest::afterOpen_docSizeAndUniqueIds()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    QCOMPARE(doc.size(), 2);
    QVERIFY(doc.idAt(0) != kInvalidSessionImageId);
    QVERIFY(doc.idAt(1) != kInvalidSessionImageId);
    QVERIFY(doc.idAt(0) != doc.idAt(1));
    QCOMPARE(doc.pathAt(0), m_pathA);
    QCOMPARE(doc.pathAt(1), m_pathB);
    QVERIFY(doc.validateUniqueIds("characterization open"));
}

void ImageViewCharacterizationTest::afterGalleryEnter_overlayAligns_whenNoLoadAdd()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});

    PackOrderOverlay overlay;
    overlay.clearExplicit();
    QVERIFY(overlay.isExplicitEmpty());

    overlay.setExplicit(doc.paths(), doc.ids());
    QVERIFY(overlay.tryCollapseToFollowDocument(&doc));
    QCOMPARE(overlay.mode(), PackOrderOverlay::Mode::FollowDocument);

    const PackOrderView pack = overlay.resolve(&doc);
    QVERIFY(pack.alignsWithDocument(doc));
    QCOMPARE(pack.size(), 2);
    QCOMPARE(pack.pathAt(0), m_pathA);
    QCOMPARE(pack.idAt(0), doc.idAt(0));
}

void ImageViewCharacterizationTest::afterCropCommit_appearanceLayoutSizeAndSibling()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;
    world.bindAppearance(&doc.appearance());

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(4, 4, 32, 24);
    crop.cropSourceSize = QSize(64, 48);
    world.setAppearance(focus, crop);

    QVERIFY(world.hasCrop(focus));
    QVERIFY(!world.hasCrop(other));
    const WorkspaceItemState *got = world.getAppearance(focus);
    QVERIFY(got);
    QVERIFY(got->hasCrop);
    QCOMPARE(got->cropRect, QRect(4, 4, 32, 24));

    const QSize native(64, 48);
    const QSize layout = ContentXform::layoutSize(native, *got);
    QCOMPARE(layout, QSize(32, 24));

    QCOMPARE(ContentXform::layoutSize(QSize(80, 60), WorkspaceItemState{}),
             QSize(80, 60));
}

void ImageViewCharacterizationTest::afterPathOrderClear_explicitEmptyDocUnchanged()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.tryCollapseToFollowDocument(&doc);
    QCOMPARE(overlay.mode(), PackOrderOverlay::Mode::FollowDocument);

    overlay.clearExplicit();
    QVERIFY(overlay.isExplicitEmpty());
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);
    QCOMPARE(doc.pathAt(0), m_pathA);
}

void ImageViewCharacterizationTest::loadAdd_multiplicityExceedsDocument()
{
    SessionDocument doc;
    doc.setPaths({m_pathA});
    QCOMPARE(doc.size(), 1);
    const SessionImageId id = doc.idAt(0);

    PackOrderOverlay overlay;
    overlay.setExplicit({m_pathA, m_pathA, m_pathA}, {id, id, id});
    QVERIFY(!overlay.tryCollapseToFollowDocument(&doc));
    QVERIFY(overlay.isExplicit());

    const PackOrderView pack = overlay.resolve(&doc);
    QCOMPARE(pack.size(), 3);
    QCOMPARE(pack.countPathOccurrences(m_pathA), 3);
    QCOMPARE(doc.size(), 1);
    QCOMPARE(doc.countPathOccurrences(m_pathA), 1);
}

void ImageViewCharacterizationTest::hostMutators_setOrderCollapsesWhenAligned()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});

    PackOrderOverlay overlay;
    overlay.clearExplicit();
    overlay.setExplicit(doc.paths(), doc.ids());
    QVERIFY(overlay.tryCollapseToFollowDocument(&doc));
    QCOMPARE(overlay.resolve(&doc).size(), 2);
}

void ImageViewCharacterizationTest::hostMutators_clearStaysExplicitEmpty()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.tryCollapseToFollowDocument(&doc);
    overlay.clearExplicit();
    QVERIFY(!overlay.tryCollapseToFollowDocument(&doc));
    QVERIFY(overlay.isExplicitEmpty());
}

void ImageViewCharacterizationTest::hostMutators_appendAfterCollapse_seedsMembership()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.tryCollapseToFollowDocument(&doc);
    QCOMPARE(overlay.mode(), PackOrderOverlay::Mode::FollowDocument);

    overlay.appendExplicitRow(m_pathA, doc.idAt(0), &doc);
    QVERIFY(overlay.isExplicit());
    QCOMPARE(overlay.explicitSize(), 3);
    QCOMPARE(overlay.resolve(&doc).countPathOccurrences(m_pathA), 2);
    QCOMPARE(doc.size(), 2);
}

void ImageViewCharacterizationTest::returnToImage_cropSurvivesPathOrderClear()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;
    world.bindAppearance(&doc.appearance());

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(8, 8, 16, 12);
    crop.cropSourceSize = QSize(64, 48);
    world.setAppearance(focus, crop);

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.clearExplicit();

    QVERIFY(world.hasCrop(focus));
    QVERIFY(!world.hasCrop(other));
    QCOMPARE(world.getAppearance(focus)->cropRect, QRect(8, 8, 16, 12));
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);
}

/** Stage 2: Placement is id-keyed; path-order clear must not drop pose. */
void ImageViewCharacterizationTest::returnToImage_placementSurvivesPathOrderClear()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;
    world.bindAppearance(&doc.appearance());

    ItemComponents::Placement pose;
    pose.pos = QPointF(120.0, 80.0);
    pose.scale = 1.25;
    pose.rotation = 15.0;
    world.setPlacement(focus, pose);

    QVERIFY(world.hasPlacement(focus));
    QVERIFY(!world.hasPlacement(other));

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.clearExplicit();

    QVERIFY(world.hasPlacement(focus));
    QVERIFY(!world.hasPlacement(other));
    const ItemComponents::Placement got = world.placement(focus);
    QCOMPARE(got.pos, QPointF(120.0, 80.0));
    QVERIFY(qFuzzyCompare(got.scale, 1.25));
    QVERIFY(qFuzzyCompare(got.rotation, 15.0));
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);
}

/** Stage 1: ContentBake is id-keyed; path-order clear must not drop orient bake. */
void ImageViewCharacterizationTest::returnToImage_contentBakeSurvivesPathOrderClear()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;
    world.bindAppearance(&doc.appearance());

    ItemComponents::ContentBake bake;
    bake.quarterTurns = 1;
    bake.hFlip = true;
    world.setContentBake(focus, bake);

    QVERIFY(world.hasContentBake(focus));
    QVERIFY(!world.hasContentBake(other));

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.clearExplicit();

    QVERIFY(world.hasContentBake(focus));
    QVERIFY(!world.hasContentBake(other));
    QCOMPARE(world.contentBake(focus).quarterTurns, 1);
    QVERIFY(world.contentBake(focus).hFlip);
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);
}

/** Stage 1: Color grade is id-keyed; path-order clear must not drop grade. */
void ImageViewCharacterizationTest::returnToImage_colorSurvivesPathOrderClear()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;
    world.bindAppearance(&doc.appearance());

    ItemComponents::Color c;
    c.grade.brightness = -12;
    c.grade.contrast = 115;
    world.setColor(focus, c);

    QVERIFY(world.hasColor(focus));
    QVERIFY(!world.hasColor(other));

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.clearExplicit();

    QVERIFY(world.hasColor(focus));
    QVERIFY(!world.hasColor(other));
    QCOMPARE(world.color(focus).grade.brightness, -12);
    QCOMPARE(world.color(focus).grade.contrast, 115);
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);
}

/** Stage 1: Attention points are id-keyed; path-order clear must not drop them. */
void ImageViewCharacterizationTest::returnToImage_attentionSurvivesPathOrderClear()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;
    world.bindAppearance(&doc.appearance());

    ItemComponents::Attention att;
    att.points = {QPointF(0.25, 0.35), QPointF(0.6, 0.7)};
    world.setAttention(focus, att);

    QVERIFY(world.hasAttention(focus));
    QVERIFY(!world.hasAttention(other));

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.clearExplicit();

    QVERIFY(world.hasAttention(focus));
    QVERIFY(!world.hasAttention(other));
    QCOMPARE(world.attention(focus).points.size(), 2);
    QCOMPARE(world.attention(focus).points.at(0), QPointF(0.25, 0.35));
    QCOMPARE(world.attention(focus).points.at(1), QPointF(0.6, 0.7));
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);
}

void ImageViewCharacterizationTest::imageView_openGalleryCropReturn()
{
#if !defined(BILTOO_HAVE_IMAGEVIEW_HARNESS)
    QSKIP("Pure scaffold only — configure with "
          "-DBILTOO_IMAGEVIEW_CHARACTERIZATION=ON to link biltoo_lib.");
#else
    // Offscreen ImageView: pack-order + appearance path (no decode wait).
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    QCOMPARE(doc.size(), 2);
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ImageView view;
    view.resize(800, 600);
    view.bindSessionAppearance(&doc.appearance());
    view.bindSessionDocument(&doc);

    // Gallery before setWorkspacePaths (Image mode rejects path placement).
    view.enterGallery(LayoutMode::Grid);
    QVERIFY(view.isGalleryMode());

    view.setWorkspacePaths(doc.paths(), doc.ids());
    {
        const PackOrderView pack = view.currentPackOrder();
        QCOMPARE(pack.size(), 2);
        // May be FollowDocument after aligned setOrder collapse.
        QVERIFY(pack.alignsWithDocument(doc) || pack.size() == doc.size());
        QCOMPARE(pack.pathAt(0), m_pathA);
        QCOMPARE(pack.idAt(0), focus);
    }

    // Crop one session id via ItemWorld (same store MainWindow binds).
    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(4, 4, 32, 24);
    crop.cropSourceSize = QSize(64, 48);
    view.itemWorld().setAppearance(focus, crop);
    QVERIFY(view.itemWorld().hasCrop(focus));
    QVERIFY(!view.itemWorld().hasCrop(other));
    QCOMPARE(ContentXform::layoutSize(QSize(64, 48),
                                      *view.itemWorld().getAppearance(focus)),
             QSize(32, 24));

    // Mode-leave style clear: pack blank, document + crop intact.
    view.pathOrderClear();
    QVERIFY(view.pathOrderIsEmpty());
    QCOMPARE(doc.size(), 2);
    QVERIFY(view.itemWorld().hasCrop(focus));
    QVERIFY(!view.itemWorld().hasCrop(other));

    // LoadAdd multiplicity on the view overlay only.
    view.pathOrderSetOrder({m_pathA, m_pathA, m_pathA}, {focus, focus, focus});
    QCOMPARE(view.pathOrderOccurrences(m_pathA), 3);
    QCOMPARE(doc.size(), 2);
    QCOMPARE(doc.countPathOccurrences(m_pathA), 1);
#endif
}

QTEST_MAIN(ImageViewCharacterizationTest)
#include "imageview_characterization.moc"
