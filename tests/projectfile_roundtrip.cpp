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
    if (!nearlyEqual(a.rotation, b.rotation) || !nearlyEqual(a.opacity, b.opacity)
        || !nearlyEqual(a.z, b.z)) {
        return false;
    }
    if (a.hFlip != b.hFlip || a.vFlip != b.vFlip) {
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
    void project_saveLoad_semantic();
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
    QVERIFY(nearlyEqual(back.rotation, s.rotation));
    QCOMPARE(back.vFlip, s.vFlip);
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
    asset.path = QStringLiteral("/tmp/qimgview-test/photo.jpg");
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
    const QString path = dir.filePath(QStringLiteral("roundtrip.qimgview"));

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
    doc.images.append(im);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p1 = dir.filePath(QStringLiteral("a.qimgview"));
    const QString p2 = dir.filePath(QStringLiteral("b.qimgview"));

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
    const QString path = dir.filePath(QStringLiteral("empty.qimgview"));
    QString err;
    QVERIFY(ProjectFile::save(path, doc, &err));
    ProjectDocument loaded;
    QVERIFY(ProjectFile::load(path, &loaded, &err));
    QCOMPARE(loaded.version, 1);
    QVERIFY(loaded.assets.isEmpty());
    QVERIFY(loaded.images.isEmpty());
}

QTEST_MAIN(ProjectFileRoundTripTest)
#include "projectfile_roundtrip.moc"
