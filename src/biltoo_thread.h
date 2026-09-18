// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_THREAD_H
#define BILTOO_THREAD_H

// Pulls Qt headers. In translation units that also use libvips/GLib, include
// <vips/vips.h> *before* this header (GLib has a field named "signals").

#include <QElapsedTimer>
#include <QThread>
#include <cstdio>

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

/**
 * Scoped wall-time budget for code that is allowed on the GUI thread.
 * Exceeding the budget asserts in debug and always logs — hitch = bug.
 * Default 2ms; use GUI_BUDGET_MS for a named limit.
 */
class GuiBudgetScope
{
public:
    explicit GuiBudgetScope(const char *label, qint64 budgetMs = 2)
        : m_label(label)
        , m_budgetMs(budgetMs > 0 ? budgetMs : 2)
    {
        m_timer.start();
    }
    ~GuiBudgetScope()
    {
        if (!QThread::isMainThread()) {
            return;
        }
        const qint64 ms = m_timer.elapsed();
        if (ms <= m_budgetMs) {
            return;
        }
        std::fprintf(stderr,
                     "biltoo/GUI_BUDGET EXCEEDED: %s took %lld ms (budget %lld ms)\n",
                     m_label ? m_label : "?",
                     static_cast<long long>(ms),
                     static_cast<long long>(m_budgetMs));
        std::fflush(stderr);
        Q_ASSERT_X(ms <= m_budgetMs, m_label ? m_label : "GuiBudget",
                   "GUI thread work exceeded budget — move off GUI or shrink");
    }
    GuiBudgetScope(const GuiBudgetScope &) = delete;
    GuiBudgetScope &operator=(const GuiBudgetScope &) = delete;

private:
    const char *m_label = nullptr;
    qint64 m_budgetMs = 2;
    QElapsedTimer m_timer;
};

#define GUI_BUDGET(label) \
    GuiBudgetScope qt_gui_budget_##__LINE__(label)
#define GUI_BUDGET_MS(label, ms) \
    GuiBudgetScope qt_gui_budget_##__LINE__(label, ms)

#endif // BILTOO_THREAD_H
