// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ImageView characterization harness (Tier 4 prerequisite).
 *
 * docs/IMAGEVIEW_CHARACTERIZATION.md — full open → Gallery → crop → return
 * needs an offscreen ImageView (near-full link). This tip expands pure-session
 * + PackOrderOverlay host-mutator simulation (post-1883/1884 storage). The
 * ImageView step remains QSKIP until that link is enabled.
 */

#include "sessiondocument.h"
#include "sessionpathorder.h"
#include "sessionappearance.h"
#include "packorderview.h"
#include "packorderoverlay.h"
#include "itemworld.h"
#include "itemcomponents.h"
#include "contentxform.h"

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

    void imageView_openGalleryCropReturn_pending();

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
    // ImageView ctor seeds Explicit empty; setWorkspacePaths → setExplicit +
    // tryCollapse when aligned with the bound document.
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

    // Logical layout size for Gallery/Image framing uses ContentXform.
    const QSize native(64, 48);
    const QSize layout = ContentXform::layoutSize(native, *got);
    QCOMPARE(layout, QSize(32, 24));

    // Sibling path keeps native layout (no crop on other).
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

    // pathOrderClear — must not follow document membership.
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
    // Simulate pathOrderSetOrder
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

    // LoadAdd after collapse: seed from doc then append.
    overlay.appendExplicitRow(m_pathA, doc.idAt(0), &doc);
    QVERIFY(overlay.isExplicit());
    QCOMPARE(overlay.explicitSize(), 3);
    QCOMPARE(overlay.resolve(&doc).countPathOccurrences(m_pathA), 2);
    QCOMPARE(doc.size(), 2);
}

void ImageViewCharacterizationTest::returnToImage_cropSurvivesPathOrderClear()
{
    // Mode-leave pathOrderClear must not wipe id-keyed crop (return to Image).
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

void ImageViewCharacterizationTest::imageView_openGalleryCropReturn_pending()
{
#if defined(BILTOO_HAVE_IMAGEVIEW_HARNESS)
    QSKIP("biltoo_lib linked; ImageView open→Gallery→crop→return body not "
          "implemented yet (docs/IMAGEVIEW_CHARACTERIZATION.md).");
#else
    QSKIP("Pure scaffold only — configure with "
          "-DBILTOO_IMAGEVIEW_CHARACTERIZATION=ON to link biltoo_lib.");
#endif
}

QTEST_MAIN(ImageViewCharacterizationTest)
#include "imageview_characterization.moc"
