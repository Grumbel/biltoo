// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ARCHIVEREADER_H
#define ARCHIVEREADER_H

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * In-memory archive access via libarchive (no extraction to disk).
 * Available only when built with BILTOO_HAVE_ARCHIVE.
 */
namespace ArchiveReader {

/** True when this build linked against libarchive. */
bool isAvailable();

/**
 * List member paths that look like images (by suffix).
 * Returns empty if the archive cannot be opened or libarchive is missing.
 */
QStringList listImageMembers(const QString &archivePath);

/**
 * Read one member fully into memory.
 * @return empty QByteArray on failure.
 */
QByteArray readMember(const QString &archivePath, const QString &memberPath);

/**
 * Expand an archive file into session path refs (ArchivePath::makeRef).
 * Empty if not an archive or no image members.
 */
QStringList expandArchiveToImageRefs(const QString &archivePath);

} // namespace ArchiveReader

#endif // ARCHIVEREADER_H
