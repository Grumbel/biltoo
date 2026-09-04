// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "projectfile.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

// ProjectAsset / ProjectDocument / ProjectImage are global types in projectfile.h;
// only the free functions live in namespace ProjectFile.

static bool nearlyEqual(qreal a, qreal b, qreal eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

static bool colorAdjustEqual(const ColorAdjustments &a, const ColorAdjustments &b)
{
    return a.brightness == b.brightness && a.contrast == b.contrast
           && a.saturation == b.saturation && a.hue == b.hue
           && nearlyEqual(a.gamma, b.gamma);
}

static bool appearanceEqual(const WorkspaceItemState &a, const WorkspaceItemState &b,
                            bool pose)
{
    if (a.hasCrop != b.hasCrop) {
        return false;
    }
    if (a.hasCrop) {
        if (a.cropRect != b.cropRect) {
            return false;
        }
        if (a.cropSourceSize != b.cropSourceSize) {
            return false;
        }
        if (!nearlyEqual(a.cropRotation, b.cropRotation)) {
            return false;
        }
    }
    if (a.contentHFlip != b.contentHFlip || a.contentVFlip != b.contentVFlip) {
        return false;
    }
    if (a.contentQuarterTurns != b.contentQuarterTurns) {
        return false;
    }
    if (!colorAdjustEqual(a.colorAdjust, b.colorAdjust)) {
        return false;
    }
    if (!pose) {
        return true;
    }
    if (!nearlyEqual(a.pos.x(), b.pos.x()) || !nearlyEqual(a.pos.y(), b.pos.y())) {
        return false;
    }
    if (!nearlyEqual(a.scale, b.scale)
        || !nearlyEqual(a.scaleY > 0 ? a.scaleY : a.scale,
                        b.scaleY > 0 ? b.scaleY : b.scale)) {
        return false;
    }
    if (!nearlyEqual(a.shear, b.shear)) {
        return false;
    }
    if (!nearlyEqual(a.rotation, b.rotation) || !nearlyEqual(a.opacity, b.opacity)
        || !nearlyEqual(a.z, b.z)) {
        return false;
    }
    if (a.hFlip != b.hFlip || a.vFlip != b.vFlip) {
        return false;
    }
    return true;
}

static bool workspaceBackgroundEqual(const WorkspaceBackground &a,
                                     const WorkspaceBackground &b)
{
    if (a.mode != b.mode) {
        return false;
    }
    if (a.color != b.color || a.colorAlt != b.colorAlt) {
        return false;
    }
    if (a.imagePath != b.imagePath || a.imagePathRelative != b.imagePathRelative
        || a.imageSha256 != b.imageSha256) {
        return false;
    }
    return true;
}

static bool documentsEqual(const ProjectDocument &a, const ProjectDocument &b)
{
    if (a.version != b.version || a.mode != b.mode) {
        return false;
    }
    if (a.pageGuideVisible != b.pageGuideVisible) {
        return false;
    }
    if (a.pageGuideSizeMm.isValid() != b.pageGuideSizeMm.isValid()) {
        return false;
    }
    if (a.pageGuideSizeMm.isValid()) {
        if (!nearlyEqual(a.pageGuideSizeMm.width(), b.pageGuideSizeMm.width())
            || !nearlyEqual(a.pageGuideSizeMm.height(), b.pageGuideSizeMm.height())) {
            return false;
        }
    }
    // Project-owned background: either side may omit AppDefault; non-default must match.
    const bool aBg = a.hasWorkspaceBackground && !a.workspaceBackground.isAppDefault();
    const bool bBg = b.hasWorkspaceBackground && !b.workspaceBackground.isAppDefault();
    if (aBg != bBg) {
        return false;
    }
    if (aBg && !workspaceBackgroundEqual(a.workspaceBackground, b.workspaceBackground)) {
        return false;
    }
    if (a.assets.size() != b.assets.size() || a.images.size() != b.images.size()) {
        return false;
    }
    for (int i = 0; i < a.assets.size(); ++i) {
        if (a.assets[i].sha256 != b.assets[i].sha256
            || a.assets[i].path != b.assets[i].path
            || a.assets[i].pathRelative != b.assets[i].pathRelative) {
            return false;
        }
    }
    for (int i = 0; i < a.images.size(); ++i) {
        const ProjectImage &ai = a.images[i];
        const ProjectImage &bi = b.images[i];
        if (ai.id != bi.id || ai.assetSha256 != bi.assetSha256) {
            return false;
        }
        if (ai.hasWorkspacePose != bi.hasWorkspacePose) {
            return false;
        }
        // After load, hasAppearance is true whenever an appearance object was present.
        if (ai.hasAppearance || ai.hasWorkspacePose) {
            if (!bi.hasAppearance && !bi.hasWorkspacePose) {
                return false;
            }
            if (!appearanceEqual(ai.appearance, bi.appearance, ai.hasWorkspacePose)) {
                return false;
            }
        }
    }
    return true;
}

class ProjectFileRoundTripTest : public QObject
{
    Q_OBJECT
private slots:
    void appearanceJson_cropRotation();
    void appearanceJson_colorGrade();
    void project_saveLoad_semantic();
    void project_saveLoad_colorAndBackground();
    void project_saveLoadSave_jsonStable();
    void project_emptyMinimal();
};

void ProjectFileRoundTripTest::appearanceJson_cropRotation()
{
    WorkspaceItemState s;
    s.hasCrop = true;
    s.cropRect = QRect(10, 20, 100, 80);
    s.cropSourceSize = QSize(640, 480);
    s.cropRotation = -22.5;
    s.contentHFlip = true;
    s.contentQuarterTurns = 1;
    s.pos = QPointF(12.5, 40.0);
    s.scale = 1.25;
    s.scaleY = 1.5;
    s.shear = 0.35;
    s.rotation = 15.0;
    s.opacity = 0.8;
    s.z = 3.0;
    s.hFlip = false;
    s.vFlip = true;

    const QJsonObject o = ProjectFile::appearanceToJson(s, /*includePose=*/true);
    const WorkspaceItemState back = ProjectFile::appearanceFromJson(o);

    QVERIFY(back.hasCrop);
    QCOMPARE(back.cropRect, s.cropRect);
    QCOMPARE(back.cropSourceSize, s.cropSourceSize);
    QVERIFY(nearlyEqual(back.cropRotation, s.cropRotation));
    QCOMPARE(back.contentHFlip, s.contentHFlip);
    QCOMPARE(back.contentQuarterTurns, s.contentQuarterTurns);
    QVERIFY(nearlyEqual(back.pos.x(), s.pos.x()));
    QVERIFY(nearlyEqual(back.scaleY, s.scaleY));
    QVERIFY(nearlyEqual(back.shear, s.shear));
    QVERIFY(nearlyEqual(back.rotation, s.rotation));
    QCOMPARE(back.vFlip, s.vFlip);
    QVERIFY(colorAdjustEqual(back.colorAdjust, ColorAdjustments{}));
}

void ProjectFileRoundTripTest::appearanceJson_colorGrade()
{
    WorkspaceItemState s;
    s.colorAdjust.brightness = -20;
    s.colorAdjust.contrast = 140;
    s.colorAdjust.saturation = 80;
    s.colorAdjust.hue = 15;
    s.colorAdjust.gamma = 1.35;
    s.hasCrop = true;
    s.cropRect = QRect(1, 2, 3, 4);
    s.cropSourceSize = QSize(100, 50);
    s.cropRotation = 5.0;
    s.contentVFlip = true;
    s.pos = QPointF(1.0, 2.0);
    s.scale = 2.0;
    s.scaleY = 2.5;
    s.rotation = -3.0;
    s.opacity = 0.9;
    s.z = 7.0;

    const QJsonObject o = ProjectFile::appearanceToJson(s, /*includePose=*/true);
    QVERIFY(o.contains(QStringLiteral("colorBrightness")));
    QVERIFY(o.contains(QStringLiteral("colorContrast")));
    QVERIFY(o.contains(QStringLiteral("colorSaturation")));
    QVERIFY(o.contains(QStringLiteral("colorHue")));
    QVERIFY(o.contains(QStringLiteral("colorGamma")));

    const WorkspaceItemState back = ProjectFile::appearanceFromJson(o);
    QVERIFY(colorAdjustEqual(back.colorAdjust, s.colorAdjust));
    QCOMPARE(back.colorAdjust.brightness, -20);
    QCOMPARE(back.colorAdjust.contrast, 140);
    QCOMPARE(back.colorAdjust.saturation, 80);
    QCOMPARE(back.colorAdjust.hue, 15);
    QVERIFY(nearlyEqual(back.colorAdjust.gamma, 1.35));
    QVERIFY(back.hasCrop);
    QCOMPARE(back.cropRect, s.cropRect);
    QVERIFY(nearlyEqual(back.cropRotation, 5.0));
    QCOMPARE(back.contentVFlip, true);
    QVERIFY(nearlyEqual(back.pos.x(), 1.0));
    QVERIFY(nearlyEqual(back.scaleY, 2.5));

    // Identity grade must be omitted from JSON and restore to defaults.
    WorkspaceItemState idState;
    idState.contentHFlip = true;
    const QJsonObject idObj = ProjectFile::appearanceToJson(idState, false);
    QVERIFY(!idObj.contains(QStringLiteral("colorBrightness")));
    QVERIFY(!idObj.contains(QStringLiteral("colorGamma")));
    const WorkspaceItemState idBack = ProjectFile::appearanceFromJson(idObj);
    QVERIFY(idBack.colorAdjust.isIdentity());
    QCOMPARE(idBack.contentHFlip, true);
}

void ProjectFileRoundTripTest::project_saveLoad_semantic()
{
    ProjectDocument doc;
    doc.version = 1;
    doc.mode = QStringLiteral("workspace");
    doc.pageGuideVisible = true;
    doc.pageGuideSizeMm = QSizeF(210.0, 297.0);

    ProjectAsset asset;
    asset.sha256 = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    asset.path = QStringLiteral("/tmp/biltoo-test/photo.jpg");
    asset.pathRelative = QStringLiteral("photo.jpg");
    doc.assets.append(asset);

    ProjectImage im;
    im.id = 42;
    im.assetSha256 = asset.sha256;
    im.hasAppearance = true;
    im.hasWorkspacePose = true;
    im.appearance.hasCrop = true;
    im.appearance.cropRect = QRect(5, 5, 200, 150);
    im.appearance.cropSourceSize = QSize(800, 600);
    im.appearance.cropRotation = 30.0;
    im.appearance.contentVFlip = true;
    im.appearance.pos = QPointF(100.0, 200.0);
    im.appearance.scale = 0.5;
    im.appearance.scaleY = 0.5;
    im.appearance.rotation = -7.5;
    im.appearance.opacity = 1.0;
    im.appearance.z = 1.0;
    doc.images.append(im);

    ProjectImage im2;
    im2.id = 43;
    im2.assetSha256 = asset.sha256;
    im2.hasAppearance = true;
    im2.hasWorkspacePose = false;
    im2.appearance.hasCrop = false;
    im2.appearance.contentQuarterTurns = 2;
    doc.images.append(im2);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.biltoo"));

    QString err;
    QVERIFY2(ProjectFile::save(path, doc, &err), qPrintable(err));

    ProjectDocument loaded;
    QVERIFY2(ProjectFile::load(path, &loaded, &err), qPrintable(err));
    QVERIFY2(documentsEqual(doc, loaded), "loaded document differs semantically");

    QCOMPARE(loaded.images.size(), 2);
    QVERIFY(nearlyEqual(loaded.images[0].appearance.cropRotation, 30.0));
    QCOMPARE(loaded.images[0].appearance.cropRect, QRect(5, 5, 200, 150));
    QCOMPARE(loaded.images[1].appearance.contentQuarterTurns, 2);
}

void ProjectFileRoundTripTest::project_saveLoad_colorAndBackground()
{
    ProjectDocument doc;
    doc.version = 1;
    doc.mode = QStringLiteral("workspace");
    doc.pageGuideVisible = false;

    ProjectAsset asset;
    asset.sha256 = QStringLiteral("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
    asset.path = QStringLiteral("/tmp/biltoo-test/graded.png");
    asset.pathRelative = QStringLiteral("graded.png");
    doc.assets.append(asset);

    ProjectImage im;
    im.id = 7;
    im.assetSha256 = asset.sha256;
    im.hasAppearance = true;
    im.hasWorkspacePose = true;
    im.appearance.hasCrop = true;
    im.appearance.cropRect = QRect(10, 20, 300, 200);
    im.appearance.cropSourceSize = QSize(1920, 1080);
    im.appearance.cropRotation = -12.5;
    im.appearance.contentHFlip = true;
    im.appearance.contentQuarterTurns = 3;
    im.appearance.colorAdjust.brightness = 10;
    im.appearance.colorAdjust.contrast = 110;
    im.appearance.colorAdjust.saturation = 90;
    im.appearance.colorAdjust.hue = -5;
    im.appearance.colorAdjust.gamma = 0.85;
    im.appearance.pos = QPointF(50.5, -10.25);
    im.appearance.scale = 1.1;
    im.appearance.scaleY = 0.95;
    im.appearance.shear = 0.42;
    im.appearance.rotation = 22.0;
    im.appearance.opacity = 0.75;
    im.appearance.z = 4.0;
    im.appearance.hFlip = true;
    doc.images.append(im);

    // Session-only row (no pose) still keeps content grade + crop.
    ProjectImage im2;
    im2.id = 8;
    im2.assetSha256 = asset.sha256;
    im2.hasAppearance = true;
    im2.hasWorkspacePose = false;
    im2.appearance.colorAdjust.brightness = -5;
    im2.appearance.colorAdjust.gamma = 1.1;
    im2.appearance.contentVFlip = true;
    doc.images.append(im2);

    doc.hasWorkspaceBackground = true;
    doc.workspaceBackground.mode = WorkspaceBackgroundMode::Checkerboard;
    doc.workspaceBackground.color = QColor(30, 40, 50);
    doc.workspaceBackground.colorAlt = QColor(60, 70, 80);
    doc.workspaceBackground.imagePath = QStringLiteral("/tmp/biltoo-test/tile.png");
    doc.workspaceBackground.imagePathRelative = QStringLiteral("tile.png");
    doc.workspaceBackground.imageSha256 =
        QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("color-bg.biltoo"));

    QString err;
    QVERIFY2(ProjectFile::save(path, doc, &err), qPrintable(err));

    ProjectDocument loaded;
    QVERIFY2(ProjectFile::load(path, &loaded, &err), qPrintable(err));
    QVERIFY2(documentsEqual(doc, loaded), "colour grade / background round-trip failed");

    QCOMPARE(loaded.images.size(), 2);
    QVERIFY(colorAdjustEqual(loaded.images[0].appearance.colorAdjust, im.appearance.colorAdjust));
    QVERIFY(colorAdjustEqual(loaded.images[1].appearance.colorAdjust, im2.appearance.colorAdjust));
    QCOMPARE(loaded.images[0].appearance.cropRect, QRect(10, 20, 300, 200));
    QVERIFY(nearlyEqual(loaded.images[0].appearance.cropRotation, -12.5));
    QVERIFY(nearlyEqual(loaded.images[0].appearance.pos.x(), 50.5));
    QVERIFY(nearlyEqual(loaded.images[0].appearance.scaleY, 0.95));
    QVERIFY(nearlyEqual(loaded.images[0].appearance.shear, 0.42));
    QCOMPARE(loaded.images[0].appearance.contentHFlip, true);
    QCOMPARE(loaded.images[0].appearance.contentQuarterTurns, 3);

    QVERIFY(loaded.hasWorkspaceBackground);
    QCOMPARE(loaded.workspaceBackground.mode, WorkspaceBackgroundMode::Checkerboard);
    QCOMPARE(loaded.workspaceBackground.color, QColor(30, 40, 50));
    QCOMPARE(loaded.workspaceBackground.colorAlt, QColor(60, 70, 80));
    QCOMPARE(loaded.workspaceBackground.imagePath, QStringLiteral("/tmp/biltoo-test/tile.png"));
    QCOMPARE(loaded.workspaceBackground.imagePathRelative, QStringLiteral("tile.png"));
    QCOMPARE(loaded.workspaceBackground.imageSha256,
             QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

    // Solid background round-trip (no image fields required).
    ProjectDocument solidDoc = doc;
    solidDoc.workspaceBackground.mode = WorkspaceBackgroundMode::Solid;
    solidDoc.workspaceBackground.color = QColor(1, 2, 3, 255);
    solidDoc.workspaceBackground.imagePath.clear();
    solidDoc.workspaceBackground.imagePathRelative.clear();
    solidDoc.workspaceBackground.imageSha256.clear();
    const QString solidPath = dir.filePath(QStringLiteral("solid-bg.biltoo"));
    QVERIFY2(ProjectFile::save(solidPath, solidDoc, &err), qPrintable(err));
    ProjectDocument solidLoaded;
    QVERIFY2(ProjectFile::load(solidPath, &solidLoaded, &err), qPrintable(err));
    QVERIFY2(documentsEqual(solidDoc, solidLoaded), "solid background round-trip failed");
    QCOMPARE(solidLoaded.workspaceBackground.mode, WorkspaceBackgroundMode::Solid);
    QCOMPARE(solidLoaded.workspaceBackground.color, QColor(1, 2, 3, 255));

    // AppDefault must not be written as a project override after load.
    ProjectDocument defaultDoc;
    defaultDoc.version = 1;
    defaultDoc.mode = QStringLiteral("workspace");
    defaultDoc.hasWorkspaceBackground = false;
    defaultDoc.workspaceBackground = WorkspaceBackground{};
    const QString defaultPath = dir.filePath(QStringLiteral("default-bg.biltoo"));
    QVERIFY2(ProjectFile::save(defaultPath, defaultDoc, &err), qPrintable(err));
    ProjectDocument defaultLoaded;
    QVERIFY2(ProjectFile::load(defaultPath, &defaultLoaded, &err), qPrintable(err));
    QVERIFY(!defaultLoaded.hasWorkspaceBackground
            || defaultLoaded.workspaceBackground.isAppDefault());
}

void ProjectFileRoundTripTest::project_saveLoadSave_jsonStable()
{
    ProjectDocument doc;
    doc.version = 1;
    doc.mode = QStringLiteral("image");
    ProjectAsset asset;
    asset.sha256 = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    asset.path = QStringLiteral("/data/a.png");
    doc.assets.append(asset);
    ProjectImage im;
    im.id = 1;
    im.assetSha256 = asset.sha256;
    im.hasAppearance = true;
    im.appearance.hasCrop = true;
    im.appearance.cropRect = QRect(0, 0, 10, 10);
    im.appearance.cropSourceSize = QSize(10, 10);
    im.appearance.cropRotation = 0.0; // omitted from JSON when ~0
    im.appearance.colorAdjust.brightness = 5;
    im.appearance.colorAdjust.gamma = 1.2;
    doc.images.append(im);
    doc.hasWorkspaceBackground = true;
    doc.workspaceBackground.mode = WorkspaceBackgroundMode::Solid;
    doc.workspaceBackground.color = QColor(10, 20, 30);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("a.biltoo"));
    const QString p2 = dir.filePath(QStringLiteral("b.biltoo"));

    QString err;
    QVERIFY(ProjectFile::save(p1, doc, &err));
    ProjectDocument loaded;
    QVERIFY(ProjectFile::load(p1, &loaded, &err));
    QVERIFY(ProjectFile::save(p2, loaded, &err));

    QFile f1(p1);
    QFile f2(p2);
    QVERIFY(f1.open(QIODevice::ReadOnly));
    QVERIFY(f2.open(QIODevice::ReadOnly));
    const QByteArray j1 = f1.readAll();
    const QByteArray j2 = f2.readAll();
    QCOMPARE(j2, j1);
}

void ProjectFileRoundTripTest::project_emptyMinimal()
{
    ProjectDocument doc;
    doc.version = 1;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("empty.biltoo"));
    QString err;
    QVERIFY(ProjectFile::save(path, doc, &err));
    ProjectDocument loaded;
    QVERIFY(ProjectFile::load(path, &loaded, &err));
    QCOMPARE(loaded.version, 1);
    QVERIFY(loaded.assets.isEmpty());
    QVERIFY(loaded.images.isEmpty());
    QVERIFY(!loaded.hasWorkspaceBackground || loaded.workspaceBackground.isAppDefault());
}

QTEST_MAIN(ProjectFileRoundTripTest)
#include "projectfile_roundtrip.moc"
