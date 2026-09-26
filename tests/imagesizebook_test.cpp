// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ImageSizeBook contracts that Gallery virtual plan and size-first open rely on.
 *
 * Recurring regressions these tests guard:
 * - Treating square stand-ins (1000²) as pack geometry → square cropped cells
 * - Provisional sizes reported as definitive
 * - noteDefinitive not clearing provisional / not installing real aspect
 * - Plan eligibility: only definitive or failed paths may enter the pack plan
 */

#include "item/imagesizebook.h"

#include <QtTest/QtTest>

class ImageSizeBookTest : public QObject
{
    Q_OBJECT
private slots:
    void standInNeutral_isSquare_documentedDanger();
    void provisional_notDefinitive_evenWhenKnown();
    void noteDefinitive_clearsProvisional_installsAspect();
    void noteDefinitive_rejectsMuchSmallerSample();
    void markFailed_isPackEligible();
    void planEligibility_onlyDefinitiveOrFailed();
    void clear_resetsAllFlags();
};

void ImageSizeBookTest::standInNeutral_isSquare_documentedDanger()
{
    // standInNeutral is intentionally square (placeholder geometry). Gallery
    // rebuildVirtualPlan must NEVER feed this into pack sizes — doing so made
    // every cell square and galleryClipLocal cropped real content (2388).
    const QSize standIn = ImageSizeBook::standInNeutral();
    QCOMPARE(standIn.width(), standIn.height());
    QVERIFY(standIn.width() > 1);
    // Compound square stand-in is also square.
    QCOMPARE(ImageSizeBook::standInSquare().width(),
             ImageSizeBook::standInSquare().height());
}

void ImageSizeBookTest::provisional_notDefinitive_evenWhenKnown()
{
    ImageSizeBook book;
    const QString path = QStringLiteral("/tmp/landscape.jpg");
    book.markProvisional(path, ImageSizeBook::standInNeutral());

    QVERIFY(book.contains(path));
    QVERIFY(book.isProvisional(path));
    QVERIFY(!book.hasDefinitive(path));
    // known() still returns the stand-in — callers must check hasDefinitive.
    QCOMPARE(book.known(path), ImageSizeBook::standInNeutral());
}

void ImageSizeBookTest::noteDefinitive_clearsProvisional_installsAspect()
{
    ImageSizeBook book;
    const QString path = QStringLiteral("/tmp/landscape.jpg");
    book.markProvisional(path, ImageSizeBook::standInNeutral());
    QVERIFY(book.isProvisional(path));

    const QSize real(4000, 2000);
    QVERIFY(book.noteDefinitive(path, real));
    QVERIFY(!book.isProvisional(path));
    QVERIFY(book.hasDefinitive(path));
    QCOMPARE(book.known(path), real);
    // Aspect must not be forced square after definitive install.
    QVERIFY(book.known(path).width() != book.known(path).height());
}

void ImageSizeBookTest::noteDefinitive_rejectsMuchSmallerSample()
{
    ImageSizeBook book;
    const QString path = QStringLiteral("/tmp/native.jpg");
    QVERIFY(book.noteDefinitive(path, QSize(4000, 3000)));
    // Soft/LQIP sample must not replace native (identity rule).
    QVERIFY(!book.noteDefinitive(path, QSize(64, 48)));
    QCOMPARE(book.known(path), QSize(4000, 3000));
    QVERIFY(book.hasDefinitive(path));
}

void ImageSizeBookTest::markFailed_isPackEligible()
{
    ImageSizeBook book;
    const QString path = QStringLiteral("/tmp/broken.jpg");
    book.markProvisional(path, ImageSizeBook::standInNeutral());
    book.markFailed(path);
    QVERIFY(book.isFailed(path));
    // Failure records flag only — do not invent 256² / 1000² geometry.
    // Gallery skips failed paths; Image waits for a real size or stays empty.
    QVERIFY(book.known(path).isEmpty());
    QVERIFY(!book.hasDefinitive(path));
    QVERIFY(!book.isProvisional(path));
    // Plan eligibility is still hasDefinitive || isFailed (not stand-in alone).
    QVERIFY(book.hasDefinitive(path) || book.isFailed(path));
}

void ImageSizeBookTest::planEligibility_onlyDefinitiveOrFailed()
{
    // Mirrors GalleryController::rebuildVirtualPlan gate:
    //   if (!hasDefinitive && !isFailed) → skip / break — never stand-in.
    ImageSizeBook book;
    const QString a = QStringLiteral("/a.jpg");
    const QString b = QStringLiteral("/b.jpg");
    const QString c = QStringLiteral("/c.jpg");

    book.markProvisional(a, ImageSizeBook::standInNeutral());
    QVERIFY(!(book.hasDefinitive(a) || book.isFailed(a)));

    QVERIFY(book.noteDefinitive(b, QSize(1920, 1080)));
    QVERIFY(book.hasDefinitive(b) || book.isFailed(b));

    book.markFailed(c);
    QVERIFY(book.hasDefinitive(c) || book.isFailed(c));
}

void ImageSizeBookTest::clear_resetsAllFlags()
{
    ImageSizeBook book;
    book.markProvisional(QStringLiteral("/p.jpg"), QSize(1000, 1000));
    book.noteDefinitive(QStringLiteral("/d.jpg"), QSize(800, 600));
    book.markFailed(QStringLiteral("/f.jpg"));
    book.clear();
    QVERIFY(!book.contains(QStringLiteral("/p.jpg")));
    QVERIFY(!book.contains(QStringLiteral("/d.jpg")));
    QVERIFY(!book.isFailed(QStringLiteral("/f.jpg")));
}

QTEST_APPLESS_MAIN(ImageSizeBookTest)
#include "imagesizebook_test.moc"
