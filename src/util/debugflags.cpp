// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util/debugflags.h"

#include <QByteArray>
#include <QCoreApplication>

#include <cstdlib>

namespace {

[[nodiscard]] bool envTruthy(const char *name)
{
    const char *e = std::getenv(name);
    return e && e[0] && e[0] != '0';
}

} // namespace

DebugFlags &DebugFlags::instance()
{
    static DebugFlags *s = []() {
        auto *d = new DebugFlags(QCoreApplication::instance());
        d->initFromEnvironment();
        return d;
    }();
    return *s;
}

DebugFlags::DebugFlags(QObject *parent)
    : QObject(parent)
{
}

void DebugFlags::initFromEnvironment()
{
    // Always re-read env so a late init still picks up process environment.
    m_flags[Overlay] = envTruthy("BILTOO_DEBUG_OVERLAY")
        || envTruthy("THUMTOO_DEBUG_OVERLAY");
    m_flags[TileDebug] = envTruthy("BILTOO_TILE_DEBUG");
    m_flags[Crop] = envTruthy("BILTOO_DEBUG_CROP");
    m_flags[Drop] = envTruthy("BILTOO_DEBUG_DROP");
    m_flags[Find] = envTruthy("BILTOO_DEBUG_FIND");
    m_flags[Appearance] = envTruthy("BILTOO_DEBUG_APPEARANCE");
    m_flags[Filmstrip] = envTruthy("BILTOO_DEBUG_FILMSTRIP");
    m_flags[Slideshow] = envTruthy("BILTOO_DEBUG_SLIDESHOW");
    m_flags[Mode] = envTruthy("BILTOO_MODE_DEBUG");
    m_flags[Load] = envTruthy("BILTOO_LOAD_DEBUG");
    m_flags[Perf] = envTruthy("BILTOO_PERF");
    m_flags[Ttfp] = envTruthy("BILTOO_TTFP");
    m_flags[GuiBudgetLog] = envTruthy("BILTOO_GUI_BUDGET_LOG");
    m_flags[ThumtooDebug] = envTruthy("THUMTOO_DEBUG")
        || envTruthy("BILTOO_THUMTOO_DEBUG");
    m_inited = true;
}

bool DebugFlags::isEnabled(Flag f) const
{
    if (f < 0 || f >= FlagCount) {
        return false;
    }
    return m_flags[f];
}

void DebugFlags::setEnabled(Flag f, bool on)
{
    if (f < 0 || f >= FlagCount) {
        return;
    }
    if (!m_inited) {
        initFromEnvironment();
    }
    if (m_flags[f] == on) {
        return;
    }
    m_flags[f] = on;
    applyEnvMirror(f, on);
    emit flagChanged(f, on);
}

void DebugFlags::applyEnvMirror(Flag f, bool on)
{
    // Mirror into the process environment so code paths (and thumtoo) that
    // still call getenv() see the same state. Does not affect parent shell.
    const QByteArray val = on ? QByteArrayLiteral("1") : QByteArrayLiteral("0");
    switch (f) {
    case Overlay:
        qputenv("BILTOO_DEBUG_OVERLAY", val);
        qputenv("THUMTOO_DEBUG_OVERLAY", val);
        break;
    case TileDebug:
        qputenv("BILTOO_TILE_DEBUG", val);
        break;
    case Crop:
        qputenv("BILTOO_DEBUG_CROP", val);
        break;
    case Drop:
        qputenv("BILTOO_DEBUG_DROP", val);
        break;
    case Find:
        qputenv("BILTOO_DEBUG_FIND", val);
        break;
    case Appearance:
        qputenv("BILTOO_DEBUG_APPEARANCE", val);
        break;
    case Filmstrip:
        qputenv("BILTOO_DEBUG_FILMSTRIP", val);
        break;
    case Slideshow:
        qputenv("BILTOO_DEBUG_SLIDESHOW", val);
        break;
    case Mode:
        qputenv("BILTOO_MODE_DEBUG", val);
        break;
    case Load:
        qputenv("BILTOO_LOAD_DEBUG", val);
        break;
    case Perf:
        qputenv("BILTOO_PERF", val);
        break;
    case Ttfp:
        qputenv("BILTOO_TTFP", val);
        break;
    case GuiBudgetLog:
        qputenv("BILTOO_GUI_BUDGET_LOG", val);
        break;
    case ThumtooDebug:
        qputenv("THUMTOO_DEBUG", val);
        qputenv("BILTOO_THUMTOO_DEBUG", val);
        break;
    case FlagCount:
        break;
    }
}

QString DebugFlags::envName(Flag f)
{
    switch (f) {
    case Overlay:
        return QStringLiteral("BILTOO_DEBUG_OVERLAY");
    case TileDebug:
        return QStringLiteral("BILTOO_TILE_DEBUG");
    case Crop:
        return QStringLiteral("BILTOO_DEBUG_CROP");
    case Drop:
        return QStringLiteral("BILTOO_DEBUG_DROP");
    case Find:
        return QStringLiteral("BILTOO_DEBUG_FIND");
    case Appearance:
        return QStringLiteral("BILTOO_DEBUG_APPEARANCE");
    case Filmstrip:
        return QStringLiteral("BILTOO_DEBUG_FILMSTRIP");
    case Slideshow:
        return QStringLiteral("BILTOO_DEBUG_SLIDESHOW");
    case Mode:
        return QStringLiteral("BILTOO_MODE_DEBUG");
    case Load:
        return QStringLiteral("BILTOO_LOAD_DEBUG");
    case Perf:
        return QStringLiteral("BILTOO_PERF");
    case Ttfp:
        return QStringLiteral("BILTOO_TTFP");
    case GuiBudgetLog:
        return QStringLiteral("BILTOO_GUI_BUDGET_LOG");
    case ThumtooDebug:
        return QStringLiteral("THUMTOO_DEBUG");
    case FlagCount:
        break;
    }
    return {};
}

QString DebugFlags::label(Flag f)
{
    switch (f) {
    case Overlay:
        return QObject::tr("Pixel &overlay");
    case TileDebug:
        return QObject::tr("&Tile plan overlay / logs");
    case Crop:
        return QObject::tr("&Crop geometry");
    case Drop:
        return QObject::tr("&Drop / DnD");
    case Find:
        return QObject::tr("&Find");
    case Appearance:
        return QObject::tr("&Appearance");
    case Filmstrip:
        return QObject::tr("&Filmstrip");
    case Slideshow:
        return QObject::tr("&Slideshow");
    case Mode:
        return QObject::tr("&Mode switches");
    case Load:
        return QObject::tr("&Load / PreferCache");
    case Perf:
        return QObject::tr("&Perf stats");
    case Ttfp:
        return QObject::tr("T&TFP trace");
    case GuiBudgetLog:
        return QObject::tr("&GUI budget log");
    case ThumtooDebug:
        return QObject::tr("T&humtoo client");
    case FlagCount:
        break;
    }
    return {};
}

QString DebugFlags::statusTip(Flag f)
{
    const QString env = envName(f);
    switch (f) {
    case Overlay:
        return QObject::tr("Stamp origin watermark on decoded samples (%1)").arg(env);
    case TileDebug:
        return QObject::tr("Tile coverage HUD and tile scheduler logs (%1)").arg(env);
    case Crop:
        return QObject::tr("Crop-mode geometry diagnostics (%1)").arg(env);
    case Drop:
        return QObject::tr("Drag-and-drop path logging (%1)").arg(env);
    case Find:
        return QObject::tr("Find / search diagnostics (%1)").arg(env);
    case Appearance:
        return QObject::tr("Session appearance materialize logging (%1)").arg(env);
    case Filmstrip:
        return QObject::tr("Filmstrip schedule diagnostics (%1)").arg(env);
    case Slideshow:
        return QObject::tr("Slideshow transition traces (%1)").arg(env);
    case Mode:
        return QObject::tr("Image/Gallery/Workspace switch diagnostics (%1)").arg(env);
    case Load:
        return QObject::tr("Load and PreferCache path logging (%1)").arg(env);
    case Perf:
        return QObject::tr("Performance counters to stderr (%1)").arg(env);
    case Ttfp:
        return QObject::tr("Time-to-first-paint trace (%1)").arg(env);
    case GuiBudgetLog:
        return QObject::tr("GUI-thread budget overruns (%1)").arg(env);
    case ThumtooDebug:
        return QObject::tr("Thumtoo client debug (also sets THUMTOO_DEBUG) (%1)").arg(env);
    case FlagCount:
        break;
    }
    return {};
}
