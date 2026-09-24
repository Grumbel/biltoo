// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hud/hudchrome.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

void HudChrome::ensureFlashTimer(QObject *parentShell, const std::function<void()> &onTimeout)
{
    if (m_flashTimer || !parentShell) {
        return;
    }
    m_flashTimer = new QTimer(parentShell);
    m_flashTimer->setSingleShot(true);
    QObject::connect(m_flashTimer, &QTimer::timeout, parentShell, [onTimeout]() {
        if (onTimeout) {
            onTimeout();
        }
    });
}

void HudChrome::stopFlashTimer()
{
    if (m_flashTimer) {
        m_flashTimer->stop();
    }
}

void HudChrome::showFlash(const QString &action, const QString &detail,
                          const std::function<void()> &afterShow)
{
    m_flash.show(action, detail);
    if (m_flashTimer) {
        m_flashTimer->start(HudFlash::kActionFlashMs);
    }
    if (afterShow) {
        afterShow();
    }
}

void HudChrome::scheduleStatusRefresh(QObject *parentShell, const std::function<void()> &onTimeout)
{
    if (!m_statusRefreshTimer) {
        if (!parentShell) {
            return;
        }
        m_statusRefreshTimer = new QTimer(parentShell);
        m_statusRefreshTimer->setSingleShot(true);
        m_statusRefreshTimer->setInterval(HudAppearance::kStatusRefreshMs);
        QObject::connect(m_statusRefreshTimer, &QTimer::timeout, parentShell, [onTimeout]() {
            if (onTimeout) {
                onTimeout();
            }
        });
    }
    m_statusRefreshTimer->start();
}

void HudChrome::stopStatusRefreshTimer()
{
    if (m_statusRefreshTimer) {
        m_statusRefreshTimer->stop();
    }
}

void HudChrome::runTimedPaint(const std::function<void()> &paint)
{
    if (!paint) {
        return;
    }
    if (!m_perf.isEnabled()) {
        paint();
        return;
    }
    QElapsedTimer t;
    t.start();
    paint();
    m_perf.notePaintUs(t.nsecsElapsed() / 1000);
}
