// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/projectfile.h"
#include "attention/attentiongeometry.h"

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

/**
 * Stage 4b on-disk appearance (projectFormat ≥ 2): nested sparse objects only.
 * No flat v1 keys (hasCrop, scaleX, colorBrightness, …). No backward reader.
 *
 *   "crop": { x,y,w,h [, sourceW, sourceH] [, rotation] }
 *   "attention": [ [x,y], … ]
 *   "bake": { [hFlip] [, vFlip] [, quarterTurns] }
 *   "color": { brightness, contrast, saturation, hue, gamma }
 *   "placement": { x, y, scaleX, scaleY [, shear] [, rotation] [, opacity] [, z]
 *                  [, hFlip] [, vFlip] }
 */
QJsonObject appearanceToJson(const WorkspaceItemState &s, bool includePose)
{
    QJsonObject o;
    if (s.hasCrop && !s.cropRect.isEmpty()) {
        QJsonObject crop;
        crop.insert(QStringLiteral("x"), s.cropRect.x());
        crop.insert(QStringLiteral("y"), s.cropRect.y());
        crop.insert(QStringLiteral("w"), s.cropRect.width());
        crop.insert(QStringLiteral("h"), s.cropRect.height());
        if (s.cropSourceSize.isValid()) {
            crop.insert(QStringLiteral("sourceW"), s.cropSourceSize.width());
            crop.insert(QStringLiteral("sourceH"), s.cropSourceSize.height());
        }
        if (!qFuzzyIsNull(s.cropRotation)) {
            crop.insert(QStringLiteral("rotation"), s.cropRotation);
        }
        o.insert(QStringLiteral("crop"), crop);
    }
    if (s.hasAttention || !s.attentionPoints.isEmpty()) {
        QJsonArray pts;
        const QVector<QPointF> list = !s.attentionPoints.isEmpty()
            ? s.attentionPoints
            : QVector<QPointF>{s.attentionNorm};
        for (const QPointF &pt : list) {
            pts.append(QJsonArray{pt.x(), pt.y()});
        }
        o.insert(QStringLiteral("attention"), pts);
    }
    if (s.contentHFlip || s.contentVFlip || s.contentQuarterTurns != 0) {
        QJsonObject bake;
        if (s.contentHFlip) {
            bake.insert(QStringLiteral("hFlip"), true);
        }
        if (s.contentVFlip) {
            bake.insert(QStringLiteral("vFlip"), true);
        }
        if (s.contentQuarterTurns != 0) {
            bake.insert(QStringLiteral("quarterTurns"), s.contentQuarterTurns);
        }
        o.insert(QStringLiteral("bake"), bake);
    }
    if (!s.colorAdjust.isIdentity()) {
        QJsonObject color;
        color.insert(QStringLiteral("brightness"), s.colorAdjust.brightness);
        color.insert(QStringLiteral("contrast"), s.colorAdjust.contrast);
        color.insert(QStringLiteral("saturation"), s.colorAdjust.saturation);
        color.insert(QStringLiteral("hue"), s.colorAdjust.hue);
        color.insert(QStringLiteral("gamma"), s.colorAdjust.gamma);
        o.insert(QStringLiteral("color"), color);
    }
    if (includePose) {
        QJsonObject placement;
        placement.insert(QStringLiteral("x"), s.pos.x());
        placement.insert(QStringLiteral("y"), s.pos.y());
        placement.insert(QStringLiteral("scaleX"), s.scale);
        placement.insert(QStringLiteral("scaleY"), s.scaleY > 0.0 ? s.scaleY : s.scale);
        if (qAbs(s.shear) > 1e-9) {
            placement.insert(QStringLiteral("shear"), s.shear);
        }
        if (!qFuzzyIsNull(s.rotation)) {
            placement.insert(QStringLiteral("rotation"), s.rotation);
        }
        if (!qFuzzyCompare(s.opacity, 1.0)) {
            placement.insert(QStringLiteral("opacity"), s.opacity);
        }
        if (!qFuzzyIsNull(s.z)) {
            placement.insert(QStringLiteral("z"), s.z);
        }
        if (s.hFlip) {
            placement.insert(QStringLiteral("hFlip"), true);
        }
        if (s.vFlip) {
            placement.insert(QStringLiteral("vFlip"), true);
        }
        o.insert(QStringLiteral("placement"), placement);
    }
    return o;
}

WorkspaceItemState appearanceFromJson(const QJsonObject &o)
{
    WorkspaceItemState s;
    if (o.contains(QStringLiteral("crop"))) {
        const QJsonObject crop = o.value(QStringLiteral("crop")).toObject();
        s.cropRect = QRect(crop.value(QStringLiteral("x")).toInt(),
                           crop.value(QStringLiteral("y")).toInt(),
                           crop.value(QStringLiteral("w")).toInt(),
                           crop.value(QStringLiteral("h")).toInt());
        s.hasCrop = !s.cropRect.isEmpty();
        if (crop.contains(QStringLiteral("sourceW")) && crop.contains(QStringLiteral("sourceH"))) {
            s.cropSourceSize = QSize(crop.value(QStringLiteral("sourceW")).toInt(),
                                     crop.value(QStringLiteral("sourceH")).toInt());
        }
        s.cropRotation = crop.value(QStringLiteral("rotation")).toDouble(0.0);
    }
    if (o.contains(QStringLiteral("attention"))) {
        const QJsonArray a = o.value(QStringLiteral("attention")).toArray();
        s.attentionPoints.clear();
        for (const QJsonValue &v : a) {
            const QJsonArray p = v.toArray();
            if (p.size() >= 2) {
                s.attentionPoints.append(AttentionGeometry::clampNorm(
                    QPointF(p.at(0).toDouble(), p.at(1).toDouble())));
            }
        }
        s.syncAttentionPrimary();
    }
    if (o.contains(QStringLiteral("bake"))) {
        const QJsonObject bake = o.value(QStringLiteral("bake")).toObject();
        s.contentHFlip = bake.value(QStringLiteral("hFlip")).toBool(false);
        s.contentVFlip = bake.value(QStringLiteral("vFlip")).toBool(false);
        s.contentQuarterTurns = bake.value(QStringLiteral("quarterTurns")).toInt(0);
    }
    if (o.contains(QStringLiteral("color"))) {
        const QJsonObject color = o.value(QStringLiteral("color")).toObject();
        s.colorAdjust.brightness = color.value(QStringLiteral("brightness")).toInt(0);
        s.colorAdjust.contrast = color.value(QStringLiteral("contrast")).toInt(100);
        s.colorAdjust.saturation = color.value(QStringLiteral("saturation")).toInt(100);
        s.colorAdjust.hue = color.value(QStringLiteral("hue")).toInt(0);
        s.colorAdjust.gamma = color.value(QStringLiteral("gamma")).toDouble(1.0);
    }
    if (o.contains(QStringLiteral("placement"))) {
        const QJsonObject pl = o.value(QStringLiteral("placement")).toObject();
        s.pos = QPointF(pl.value(QStringLiteral("x")).toDouble(),
                        pl.value(QStringLiteral("y")).toDouble());
        s.scale = pl.value(QStringLiteral("scaleX")).toDouble(1.0);
        s.scaleY = pl.value(QStringLiteral("scaleY")).toDouble(s.scale);
        s.shear = pl.value(QStringLiteral("shear")).toDouble(0.0);
        s.rotation = pl.value(QStringLiteral("rotation")).toDouble(0.0);
        s.opacity = pl.value(QStringLiteral("opacity")).toDouble(1.0);
        s.z = pl.value(QStringLiteral("z")).toDouble(0.0);
        s.hFlip = pl.value(QStringLiteral("hFlip")).toBool(false);
        s.vFlip = pl.value(QStringLiteral("vFlip")).toBool(false);
    }
    return s;
}

bool save(const QString &projectPath, const ProjectDocument &doc, QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("biltoo-project"));
    // Stage 4b: nested sparse appearance; minimum supported format is 2.
    root.insert(QStringLiteral("version"), doc.version >= 2 ? doc.version : 2);
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

    if (doc.hasWorkspaceBackground && !doc.workspaceBackground.isAppDefault()) {
        QJsonObject wb;
        const WorkspaceBackground &b = doc.workspaceBackground;
        QString modeStr = QStringLiteral("solid");
        switch (b.mode) {
        case WorkspaceBackgroundMode::AppDefault:
            modeStr = QStringLiteral("default");
            break;
        case WorkspaceBackgroundMode::Checkerboard:
            modeStr = QStringLiteral("checkerboard");
            break;
        case WorkspaceBackgroundMode::ImageTile:
            modeStr = QStringLiteral("image");
            break;
        case WorkspaceBackgroundMode::Solid:
            modeStr = QStringLiteral("solid");
            break;
        case WorkspaceBackgroundMode::ContentBlur:
            // Session View only — never a durable project field.
            modeStr = QStringLiteral("default");
            break;
        }
        wb.insert(QStringLiteral("mode"), modeStr);
        if (b.color.isValid()) {
            wb.insert(QStringLiteral("color"), b.color.name(QColor::HexRgb));
        }
        if (b.colorAlt.isValid()) {
            wb.insert(QStringLiteral("colorAlt"), b.colorAlt.name(QColor::HexRgb));
        }
        if (!b.imagePath.isEmpty()) {
            wb.insert(QStringLiteral("image"), b.imagePath);
        }
        if (!b.imagePathRelative.isEmpty()) {
            wb.insert(QStringLiteral("imageRelative"), b.imagePathRelative);
        }
        if (!b.imageSha256.isEmpty()) {
            wb.insert(QStringLiteral("imageSha256"), b.imageSha256);
        }
        root.insert(QStringLiteral("workspaceBackground"), wb);
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
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("biltoo-project")) {
        if (error) {
            *error = QObject::tr("Not a biltoo project file.");
        }
        return false;
    }
    doc->version = root.value(QStringLiteral("version")).toInt(0);
    if (doc->version < 2) {
        if (error) {
            *error = QObject::tr("Unsupported project format version %1 (need ≥ 2).")
                         .arg(doc->version);
        }
        return false;
    }
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
            const QJsonObject appObj = o.value(QStringLiteral("appearance")).toObject();
            im.appearance = appearanceFromJson(appObj);
            // Content components present ⇒ hasAppearance; placement ⇒ workspace pose.
            im.hasAppearance = appObj.contains(QStringLiteral("crop"))
                || appObj.contains(QStringLiteral("attention"))
                || appObj.contains(QStringLiteral("bake"))
                || appObj.contains(QStringLiteral("color"));
            if (appObj.contains(QStringLiteral("placement"))) {
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
    doc->hasWorkspaceBackground = false;
    doc->workspaceBackground = WorkspaceBackground{};
    if (root.contains(QStringLiteral("workspaceBackground"))) {
        const QJsonObject wb = root.value(QStringLiteral("workspaceBackground")).toObject();
        WorkspaceBackground b;
        const QString modeStr = wb.value(QStringLiteral("mode")).toString(QStringLiteral("default"));
        if (modeStr == QLatin1String("solid")) {
            b.mode = WorkspaceBackgroundMode::Solid;
        } else if (modeStr == QLatin1String("checkerboard")) {
            b.mode = WorkspaceBackgroundMode::Checkerboard;
        } else if (modeStr == QLatin1String("image")) {
            b.mode = WorkspaceBackgroundMode::ImageTile;
        } else {
            b.mode = WorkspaceBackgroundMode::AppDefault;
        }
        if (wb.contains(QStringLiteral("color"))) {
            b.color = QColor(wb.value(QStringLiteral("color")).toString());
        }
        if (wb.contains(QStringLiteral("colorAlt"))) {
            b.colorAlt = QColor(wb.value(QStringLiteral("colorAlt")).toString());
        }
        b.imagePath = wb.value(QStringLiteral("image")).toString();
        b.imagePathRelative = wb.value(QStringLiteral("imageRelative")).toString();
        b.imageSha256 = wb.value(QStringLiteral("imageSha256")).toString();
        doc->workspaceBackground = b;
        doc->hasWorkspaceBackground = !b.isAppDefault();
    }
    return true;
}

QString resolveWorkspaceBackgroundImage(const WorkspaceBackground &bg,
                                        const QString &projectFilePath)
{
    auto tryFile = [&](const QString &candidate) -> QString {
        if (candidate.isEmpty()) {
            return {};
        }
        const QFileInfo fi(candidate);
        if (!fi.isFile()) {
            return {};
        }
        const QString abs = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath()
                                                             : fi.canonicalFilePath();
        if (!bg.imageSha256.isEmpty()) {
            const QString actual = fileSha256(abs);
            if (actual.isEmpty()
                || actual.compare(bg.imageSha256, Qt::CaseInsensitive) != 0) {
                return {};
            }
        }
        return abs;
    };
    if (QString r = tryFile(bg.imagePath); !r.isEmpty()) {
        return r;
    }
    if (!bg.imagePathRelative.isEmpty() && !projectFilePath.isEmpty()) {
        const QDir projDir(QFileInfo(projectFilePath).absoluteDir());
        if (QString r = tryFile(projDir.absoluteFilePath(bg.imagePathRelative)); !r.isEmpty()) {
            return r;
        }
    }
    // Fall back without hash if the preferred path exists (legacy projects).
    if (!bg.imageSha256.isEmpty()) {
        auto tryNoHash = [](const QString &candidate) -> QString {
            if (candidate.isEmpty()) return {};
            const QFileInfo fi(candidate);
            if (!fi.isFile()) return {};
            return fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath()
                                                    : fi.canonicalFilePath();
        };
        if (QString r = tryNoHash(bg.imagePath); !r.isEmpty()) {
            return r;
        }
        if (!bg.imagePathRelative.isEmpty() && !projectFilePath.isEmpty()) {
            const QDir projDir(QFileInfo(projectFilePath).absoluteDir());
            if (QString r = tryNoHash(projDir.absoluteFilePath(bg.imagePathRelative)); !r.isEmpty()) {
                return r;
            }
        }
    }
    return {};
}

} // namespace ProjectFile
