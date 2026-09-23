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

#include "session/sessiondocument.h"
#include "session/sessionpathorder.h"
#include "session/sessionappearance.h"
#include "session/packorderview.h"
#include "session/packorderoverlay.h"
#include "item/itemworld.h"
#include "item/itemcomponents.h"
#include "content/contentxform.h"
#include "view/viewframing.h"
#include "view/viewtransform.h"
#include "imageview_types.h"

#if defined(BILTOO_HAVE_IMAGEVIEW_HARNESS)
#  include "imageview.h"
#  include "imageitem.h"
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
    void returnToImage_appliedContentXformSurvivesPathOrderClear();
    void returnToImage_liveColorLagSurvivesPathOrderClear();
    void viewFraming_defaultsAndFitFill();
    void viewFraming_stickyPanNorms();
    void viewTransform_uniformFitAndPadded();

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

    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(4, 4, 32, 24);
    crop.cropSourceSize = QSize(64, 48);
    world.setAppearance(focus, crop);

    QVERIFY(world.hasCrop(focus));
    QVERIFY(!world.hasCrop(other));
    QVERIFY(world.hasAppearance(focus));
    const WorkspaceItemState got = world.appearanceValue(focus);
    QVERIFY(got.hasCrop);
    QCOMPARE(got.cropRect, QRect(4, 4, 32, 24));

    const QSize native(64, 48);
    const QSize layout = ContentXform::layoutSize(native, got);
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
    QCOMPARE(world.appearanceValue(focus).cropRect, QRect(8, 8, 16, 12));
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

/** Stage 2 residual: applied ContentXform is runtime-only (not durable);
 * path-order clear must not drop the fingerprint; sibling stays clean. */
void ImageViewCharacterizationTest::returnToImage_appliedContentXformSurvivesPathOrderClear()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;

    ContentXform::Value x;
    x.quarterTurns = 1;
    x.hFlip = true;
    world.setAppliedContentXform(focus, x);

    QVERIFY(world.hasAppliedContentXform(focus));
    QVERIFY(!world.hasAppliedContentXform(other));
    // Applied alone is not durable appearance (project/clipboard).
    QVERIFY(!world.hasDurableAppearance(focus));

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.clearExplicit();

    QVERIFY(world.hasAppliedContentXform(focus));
    QVERIFY(!world.hasAppliedContentXform(other));
    QCOMPARE(world.appliedContentXform(focus).quarterTurns, 1);
    QVERIFY(world.appliedContentXform(focus).hFlip);
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);

    world.clearAppearance();
    QVERIFY(!world.hasAppliedContentXform(focus));
}

/** Stage 2 residual: live colour lag is runtime-only (not durable Color);
 * path-order clear must not drop it; sibling stays clean. */
void ImageViewCharacterizationTest::returnToImage_liveColorLagSurvivesPathOrderClear()
{
    SessionDocument doc;
    doc.setPaths({m_pathA, m_pathB});
    const SessionImageId focus = doc.idAt(0);
    const SessionImageId other = doc.idAt(1);

    ItemWorld world;

    ColorAdjustments g;
    g.brightness = 22;
    g.contrast = 108;
    world.setLiveColorLag(focus, g);

    QVERIFY(world.hasLiveColorLag(focus));
    QVERIFY(!world.hasLiveColorLag(other));
    QVERIFY(!world.hasColor(focus)); // durable Color is separate
    QVERIFY(!world.hasDurableAppearance(focus));

    PackOrderOverlay overlay;
    overlay.setExplicit(doc.paths(), doc.ids());
    overlay.clearExplicit();

    QVERIFY(world.hasLiveColorLag(focus));
    QVERIFY(!world.hasLiveColorLag(other));
    QCOMPARE(world.liveColorLag(focus).brightness, 22);
    QCOMPARE(world.liveColorLag(focus).contrast, 108);
    QVERIFY(overlay.resolve(&doc).isEmpty());
    QCOMPARE(doc.size(), 2);

    world.clearAppearance();
    QVERIFY(!world.hasLiveColorLag(focus));
}


/** Phase 6 Tier 4 residual: ViewFraming pure defaults and fit/fill transitions. */
void ImageViewCharacterizationTest::viewFraming_defaultsAndFitFill()
{
    ViewFraming f;
    QVERIFY(f.isFitMode());
    QVERIFY(!f.isFillMode());
    QVERIFY(!f.isStickyZoomEnabled());
    QVERIFY(!f.hasPreservedViewScale());
    QVERIFY(!f.hasStickyPan());
    QCOMPARE(f.aspectMode(), Qt::KeepAspectRatio);

    QVERIFY(f.setFillMode());
    QVERIFY(f.isFitMode());
    QVERIFY(f.isFillMode());
    QCOMPARE(f.aspectMode(), Qt::KeepAspectRatioByExpanding);

    QVERIFY(f.setFitOnly());
    QVERIFY(f.isFitMode());
    QVERIFY(!f.isFillMode());

    QVERIFY(!f.setFitOnly()); // no-op when already fit-only

    QVERIFY(f.setFitFillFlags(false, true));
    QVERIFY(!f.isFitMode());
    QVERIFY(f.isFillMode());

    QVERIFY(f.setStickyZoomEnabled(true));
    QVERIFY(f.isStickyZoomEnabled());
    QVERIFY(!f.setStickyZoomEnabled(true));
}

/** Phase 6 Tier 4 residual: sticky pan norms clamp and map within item bounds. */
void ImageViewCharacterizationTest::viewFraming_stickyPanNorms()
{
    ViewFraming f;
    const QRectF bounds(10.0, 20.0, 100.0, 50.0);
    f.setStickyPanFromScene(QPointF(60.0, 45.0), bounds);
    QVERIFY(f.hasStickyPan());
    QCOMPARE(f.stickyPanNormPoint().x(), 0.5);
    QCOMPARE(f.stickyPanNormPoint().y(), 0.5);
    QCOMPARE(f.sceneFromStickyPan(bounds), QPointF(60.0, 45.0));

    // Outside bounds → clamped to [0,1].
    f.setStickyPanFromScene(QPointF(-50.0, 200.0), bounds);
    QVERIFY(f.stickyPanNormPoint().x() >= 0.0);
    QVERIFY(f.stickyPanNormPoint().x() <= 1.0);
    QVERIFY(f.stickyPanNormPoint().y() >= 0.0);
    QVERIFY(f.stickyPanNormPoint().y() <= 1.0);

    f.clearStickyPan();
    QVERIFY(!f.hasStickyPan());
    f.setPreservedViewScale(1.75);
    QVERIFY(f.hasPreservedViewScale());
    QCOMPARE(f.currentPreservedViewScale(), 1.75);
    f.clearPreservedViewScale();
    QVERIFY(!f.hasPreservedViewScale());
}

/** Phase 6 Tier 4 residual: pure ViewTransform fit scale and padded bounds. */
void ImageViewCharacterizationTest::viewTransform_uniformFitAndPadded()
{
    QCOMPARE(ViewTransform::uniformFitScale(200.0, 100.0, 100.0, 50.0), 2.0);
    QCOMPARE(ViewTransform::uniformFitScale(100.0, 100.0, 200.0, 100.0), 0.5);
    // Zero content width is floored to 1 so scale stays finite.
    QCOMPARE(ViewTransform::uniformFitScale(100.0, 100.0, 0.0, 50.0), 2.0);

    const QRectF bounds(10.0, 20.0, 40.0, 30.0);
    const QRectF pad = ViewTransform::padded(bounds, 5.0);
    QCOMPARE(pad, QRectF(5.0, 15.0, 50.0, 40.0));
    QVERIFY(ViewTransform::padded(QRectF(), 8.0).isEmpty());

    const QRectF fitted = ViewTransform::fitRectCentered(QRectF(0, 0, 200, 100),
                                                         QSizeF(50, 50));
    QCOMPARE(fitted.width(), 100.0);
    QCOMPARE(fitted.height(), 100.0);
    QCOMPARE(fitted.center(), QPointF(100.0, 50.0));
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
    view.bindSessionSeedBook(&doc.seedBook());
    view.bindSessionDocument(&doc);
    // Phase 6 Tier 4: framing defaults before any mode enter.
    QVERIFY(view.hostFraming().isFitMode());
    QVERIFY(!view.hostFraming().isFillMode());
    QVERIFY(!view.hostFraming().isStickyZoomEnabled());

    // Gallery before setWorkspacePaths (Image mode rejects path placement).
    view.enterGallery(LayoutMode::Grid);
    QVERIFY(view.isGalleryMode());

    // Prime definitive sizes so GallerySizeResolve does not defer populate.
    // Without this, setWorkspacePaths returns with an empty live canvas until
    // async probes finish (headless CI: focusItem == nullptr).
    view.rememberImageSize(m_pathA, QSize(64, 48));
    view.rememberImageSize(m_pathB, QSize(80, 60));

    view.setWorkspacePaths(doc.paths(), doc.ids());
    {
        const PackOrderView pack = view.currentPackOrder();
        QCOMPARE(pack.size(), 2);
        // May be FollowDocument after aligned setOrder collapse.
        QVERIFY(pack.alignsWithDocument(doc) || pack.size() == doc.size());
        QCOMPARE(pack.pathAt(0), m_pathA);
        QCOMPARE(pack.idAt(0), focus);
    }
    QVERIFY2(view.findItemBySessionId(focus) != nullptr,
             "Gallery tiles must exist after setWorkspacePaths with primed sizes");

    // Id-keyed components via ItemWorld (same store MainWindow binds).
    WorkspaceItemState crop;
    crop.hasCrop = true;
    crop.cropRect = QRect(4, 4, 32, 24);
    crop.cropSourceSize = QSize(64, 48);
    view.itemWorld().setAppearance(focus, crop);

    ItemComponents::Placement pose;
    pose.pos = QPointF(40.0, 60.0);
    pose.scale = 1.5;
    view.itemWorld().setPlacement(focus, pose);

    ItemComponents::ContentBake bake;
    bake.quarterTurns = 2;
    bake.vFlip = true;
    view.itemWorld().setContentBake(focus, bake);

    ItemComponents::Color color;
    color.grade.brightness = 8;
    view.itemWorld().setColor(focus, color);

    ItemComponents::Attention att;
    att.points = {QPointF(0.1, 0.2)};
    view.itemWorld().setAttention(focus, att);

    ContentXform::Value appliedCx;
    appliedCx.quarterTurns = 1;
    appliedCx.hFlip = true;
    view.itemWorld().setAppliedContentXform(focus, appliedCx);
    ColorAdjustments lag;
    lag.brightness = 5;
    view.itemWorld().setLiveColorLag(focus, lag);

    QVERIFY(view.itemWorld().hasCrop(focus));
    QVERIFY(view.itemWorld().hasPlacement(focus));
    QVERIFY(view.itemWorld().hasContentBake(focus));
    QVERIFY(view.itemWorld().hasColor(focus));
    QVERIFY(view.itemWorld().hasAttention(focus));
    // Content components stay focus-only (IDENTITY isolation).
    QVERIFY(!view.itemWorld().hasCrop(other));
    QVERIFY(!view.itemWorld().hasContentBake(other));
    QVERIFY(!view.itemWorld().hasColor(other));
    QVERIFY(!view.itemWorld().hasAttention(other));
    // Gallery pack writes sparse Placement for every live tile — sibling may
    // already have pose; isolation is content, not pack pose.
    QCOMPARE(view.itemWorld().placement(focus).pos, QPointF(40.0, 60.0));
    QCOMPARE(view.itemWorld().placement(focus).scale, 1.5);
    QCOMPARE(ContentXform::layoutSize(QSize(64, 48),
                                      view.itemWorld().appearanceValue(focus)),
             QSize(32, 24));

    // Phase 6 Tier 4 residual: soft display without async decode wait.
    // Fixture PNG → SoftPreview install; crop want materializes on GUI thread.
    {
        QImage hostA(m_pathA);
        QVERIFY(!hostA.isNull());
        QCOMPARE(hostA.size(), QSize(64, 48));
        ImageItem *focusItem = view.findItemBySessionId(focus);
        QVERIFY(focusItem != nullptr);
        view.hostDisplayPipeline().installDisplayPixels(
            focusItem, hostA, SessionAppearance::PixelKind::SoftPreview, focus);
        QVERIFY(focusItem->hasDisplayPixels());
        // Sibling not installed stays without display pixels.
        if (ImageItem *otherItem = view.findItemBySessionId(other)) {
            QVERIFY(!otherItem->hasDisplayPixels());
        }

        // Live framing residual: Image-mode canvas prep resets view matrix + Fit.
        view.prepareImageModeCanvas();
        QVERIFY(view.hostFraming().isFitMode());
        QVERIFY(!view.hostFraming().isFillMode());
        QCOMPARE(ViewTransform::scaleFrom(view.transform()), 1.0);

        // fitItem uses view matrix for framing (DOMAIN); scale > 0 after fit.
        view.fitItem(focusItem, Qt::KeepAspectRatio);
        const qreal fitScale = ViewTransform::scaleFrom(view.transform());
        QVERIFY(fitScale > 0.0);
        QVERIFY(view.hostFraming().isFitMode());

        // Sticky pan / preserved scale capture after framing (return-to-Image path).
        view.captureStickyPanAnchor(focusItem);
        QVERIFY(view.hostFraming().hasPreservedViewScale()
                || view.hostFraming().hasStickyPan());
        if (view.hostFraming().hasPreservedViewScale()) {
            QCOMPARE(view.hostFraming().currentPreservedViewScale(), fitScale);
        }
        // restore must not crash; centres on captured pan when available.
        view.restoreStickyPanAnchor(focusItem);
        QVERIFY(ViewTransform::scaleFrom(view.transform()) > 0.0);

        // Framing handoff across session ids (prev/next style without async Image load).
        ImageItem *otherItem = view.findItemBySessionId(other);
        QVERIFY(otherItem != nullptr);
        QImage hostB(m_pathB);
        QVERIFY(!hostB.isNull());
        QCOMPARE(hostB.size(), QSize(80, 60));
        view.hostDisplayPipeline().installDisplayPixels(
            otherItem, hostB, SessionAppearance::PixelKind::SoftPreview, other);
        QVERIFY(otherItem->hasDisplayPixels());
        // Sibling now has pixels; focus still has pixels from earlier install.
        QVERIFY(focusItem->hasDisplayPixels());

        view.fitItem(otherItem, Qt::KeepAspectRatio);
        const qreal otherFitScale = ViewTransform::scaleFrom(view.transform());
        QVERIFY(otherFitScale > 0.0);
        view.captureStickyPanAnchor(otherItem);
        if (view.hostFraming().hasPreservedViewScale()) {
            QCOMPARE(view.hostFraming().currentPreservedViewScale(), otherFitScale);
        }
        view.restoreStickyPanAnchor(otherItem);
        QVERIFY(ViewTransform::scaleFrom(view.transform()) > 0.0);

        // Switch capture back to focus id (navigate-like framing continuity).
        view.fitItem(focusItem, Qt::KeepAspectRatio);
        view.captureStickyPanAnchor(focusItem);
        view.restoreStickyPanAnchor(focusItem);
        QVERIFY(ViewTransform::scaleFrom(view.transform()) > 0.0);
    }

    // Phase 6 Tier 4 residual: Image-mode LoadReplace without async decode.
    // setViewMode(Image) clears the gallery canvas; installImageModeReplaceItem
    // rebuilds the sole Image item from fixture host pixels + ItemWorld want.
    {
        view.setCurrentSessionId(focus);
        view.setViewMode(ImageView::ViewMode::Image);
        QVERIFY(view.isImageMode());
        QVERIFY(view.hostFraming().isFitMode());

        QImage hostA(m_pathA);
        QVERIFY(!hostA.isNull());
        view.hostDisplayPipeline().installImageModeReplaceItem(m_pathA, hostA);
        QVERIFY(!view.liveItems().isEmpty());
        ImageItem *imgItem = view.liveItems().first();
        QVERIFY(imgItem != nullptr);
        QVERIFY(imgItem->hasDisplayPixels());
        QCOMPARE(imgItem->path(), m_pathA);
        // Crop want still on focus in ItemWorld → layout is crop box.
        QCOMPARE(ContentXform::layoutSize(QSize(64, 48),
                                          view.itemWorld().appearanceValue(focus)),
                 QSize(32, 24));

        view.fitItem(imgItem, Qt::KeepAspectRatio);
        const qreal imgFitScale = ViewTransform::scaleFrom(view.transform());
        QVERIFY(imgFitScale > 0.0);
        view.captureStickyPanAnchor(imgItem);
        QVERIFY(view.hostFraming().hasPreservedViewScale()
                || view.hostFraming().hasStickyPan());

        // Navigate-style replace to sibling session id + path (sync soft sample).
        view.setCurrentSessionId(other);
        QImage hostB(m_pathB);
        QVERIFY(!hostB.isNull());
        view.hostDisplayPipeline().installImageModeReplaceItem(m_pathB, hostB);
        QVERIFY(!view.liveItems().isEmpty());
        ImageItem *imgOther = view.liveItems().first();
        QVERIFY(imgOther != nullptr);
        QVERIFY(imgOther->hasDisplayPixels());
        QCOMPARE(imgOther->path(), m_pathB);
        // Sibling has no crop in ItemWorld.
        QVERIFY(!view.itemWorld().hasCrop(other));
        view.fitItem(imgOther, Qt::KeepAspectRatio);
        QVERIFY(ViewTransform::scaleFrom(view.transform()) > 0.0);
        view.restoreStickyPanAnchor(imgOther);
        QVERIFY(ViewTransform::scaleFrom(view.transform()) > 0.0);

        // Focus durable ItemWorld components survived Image-mode navigate (id-keyed).
        // Applied ContentXform is presentation-only: flushAppliedContentToItemWorld on
        // Gallery→Image commits it into contentBake and clears the residual table
        // (ECS — underlay must not materialize from stale applied dual-write).
        QVERIFY(view.itemWorld().hasCrop(focus));
        QVERIFY(view.itemWorld().hasContentBake(focus));
        QVERIFY(!view.itemWorld().hasAppliedContentXform(focus));
        QVERIFY(view.itemWorld().hasLiveColorLag(focus));
    }

    // Mode-leave style clear: pack blank; durable id-keyed components intact.
    view.pathOrderClear();
    QVERIFY(view.pathOrderIsEmpty());
    QCOMPARE(doc.size(), 2);
    QVERIFY(view.itemWorld().hasCrop(focus));
    QVERIFY(view.itemWorld().hasPlacement(focus));
    QVERIFY(view.itemWorld().hasContentBake(focus));
    QVERIFY(view.itemWorld().hasColor(focus));
    QVERIFY(view.itemWorld().hasAttention(focus));
    QVERIFY(!view.itemWorld().hasAppliedContentXform(focus));
    QVERIFY(view.itemWorld().hasLiveColorLag(focus));
    // Mid-edit lag (brightness 5) does not outlive mode leave: underlay install
    // re-seeds live lag from durable Color via attachDisplaySample /
    // syncLiveColorFromState. Lag settles to durable grade (8).
    QCOMPARE(view.itemWorld().liveColorLag(focus).brightness, 8);
    // Content isolation: sibling must not share focus crop/bake/color/attention.
    QVERIFY(!view.itemWorld().hasCrop(other));
    QVERIFY(!view.itemWorld().hasContentBake(other));
    QVERIFY(!view.itemWorld().hasColor(other));
    QVERIFY(!view.itemWorld().hasAttention(other));
    QVERIFY(!view.itemWorld().hasAppliedContentXform(other));
    // Gallery pack may leave Placement on the sibling — not a content leak.
    QCOMPARE(view.itemWorld().placement(focus).pos, QPointF(40.0, 60.0));
    // contentBake may include flushed applied (turns) — at least non-identity.
    QVERIFY(view.itemWorld().contentBake(focus).quarterTurns != 0
            || view.itemWorld().contentBake(focus).vFlip);
    QCOMPARE(view.itemWorld().color(focus).grade.brightness, 8);
    QCOMPARE(view.itemWorld().attention(focus).points.size(), 1);

    // LoadAdd multiplicity on the view overlay only.
    view.pathOrderSetOrder({m_pathA, m_pathA, m_pathA}, {focus, focus, focus});
    QCOMPARE(view.pathOrderOccurrences(m_pathA), 3);
    QCOMPARE(doc.size(), 2);
    QCOMPARE(doc.countPathOccurrences(m_pathA), 1);
#endif
}

QTEST_MAIN(ImageViewCharacterizationTest)
#include "imageview_characterization.moc"
