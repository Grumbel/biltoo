// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONEXPORT_H
#define SESSIONEXPORT_H

#include "imageview_types.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Bake session content appearance into new files (never writes sources).
 * See docs/SESSION_EXPORT_AND_ORDER.md.
 */
namespace SessionExport {

enum class Format {
    Jpeg = 0,
    Png = 1,
};

enum class Container {
    Directory = 0,
    Cbz = 1,
    Pdf = 2,
};

struct Item {
    QString path;
    SessionImageId id = kInvalidSessionImageId;
    WorkspaceItemState appearance;
};

struct Options {
    Container container = Container::Directory;
    Format format = Format::Jpeg;
    int jpegQuality = 90;
    /** 0 = native (after bake). Otherwise clamp long edge. */
    int maxLongEdge = 0;
    QString destPath; ///< directory, or .cbz / .pdf file
};

struct Result {
    int written = 0;
    int failed = 0;
    QStringList errors;
    QString destPath;
};

/** Decode + bake one item. Worker thread only (ImageLoader). */
QImage bakeItem(const Item &item, int maxLongEdge);

/**
 * Export all items. Creates parent dirs as needed.
 * For Directory: destPath is the folder.
 * For Cbz/Pdf: destPath is the file path.
 * Must not run on the GUI thread when decoding.
 */
Result exportItems(const QVector<Item> &items, const Options &opt);

/** Safe leaf stem from path (no directories). */
QString fileStem(const QString &path);

} // namespace SessionExport

#endif
