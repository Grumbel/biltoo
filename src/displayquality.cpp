// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displayquality.h"
#include "imagecache.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>

#include <cstdio>

namespace DisplayQuality {
namespace {

QMutex &warnMu()
{
    static QMutex m;
    return m;
}

/** path+surface → last warn ms (rate limit). */
QHash<QString, qint64> &lastWarnMs()
{
    static QHash<QString, qint64> h;
    return h;
}

constexpr qint64 kWarnIntervalMs = 4000;

bool shouldWarn(const QString &key)
{
    QMutexLocker lock(&warnMu());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 prev = lastWarnMs().value(key, 0);
    if (now - prev < kWarnIntervalMs) {
        return false;
    }
    lastWarnMs().insert(key, now);
    return true;
}

} // namespace

Tier tierOf(int longEdge)
{
    if (longEdge <= 0) {
        return Tier::Blank;
    }
    if (longEdge <= kLqipMaxEdge) {
        return Tier::Lqip;
    }
    if (longEdge <= kSoftMaxEdge) {
        return Tier::Soft;
    }
    if (longEdge <= 1024) {
        return Tier::Overview;
    }
    return Tier::Full;
}

bool isStrictUpgrade(int shownLongEdge, int incomingLongEdge)
{
    if (incomingLongEdge <= 0) {
        return false;
    }
    // Blank always accepts a real sample.
    if (shownLongEdge <= 0) {
        return true;
    }
    // Require a true increase so equal ladder re-delivers are no-ops.
    return incomingLongEdge > shownLongEdge;
}

int hostLongEdge(const QString &path)
{
    if (path.isEmpty()) {
        return 0;
    }
    return ImageCache::longEdge(ImageCache::get(path));
}

Check checkSurface(const QString &path, int shownLongEdge, int targetLongEdge,
                   bool climbPending)
{
    Check c;
    c.shownEdge = shownLongEdge;
    c.targetEdge = targetLongEdge;
    c.hostEdge = hostLongEdge(path);
    c.shownTier = tierOf(shownLongEdge);
    c.hostTier = tierOf(c.hostEdge);

    const int target = targetLongEdge > 0 ? targetLongEdge : kSoftMaxEdge;

    // Surface already meets its display target — done. Do not demand installing
    // a larger host sample (host may hold soft/full while the tile only needs
    // 128). Checking host-first produced install-host-better spam:
    // shown=256 host=512 target=128.
    if (shownLongEdge > 0 && shownLongEdge >= (target * 9) / 10) {
        c.verdict = Verdict::Ok;
        return c;
    }

    // Still short of target and host has a stricter sample — install it.
    if (isStrictUpgrade(shownLongEdge, c.hostEdge)) {
        c.verdict = Verdict::InstallHostBetter;
        return c;
    }

    if (climbPending) {
        c.verdict = Verdict::Ok;
        return c;
    }

    // Still in LQIP/blank while the surface asked for soft-or-better.
    if (target > kLqipMaxEdge
        && (c.shownTier == Tier::Blank || c.shownTier == Tier::Lqip)) {
        c.verdict = Verdict::StuckWeak;
        return c;
    }

    // Below target, host has nothing better, no climb — need schedule.
    if (shownLongEdge < (target * 9) / 10) {
        c.verdict = Verdict::ScheduleClimb;
        return c;
    }

    c.verdict = Verdict::Ok;
    return c;
}

QString verdictLabel(Verdict v)
{
    switch (v) {
    case Verdict::Ok:
        return QStringLiteral("ok");
    case Verdict::InstallHostBetter:
        return QStringLiteral("install-host-better");
    case Verdict::ScheduleClimb:
        return QStringLiteral("schedule-climb");
    case Verdict::StuckWeak:
        return QStringLiteral("stuck-weak");
    }
    return QStringLiteral("?");
}

QString tierLabel(Tier t)
{
    switch (t) {
    case Tier::Blank:
        return QStringLiteral("blank");
    case Tier::Lqip:
        return QStringLiteral("lqip");
    case Tier::Soft:
        return QStringLiteral("soft");
    case Tier::Overview:
        return QStringLiteral("overview");
    case Tier::Full:
        return QStringLiteral("full");
    }
    return QStringLiteral("?");
}

void reportViolation(const char *surface, const QString &path, const Check &check,
                     bool assertHard)
{
    if (check.verdict == Verdict::Ok) {
        return;
    }
    // Soft climb in progress (host still LQIP/blank): recovery runs at the call
    // site; logging every path every few seconds floods the console and hid
    // real InstallHostBetter / host-soft StuckWeak cases.
    if (check.verdict == Verdict::StuckWeak
        && (check.hostTier == Tier::Blank || check.hostTier == Tier::Lqip)) {
        return;
    }
    const QString key =
        QString::fromLatin1(surface ? surface : "?") + QLatin1Char('\n') + path;
    if (!shouldWarn(key)) {
        return;
    }
    const QString file = QFileInfo(path).fileName();
    const QString msg = QStringLiteral(
        "biltoo/quality: %1 path=%2 shown=%3(%4) host=%5(%6) target=%7 verdict=%8")
                            .arg(QString::fromLatin1(surface ? surface : "?"))
                            .arg(file)
                            .arg(check.shownEdge)
                            .arg(tierLabel(check.shownTier))
                            .arg(check.hostEdge)
                            .arg(tierLabel(check.hostTier))
                            .arg(check.targetEdge)
                            .arg(verdictLabel(check.verdict));
    // qWarning already goes to stderr in typical Qt setups — do not fprintf twice.
    qWarning("%s", qPrintable(msg));

#ifndef NDEBUG
    if (assertHard) {
        // InstallHostBetter: host already has a better sample than paint — real bug.
        // StuckWeak with host still blank/LQIP: soft climb is incomplete (queue
        // backlog, cold decode). Warn only — hard-assert aborted large galleries
        // after 2.5s while SoftOnly was still working other paths.
        if (check.verdict == Verdict::InstallHostBetter) {
            Q_ASSERT_X(false, "DisplayQuality", qPrintable(msg));
        } else if (check.verdict == Verdict::StuckWeak
                   && check.hostTier != Tier::Blank
                   && check.hostTier != Tier::Lqip) {
            // Host has soft+ but surface still weak without climb — contract break.
            Q_ASSERT_X(false, "DisplayQuality", qPrintable(msg));
        }
    }
#else
    Q_UNUSED(assertHard);
#endif
}

} // namespace DisplayQuality
