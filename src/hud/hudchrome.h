// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDCHROME_H
#define HUDCHROME_H

#include "hud/hudappearance.h"
#include "hud/hudflash.h"
#include "shell/centreprogress.h"
#include "util/perfstats.h"

#include <functional>

class QObject;
class QTimer;

/**
 * Shell HUD chrome: appearance prefs, action flash, centre progress,
 * perf overlay stats, and status-refresh timer. Timers parented to the
 * ImageView shell.
 */
class HudChrome
{
public:
    HudAppearance &appearance() { return m_appearance; }
    const HudAppearance &appearance() const { return m_appearance; }

    HudFlash &flash() { return m_flash; }
    const HudFlash &flash() const { return m_flash; }

    CentreProgress &centreProgress() { return m_centreProgress; }
    const CentreProgress &centreProgress() const { return m_centreProgress; }

    PerfStats &perf() { return m_perf; }
    const PerfStats &perf() const { return m_perf; }

    QTimer *flashTimer() const { return m_flashTimer; }
    QTimer *statusRefreshTimer() const { return m_statusRefreshTimer; }

    /**
     * Create single-shot flash timer parented to @p parentShell.
     * @p onTimeout runs after the flash duration (clear + viewport update).
     */
    void ensureFlashTimer(QObject *parentShell, const std::function<void()> &onTimeout);

    void stopFlashTimer();

    /**
     * Show action flash and arm the timer.
     * @p afterShow runs immediately (typically viewport()->update()).
     */
    void showFlash(const QString &action, const QString &detail,
                   const std::function<void()> &afterShow = {});

    /**
     * Coalesce rapid statusChanged / soft-climb updates.
     * Ensures timer parented to @p parentShell; @p onTimeout emits status + paint.
     */
    void scheduleStatusRefresh(QObject *parentShell, const std::function<void()> &onTimeout);

    void stopStatusRefreshTimer();

    /**
     * Run the base QGraphicsView paint path. When perf overlay is enabled,
     * records duration into PerfStats (FPS / lastPaintUs). ImageView::paintEvent
     * is a thin shell that calls this.
     */
    void runTimedPaint(const std::function<void()> &paint);

private:
    HudAppearance m_appearance;
    HudFlash m_flash;
    CentreProgress m_centreProgress;
    PerfStats m_perf;
    QTimer *m_flashTimer = nullptr;
    QTimer *m_statusRefreshTimer = nullptr;
};

#endif // HUDCHROME_H
