// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDCHROME_H
#define HUDCHROME_H

#include "hud/hudappearance.h"
#include "hud/hudflash.h"

#include <functional>

class QObject;
class QTimer;
class QWidget;

/**
 * Shell HUD chrome: pinned appearance prefs + transient action flash.
 * Owns bags and the flash QTimer (parented to the ImageView shell).
 */
class HudChrome
{
public:
    HudAppearance &appearance() { return m_appearance; }
    const HudAppearance &appearance() const { return m_appearance; }

    HudFlash &flash() { return m_flash; }
    const HudFlash &flash() const { return m_flash; }

    QTimer *flashTimer() const { return m_flashTimer; }

    /**
     * Create single-shot flash timer parented to @p parentShell.
     * @p onTimeout runs after the flash duration (clear + viewport update).
     */
    void ensureFlashTimer(QObject *parentShell, const std::function<void()> &onTimeout);

    void stopFlashTimer();

    /** Show action flash, arm timer, optional viewport update. */
    void showFlash(const QString &action, const QString &detail, QWidget *viewport);

private:
    HudAppearance m_appearance;
    HudFlash m_flash;
    QTimer *m_flashTimer = nullptr;
};

#endif // HUDCHROME_H
