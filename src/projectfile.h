// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROJECTFILE_H
#define PROJECTFILE_H

#include "imageview_types.h"

#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QVector>

/**
 * On-disk project (.qimgview): JSON session + non-destructive appearance and
 * optional Workspace free-form poses. External images are referenced by path
 * and SHA-256 content address so relocated files can be recovered.
 *
 * Workspace is an ad-hoc arrangement tool, not a print document; page guide
 * is optional metadata only.
 */
struct ProjectAsset {
    QString sha256;       /**< Lowercase hex SHA-256 of file bytes. */
    QString path;         /**< Absolute path at save time. */
    QString pathRelative; /**< Path relative to the project file, if under it. */
};

struct ProjectImage {
    SessionImageId id = kInvalidSessionImageId;
    QString assetSha256;
    WorkspaceItemState appearance;
    bool hasAppearance = false;
    bool hasWorkspacePose = false;
};

struct ProjectDocument {
    int version = 1;
    QString mode;
    QVector<ProjectAsset> assets;
    QVector<ProjectImage> images;
    bool pageGuideVisible = false;
    QSizeF pageGuideSizeMm;
    /** When hasWorkspaceBackground, workspaceBackground is project-owned. */
    bool hasWorkspaceBackground = false;
    WorkspaceBackground workspaceBackground;
};

namespace ProjectFile {

QString fileSha256(const QString &path);

QString resolveAssetPath(const ProjectAsset &asset, const QString &projectFilePath,
                         QString *error = nullptr);

bool save(const QString &projectPath, const ProjectDocument &doc, QString *error = nullptr);
bool load(const QString &projectPath, ProjectDocument *doc, QString *error = nullptr);

WorkspaceItemState appearanceFromJson(const QJsonObject &o);
QJsonObject appearanceToJson(const WorkspaceItemState &s, bool includePose);

} // namespace ProjectFile

#endif // PROJECTFILE_H
