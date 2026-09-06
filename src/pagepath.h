// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PAGEPATH_H
#define PAGEPATH_H

#include <QString>
#include <QStringList>

/**
 * PDF page references for session paths (one image per page).
 *
 * Syntax (parallel to ArchivePath):
 *   /home/user/doc.pdf//page:12
 *   file:///home/user/doc.pdf//page:12
 *
 * Page numbers are 1-based. Only one level is supported.
 */
namespace PagePath {

inline constexpr const char kMarker[] = "//page:";

struct Ref {
    QString pdfPath; /**< Local filesystem path of the PDF file. */
    int page = 0;    /**< 1-based page index. */
    bool valid = false;
};

bool isPageRef(const QString &path);
Ref parse(const QString &path);
QString makeRef(const QString &pdfPath, int page_1based);
QString pdfFilePath(const QString &path);
int pageNumber(const QString &path);
/** Display name for any session path (page, archive, or plain file). */
QString displayName(const QString &path);
QString canonicalSessionPath(const QString &path);
bool isPdfFile(const QString &path);
QStringList pdfSuffixes();

} // namespace PagePath

#endif // PAGEPATH_H
