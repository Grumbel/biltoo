// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ImageView characterization harness (Tier 4 prerequisite).
 *
 * docs/IMAGEVIEW_CHARACTERIZATION.md — full open → Gallery → crop → return
 * needs an offscreen ImageView (near-full link). This tip lands the fixture
 * and pure-session assertions; ImageView steps are QSKIP until that link exists.
 */

#include "sessiondocument.h"
#include "sessionpathorder.h"
#include "sessionappearance.h"
#include "packorderview.h"
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
    void afterGalleryEnter_pathOrderAligns_whenNoLoadAdd();
    void afterCropCommit_appearanceAndItemWorld();
    void afterPathOrderClear_bookEmptyDocUnchanged();
    void loadAdd_multiplicityBookExceedsDocument();

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
    // QTemporaryDir removes files
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

void ImageViewCharacterizationTest::afterGalleryEnter_pathOrderAligns_whenNoLoadAdd()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    SessionPathOrder book;
    book.setOrder(doc.paths(), doc.ids());
    QCOMPARE(book.size(), doc.size());
    const PackOrderView pack = PackOrderView::fromBook(book);
    QVERIFY(pack.alignsWithDocument(doc));
    QCOMPARE(pack.size(), 2);
}

void ImageViewCharacterizationTest::afterCropCommit_appearanceAndItemWorld()
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
}

void ImageViewCharacterizationTest::afterPathOrderClear_bookEmptyDocUnchanged()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    SessionPathOrder book;
    book.setOrder(doc.paths(), doc.ids());
    QCOMPARE(book.size(), 2);
    book.clear();
    QCOMPARE(book.size(), 0);
    QCOMPARE(doc.size(), 2);
    QCOMPARE(doc.pathAt(0), m_pathA);
}

void ImageViewCharacterizationTest::loadAdd_multiplicityBookExceedsDocument()
{
    SessionDocument doc;
    doc.setPaths({m_pathA});
    QCOMPARE(doc.size(), 1);
    const SessionImageId id = doc.idAt(0);

    SessionPathOrder book;
    // LoadAdd×3 same path: book carries multiplicity; document stays one id.
    book.setOrder({m_pathA, m_pathA, m_pathA}, {id, id, id});
    QCOMPARE(book.size(), 3);
    QCOMPARE(book.countPathOccurrences(m_pathA), 3);
    QCOMPARE(doc.size(), 1);
    QCOMPARE(doc.countPathOccurrences(m_pathA), 1);

    const PackOrderView pack = PackOrderView::fromBook(book);
    QCOMPARE(pack.size(), 3);
}

void ImageViewCharacterizationTest::imageView_openGalleryCropReturn_pending()
{
    QSKIP("Offscreen ImageView link not enabled yet "
          "(docs/IMAGEVIEW_CHARACTERIZATION.md — near-full app objects).");
}

QTEST_MAIN(ImageViewCharacterizationTest)
#include "imageview_characterization.moc"
