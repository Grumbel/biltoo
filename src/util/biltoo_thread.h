// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_THREAD_H
#define BILTOO_THREAD_H

// Pulls Qt headers. In translation units that also use libvips/GLib, include
// <vips/vips.h> *before* this header (GLib has a field named "signals").

#include <QElapsedTimer>
#include <QThread>
#include <cstdlib>
#include <cstdio>

/**
 * Heavy decode / scale / appearance bake / Store SQLite must not run on the GUI
 * thread. Call at the top of worker-only functions so misuse aborts in debug.
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
 * Default GUI wall budget (ms). Generous enough to avoid noise on modest
 * hardware; still catches multi-frame stalls. Override per-site with
 * GUI_BUDGET_MS only when a tighter budget is intentional.
 */
constexpr qint64 kGuiBudgetDefaultMs = 25;

/**
 * Scoped wall-time budget for code that is allowed on the GUI thread.
 * Always logs when exceeded. Aborts only if BILTOO_GUI_BUDGET_STRICT is set
 * (non-empty, not "0") so normal runs stay usable while budgets are tightened.
 *
 * Put a scope at every GUI entry that can do non-trivial work (paint, mode
 * switch, sizeReady, layout, filmstrip fill, decode window, …). Nested scopes
 * are fine; the outer one reports total wall time for that entry.
 */
class GuiBudgetScope
{
public:
    explicit GuiBudgetScope(const char *label, qint64 budgetMs = kGuiBudgetDefaultMs)
        : m_label(label)
        , m_budgetMs(budgetMs > 0 ? budgetMs : kGuiBudgetDefaultMs)
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
                     "biltoo/GUI_BUDGET EXCEEDED: %s took %lld ms (budget %lld ms)%s\n",
                     m_label ? m_label : "?",
                     static_cast<long long>(ms),
                     static_cast<long long>(m_budgetMs),
                     strictMode() ? " [STRICT abort]" : " [log only; set BILTOO_GUI_BUDGET_STRICT=1 to abort]");
        std::fflush(stderr);
        if (strictMode()) {
            Q_ASSERT_X(ms <= m_budgetMs, m_label ? m_label : "GuiBudget",
                       "GUI thread work exceeded budget — move off GUI or shrink");
        }
    }
    GuiBudgetScope(const GuiBudgetScope &) = delete;
    GuiBudgetScope &operator=(const GuiBudgetScope &) = delete;

private:
    static bool strictMode()
    {
        const char *e = std::getenv("BILTOO_GUI_BUDGET_STRICT");
        return e && e[0] && e[0] != '0';
    }

    const char *m_label = nullptr;
    qint64 m_budgetMs = kGuiBudgetDefaultMs;
    QElapsedTimer m_timer;
};

#define GUI_BUDGET(label) \
    GuiBudgetScope qt_gui_budget_##__LINE__(label)
#define GUI_BUDGET_MS(label, ms) \
    GuiBudgetScope qt_gui_budget_##__LINE__(label, ms)

#endif // BILTOO_THREAD_H
