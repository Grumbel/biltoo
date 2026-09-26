// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_DEBUGFLAGS_H
#define BILTOO_DEBUGFLAGS_H

#include <QObject>
#include <QString>

/**
 * Runtime-togglable debug switches (menu + env).
 *
 * Initial state is taken from the matching environment variables at
 * process start. Menu toggles update the in-process flags and, when
 * useful for libraries that still read the environment (thumtoo),
 * call qputenv so later getenv() sees the same value.
 *
 * Numeric / path env vars (cache MiB, job counts, …) are not managed
 * here — only on/off diagnostics.
 */
class DebugFlags : public QObject
{
    Q_OBJECT
public:
    enum Flag {
        Overlay = 0,     ///< BILTOO_DEBUG_OVERLAY / THUMTOO_DEBUG_OVERLAY
        TileDebug,       ///< BILTOO_TILE_DEBUG (tile plan HUD / logs)
        Crop,            ///< BILTOO_DEBUG_CROP
        Drop,            ///< BILTOO_DEBUG_DROP
        Find,            ///< BILTOO_DEBUG_FIND
        Appearance,      ///< BILTOO_DEBUG_APPEARANCE
        Filmstrip,       ///< BILTOO_DEBUG_FILMSTRIP
        Slideshow,       ///< BILTOO_DEBUG_SLIDESHOW
        Mode,            ///< BILTOO_MODE_DEBUG
        Load,            ///< BILTOO_LOAD_DEBUG
        Perf,            ///< BILTOO_PERF
        Ttfp,            ///< BILTOO_TTFP
        GuiBudgetLog,    ///< BILTOO_GUI_BUDGET_LOG
        ThumtooDebug,    ///< THUMTOO_DEBUG / BILTOO_THUMTOO_DEBUG
        FlagCount
    };
    Q_ENUM(Flag)

    static DebugFlags &instance();

    /** Seed all flags from the environment (idempotent; safe to call early). */
    void initFromEnvironment();

    [[nodiscard]] bool isEnabled(Flag f) const;
    void setEnabled(Flag f, bool on);

    [[nodiscard]] static QString envName(Flag f);
    [[nodiscard]] static QString label(Flag f);
    [[nodiscard]] static QString statusTip(Flag f);

signals:
    void flagChanged(DebugFlags::Flag flag, bool enabled);

private:
    explicit DebugFlags(QObject *parent = nullptr);
    void applyEnvMirror(Flag f, bool on);

    bool m_flags[FlagCount]{};
    bool m_inited = false;
};

/** Convenience — same as DebugFlags::instance().isEnabled(f). */
[[nodiscard]] inline bool debugFlag(DebugFlags::Flag f)
{
    return DebugFlags::instance().isEnabled(f);
}

#endif // BILTOO_DEBUGFLAGS_H
