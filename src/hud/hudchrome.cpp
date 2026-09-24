// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hud/hudchrome.h"

#include <QObject>
#include <QTimer>
#include <QWidget>
#include <functional>

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

void HudChrome::showFlash(const QString &action, const QString &detail, QWidget *viewport)
{
    m_flash.show(action, detail);
    if (m_flashTimer) {
        m_flashTimer->start(HudFlash::kActionFlashMs);
    }
    if (viewport) {
        viewport->update();
    }
}
