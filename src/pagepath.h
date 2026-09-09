// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PAGEPATH_H
#define PAGEPATH_H

#include <QString>
#include <QStringList>

/**
 * Multipage document references for session paths (one image per page).
 *
 * PDF:
 *   /home/user/doc.pdf//page:12
 *   file:///home/user/doc.pdf//page:12
 *
 * EPUB (layout profile required by thumtoo; default w/h/em when expanding):
 *   /home/user/book.epub//epub:w=600,h=900,em=12//page:3
 *
 * Page numbers are 1-based. Only one page level is supported.
 */
namespace PagePath {

inline constexpr const char kPageMarker[] = "//page:";
inline constexpr const char kEpubMarker[] = "//epub:";
/** @deprecated use kPageMarker */
inline constexpr const char kMarker[] = "//page:";

struct Ref {
    QString pdfPath; /**< Local filesystem path of the PDF or EPUB file. */
    int page = 0;    /**< 1-based page index. */
    bool valid = false;
    /** Empty for PDF; for EPUB holds "w=600,h=900,em=12" (no //epub: prefix). */
    QString epubLayoutParams;
    bool isEpub() const { return !epubLayoutParams.isEmpty(); }
};

bool isPageRef(const QString &path);
Ref parse(const QString &path);
QString makeRef(const QString &pdfPath, int page_1based);
/** EPUB page ref with explicit layout params (thumtoo //epub: payload). */
QString makeEpubRef(const QString &epubPath, int page_1based,
                    const QString &layoutParams);
QString pdfFilePath(const QString &path);
int pageNumber(const QString &path);
/** Display name for any session path (page, archive, or plain file). */
QString displayName(const QString &path);
QString canonicalSessionPath(const QString &path);
bool isPdfFile(const QString &path);
bool isEpubFile(const QString &path);
QStringList pdfSuffixes();
QStringList epubSuffixes();

} // namespace PagePath

#endif // PAGEPATH_H
