// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "projectfile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonObject>

namespace ProjectFile {

QString fileSha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr qint64 kChunk = 1024 * 1024;
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(kChunk);
        if (chunk.isEmpty() && !f.atEnd()) {
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

static bool hashMatches(const QString &path, const QString &expectedHex)
{
    if (expectedHex.isEmpty() || path.isEmpty()) {
        return false;
    }
    const QString actual = fileSha256(path);
    return !actual.isEmpty() && actual.compare(expectedHex, Qt::CaseInsensitive) == 0;
}

QString resolveAssetPath(const ProjectAsset &asset, const QString &projectFilePath,
                         QString *error)
{
    const QFileInfo proj(projectFilePath);
    const QDir projDir = proj.absoluteDir();

    auto tryPath = [&](const QString &candidate) -> QString {
        if (candidate.isEmpty()) {
            return {};
        }
        const QFileInfo fi(candidate);
        if (!fi.isFile()) {
            return {};
        }
        const QString abs = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath()
                                                             : fi.canonicalFilePath();
        if (asset.sha256.isEmpty() || hashMatches(abs, asset.sha256)) {
            return abs;
        }
        return {};
    };

    // 1) Absolute path from save time.
    if (QString r = tryPath(asset.path); !r.isEmpty()) {
        return r;
    }
    // 2) Relative to project file.
    if (!asset.pathRelative.isEmpty()) {
        if (QString r = tryPath(projDir.absoluteFilePath(asset.pathRelative)); !r.isEmpty()) {
            return r;
        }
    }
    // 3) Content-addressed search under the project directory (depth-limited).
    if (!asset.sha256.isEmpty() && projDir.exists()) {
        QDirIterator it(projDir.absolutePath(), QDir::Files,
                        QDirIterator::Subdirectories);
        int checked = 0;
        constexpr int kMaxFiles = 5000;
        while (it.hasNext() && checked < kMaxFiles) {
            const QString p = it.next();
            ++checked;
            if (hashMatches(p, asset.sha256)) {
                return QFileInfo(p).canonicalFilePath().isEmpty()
                    ? QFileInfo(p).absoluteFilePath()
                    : QFileInfo(p).canonicalFilePath();
            }
        }
    }

    if (error) {
        *error = QObject::tr("Missing or modified image (sha256=%1, path=%2)")
                     .arg(asset.sha256.left(12), asset.path);
    }
    return {};
}

QJsonObject appearanceToJson(const WorkspaceItemState &s, bool includePose)
{
    QJsonObject o;
    if (s.hasCrop) {
        o.insert(QStringLiteral("hasCrop"), true);
        o.insert(QStringLiteral("crop"),
                 QJsonArray{s.cropRect.x(), s.cropRect.y(), s.cropRect.width(),
                            s.cropRect.height()});
        if (s.cropSourceSize.isValid()) {
            o.insert(QStringLiteral("cropSource"),
                     QJsonArray{s.cropSourceSize.width(), s.cropSourceSize.height()});
        }
        if (!qFuzzyIsNull(s.cropRotation)) {
            o.insert(QStringLiteral("cropRotation"), s.cropRotation);
        }
    }
    if (s.contentHFlip) {
        o.insert(QStringLiteral("contentHFlip"), true);
    }
    if (s.contentVFlip) {
        o.insert(QStringLiteral("contentVFlip"), true);
    }
    if (s.contentQuarterTurns != 0) {
        o.insert(QStringLiteral("contentQuarterTurns"), s.contentQuarterTurns);
    }
    if (!s.colorAdjust.isIdentity()) {
        o.insert(QStringLiteral("colorBrightness"), s.colorAdjust.brightness);
        o.insert(QStringLiteral("colorContrast"), s.colorAdjust.contrast);
        o.insert(QStringLiteral("colorSaturation"), s.colorAdjust.saturation);
        o.insert(QStringLiteral("colorHue"), s.colorAdjust.hue);
        o.insert(QStringLiteral("colorGamma"), s.colorAdjust.gamma);
    }
    if (includePose) {
        o.insert(QStringLiteral("x"), s.pos.x());
        o.insert(QStringLiteral("y"), s.pos.y());
        o.insert(QStringLiteral("scaleX"), s.scale);
        o.insert(QStringLiteral("scaleY"), s.scaleY > 0.0 ? s.scaleY : s.scale);
        o.insert(QStringLiteral("rotation"), s.rotation);
        o.insert(QStringLiteral("opacity"), s.opacity);
        o.insert(QStringLiteral("z"), s.z);
        if (s.hFlip) {
            o.insert(QStringLiteral("hFlip"), true);
        }
        if (s.vFlip) {
            o.insert(QStringLiteral("vFlip"), true);
        }
    }
    return o;
}

WorkspaceItemState appearanceFromJson(const QJsonObject &o)
{
    WorkspaceItemState s;
    s.hasCrop = o.value(QStringLiteral("hasCrop")).toBool(false);
    if (o.contains(QStringLiteral("crop"))) {
        const QJsonArray a = o.value(QStringLiteral("crop")).toArray();
        if (a.size() >= 4) {
            s.cropRect = QRect(a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt(),
                               a.at(3).toInt());
        }
    }
    s.cropRotation = o.value(QStringLiteral("cropRotation")).toDouble(0.0);
    if (o.contains(QStringLiteral("cropSource"))) {
        const QJsonArray a = o.value(QStringLiteral("cropSource")).toArray();
        if (a.size() >= 2) {
            s.cropSourceSize = QSize(a.at(0).toInt(), a.at(1).toInt());
        }
    }
    s.contentHFlip = o.value(QStringLiteral("contentHFlip")).toBool(false);
    s.contentVFlip = o.value(QStringLiteral("contentVFlip")).toBool(false);
    s.contentQuarterTurns = o.value(QStringLiteral("contentQuarterTurns")).toInt(0);
    s.colorAdjust.brightness = o.value(QStringLiteral("colorBrightness")).toInt(0);
    s.colorAdjust.contrast = o.value(QStringLiteral("colorContrast")).toInt(100);
    s.colorAdjust.saturation = o.value(QStringLiteral("colorSaturation")).toInt(100);
    s.colorAdjust.hue = o.value(QStringLiteral("colorHue")).toInt(0);
    s.colorAdjust.gamma = o.value(QStringLiteral("colorGamma")).toDouble(1.0);
    if (o.contains(QStringLiteral("x")) || o.contains(QStringLiteral("y"))) {
        s.pos = QPointF(o.value(QStringLiteral("x")).toDouble(),
                        o.value(QStringLiteral("y")).toDouble());
    }
    s.scale = o.value(QStringLiteral("scaleX")).toDouble(1.0);
    s.scaleY = o.value(QStringLiteral("scaleY")).toDouble(s.scale);
    s.rotation = o.value(QStringLiteral("rotation")).toDouble(0.0);
    s.opacity = o.value(QStringLiteral("opacity")).toDouble(1.0);
    s.z = o.value(QStringLiteral("z")).toDouble(0.0);
    s.hFlip = o.value(QStringLiteral("hFlip")).toBool(false);
    s.vFlip = o.value(QStringLiteral("vFlip")).toBool(false);
    return s;
}

bool save(const QString &projectPath, const ProjectDocument &doc, QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("qimgview-project"));
    root.insert(QStringLiteral("version"), doc.version > 0 ? doc.version : 1);
    if (!doc.mode.isEmpty()) {
        root.insert(QStringLiteral("mode"), doc.mode);
    }

    QJsonArray assets;
    for (const ProjectAsset &a : doc.assets) {
        QJsonObject o;
        o.insert(QStringLiteral("sha256"), a.sha256);
        o.insert(QStringLiteral("path"), a.path);
        if (!a.pathRelative.isEmpty()) {
            o.insert(QStringLiteral("pathRelative"), a.pathRelative);
        }
        assets.append(o);
    }
    root.insert(QStringLiteral("assets"), assets);

    QJsonArray images;
    for (const ProjectImage &im : doc.images) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), static_cast<qint64>(im.id));
        o.insert(QStringLiteral("asset"), im.assetSha256);
        if (im.hasAppearance || im.hasWorkspacePose) {
            o.insert(QStringLiteral("appearance"),
                     appearanceToJson(im.appearance, im.hasWorkspacePose));
        }
        if (im.hasWorkspacePose) {
            o.insert(QStringLiteral("workspace"), true);
        }
        images.append(o);
    }
    root.insert(QStringLiteral("images"), images);

    if (doc.pageGuideVisible || doc.pageGuideSizeMm.isValid()) {
        QJsonObject pg;
        pg.insert(QStringLiteral("visible"), doc.pageGuideVisible);
        if (doc.pageGuideSizeMm.isValid()) {
            pg.insert(QStringLiteral("widthMm"), doc.pageGuideSizeMm.width());
            pg.insert(QStringLiteral("heightMm"), doc.pageGuideSizeMm.height());
        }
        root.insert(QStringLiteral("pageGuide"), pg);
    }

    const QJsonDocument jd(root);
    QFile f(projectPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QObject::tr("Cannot write %1: %2").arg(projectPath, f.errorString());
        }
        return false;
    }
    f.write(jd.toJson(QJsonDocument::Indented));
    return true;
}

bool load(const QString &projectPath, ProjectDocument *doc, QString *error)
{
    if (!doc) {
        return false;
    }
    QFile f(projectPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("Cannot read %1: %2").arg(projectPath, f.errorString());
        }
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !jd.isObject()) {
        if (error) {
            *error = QObject::tr("Invalid project JSON: %1").arg(pe.errorString());
        }
        return false;
    }
    const QJsonObject root = jd.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qimgview-project")) {
        if (error) {
            *error = QObject::tr("Not a qimgview project file.");
        }
        return false;
    }
    doc->version = root.value(QStringLiteral("version")).toInt(1);
    doc->mode = root.value(QStringLiteral("mode")).toString();
    doc->assets.clear();
    doc->images.clear();

    for (const QJsonValue &v : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject o = v.toObject();
        ProjectAsset a;
        a.sha256 = o.value(QStringLiteral("sha256")).toString();
        a.path = o.value(QStringLiteral("path")).toString();
        a.pathRelative = o.value(QStringLiteral("pathRelative")).toString();
        doc->assets.append(a);
    }

    for (const QJsonValue &v : root.value(QStringLiteral("images")).toArray()) {
        const QJsonObject o = v.toObject();
        ProjectImage im;
        im.id = static_cast<SessionImageId>(o.value(QStringLiteral("id")).toVariant().toLongLong());
        im.assetSha256 = o.value(QStringLiteral("asset")).toString();
        im.hasWorkspacePose = o.value(QStringLiteral("workspace")).toBool(false);
        if (o.contains(QStringLiteral("appearance"))) {
            im.appearance = appearanceFromJson(o.value(QStringLiteral("appearance")).toObject());
            im.hasAppearance = true;
            if (o.value(QStringLiteral("appearance")).toObject().contains(QStringLiteral("x"))) {
                im.hasWorkspacePose = true;
            }
        }
        doc->images.append(im);
    }

    if (root.contains(QStringLiteral("pageGuide"))) {
        const QJsonObject pg = root.value(QStringLiteral("pageGuide")).toObject();
        doc->pageGuideVisible = pg.value(QStringLiteral("visible")).toBool(false);
        if (pg.contains(QStringLiteral("widthMm")) && pg.contains(QStringLiteral("heightMm"))) {
            doc->pageGuideSizeMm = QSizeF(pg.value(QStringLiteral("widthMm")).toDouble(),
                                          pg.value(QStringLiteral("heightMm")).toDouble());
        }
    }
    return true;
}

} // namespace ProjectFile
