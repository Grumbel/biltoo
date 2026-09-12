// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_THREAD_H
#define BILTOO_THREAD_H

#include <QThread>

/**
 * Heavy decode / scale / appearance bake must not run on the GUI thread.
 * Call at the top of worker-only functions so misuse aborts in debug builds.
 *
 * Completions back to the UI use QTimer::singleShot(0, …) or
 * Qt::QueuedConnection — that only *delivers* the result; the work itself
 * must still run off-GUI (this assert guards the work).
 */
#define ASSERT_NOT_GUI_THREAD() \
    Q_ASSERT_X(!QThread::isMainThread(), Q_FUNC_INFO, \
               "heavy work must not run on the GUI thread")

/** Scene / widget / QPixmap updates belong on the GUI thread. */
#define ASSERT_GUI_THREAD() \
    Q_ASSERT_X(QThread::isMainThread(), Q_FUNC_INFO, \
               "UI work must run on the GUI thread")

#endif // BILTOO_THREAD_H
