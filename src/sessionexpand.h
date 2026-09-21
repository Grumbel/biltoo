// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONEXPAND_H
#define SESSIONEXPAND_H

#include <QString>
#include <QStringList>

#include <functional>

/**
 * Path expansion for session open/append (PDF/EPUB/DjVu/archive/dir → leaves).
 * Pure of MainWindow chrome; progress/cancel are callbacks only.
 */
namespace SessionExpand {

using ReportFn = std::function<void(const QString &message, int current, int total)>;
using CancelFn = std::function<bool()>; // true → abort

/** Absolute path for session identity without exists()/canonicalFilePath() stats. */
QString canonicalImagePath(const QString &path);

/**
 * Expand @p paths into session leaves (page refs, archive members, images).
 * @p recursive controls directory walk. Optional @p report / @p cancel for
 * background expand UI.
 */
QStringList expandPathList(const QStringList &paths, bool recursive,
                           const ReportFn &report = {},
                           const CancelFn &cancel = {});

/** User-visible reason when expand produced an empty list. */
QString emptyResultMessage(const QStringList &paths, bool append);

} // namespace SessionExpand

#endif // SESSIONEXPAND_H
