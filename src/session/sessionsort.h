// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONSORT_H
#define SESSIONSORT_H

#include <QHash>
#include <QSize>
#include <QStringList>
#include <QVector>

/**
 * Pure session-list ordering (REFACTOR: list policy outside MainWindow chrome).
 * Indices refer to positions in @p paths; callers reorder paths∥ids together.
 */
namespace SessionSort {

enum class Mode : int {
    Name = 0,
    MTime = 1,
    FileSize = 2,
    Width = 3,
    Height = 4,
    PixelCount = 5,
    Path = 6,
    AspectRatio = 7,
    Shuffle = 8
};

/** True when the mode needs disk or decode probes (must not run on the GUI). */
bool modeNeedsImageProbe(Mode mode);

/** Majority of paths are PDF/EPUB/DjVu page (or pdfimage) refs. */
bool looksLikePagedDocument(const QStringList &paths);

/**
 * Stable permutation of [0..paths.size()).
 * @p sizes / @p mtimes / @p fsizes are consulted only for the matching modes;
 * empty maps are fine for Name / Path / Shuffle.
 */
QVector<int> orderIndices(Mode mode,
                          const QStringList &paths,
                          const QHash<QString, QSize> &sizes = {},
                          const QHash<QString, qint64> &mtimes = {},
                          const QHash<QString, qint64> &fsizes = {});

} // namespace SessionSort

#endif // SESSIONSORT_H
