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
 * Default GUI wall budget (ms) for a single scope *or* a tight chain of scopes
 * in one event-loop burst (e.g. 16× sizeReady in one chunk).
 */
constexpr qint64 kGuiBudgetDefaultMs = 25;

/**
 * Gap (ms) after which sequential GuiBudgetScope instances are treated as a
 * new event-loop turn instead of one chained burst.
 */
constexpr qint64 kGuiBudgetChainGapMs = 3;

/**
 * Scoped wall-time budget for code that is allowed on the GUI thread.
 * Always logs when *this* scope exceeds the budget.
 *
 * Also tracks a **chain**: scopes that start within kGuiBudgetChainGapMs of
 * the previous scope's end share one turn clock. If the chain total exceeds
 * the budget, logs once:
 *   biltoo/GUI_BUDGET EXCEEDED: chained-turn … (via last-label)
 * so N× small handlers cannot freeze the UI without a report.
 *
 * Aborts only if BILTOO_GUI_BUDGET_STRICT is set (non-empty, not "0").
 */
class GuiBudgetScope
{
public:
    explicit GuiBudgetScope(const char *label, qint64 budgetMs = kGuiBudgetDefaultMs)
        : m_label(label)
        , m_budgetMs(budgetMs > 0 ? budgetMs : kGuiBudgetDefaultMs)
    {
        if (!QThread::isMainThread()) {
            return;
        }
        m_onGui = true;
        // Continue or start the event-burst chain.
        if (!s_chainOn || s_chain.elapsed() - s_lastEndElapsed > kGuiBudgetChainGapMs) {
            s_chain.restart();
            s_chainOn = true;
            s_chainReported = false;
            s_lastEndElapsed = 0;
        }
        m_timer.start();
    }
    ~GuiBudgetScope()
    {
        if (!m_onGui) {
            return;
        }
        const qint64 selfMs = m_timer.elapsed();
        const qint64 chainMs = s_chain.elapsed();
        s_lastEndElapsed = chainMs;

        if (selfMs > m_budgetMs) {
            logExceed(m_label, selfMs, m_budgetMs);
        }
        // One report per chain when the burst total blows the budget.
        if (!s_chainReported && chainMs > kGuiBudgetDefaultMs) {
            s_chainReported = true;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "chained-turn (last=%s)",
                          m_label ? m_label : "?");
            logExceed(buf, chainMs, kGuiBudgetDefaultMs);
        }
    }
    GuiBudgetScope(const GuiBudgetScope &) = delete;
    GuiBudgetScope &operator=(const GuiBudgetScope &) = delete;

private:
    static void logExceed(const char *label, qint64 ms, qint64 budgetMs)
    {
        std::fprintf(stderr,
                     "biltoo/GUI_BUDGET EXCEEDED: %s took %lld ms (budget %lld ms)%s\n",
                     label ? label : "?",
                     static_cast<long long>(ms),
                     static_cast<long long>(budgetMs),
                     strictMode() ? " [STRICT abort]"
                                  : " [log only; set BILTOO_GUI_BUDGET_STRICT=1 to abort]");
        std::fflush(stderr);
        if (strictMode()) {
            Q_ASSERT_X(ms <= budgetMs, label ? label : "GuiBudget",
                       "GUI thread work exceeded budget — move off GUI or shrink");
        }
    }
    static bool strictMode()
    {
        const char *e = std::getenv("BILTOO_GUI_BUDGET_STRICT");
        return e && e[0] && e[0] != '0';
    }

    const char *m_label = nullptr;
    qint64 m_budgetMs = kGuiBudgetDefaultMs;
    QElapsedTimer m_timer;
    bool m_onGui = false;

    static inline thread_local bool s_chainOn = false;
    static inline thread_local bool s_chainReported = false;
    static inline thread_local QElapsedTimer s_chain{};
    static inline thread_local qint64 s_lastEndElapsed = 0;
};

#define GUI_BUDGET(label) \
    GuiBudgetScope qt_gui_budget_##__LINE__(label)
#define GUI_BUDGET_MS(label, ms) \
    GuiBudgetScope qt_gui_budget_##__LINE__(label, ms)

#endif // BILTOO_THREAD_H
