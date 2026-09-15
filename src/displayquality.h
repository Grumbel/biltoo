// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYQUALITY_H
#define DISPLAYQUALITY_H

#include <QString>

/**
 * Host-side display quality helpers (logging / tier labels).
 *
 * **Install policy** lives in DisplaySurface::decide (docs/DISPLAY_SURFACE.md).
 * Do not drive Attach / soft demote from checkSurface host-vs-shown — that
 * compared pre-crop host edge to post-crop display and caused the 1s pulse.
 *
 * Remaining roles: tierOf, hostLongEdge, isStrictUpgrade (edge-only, no crop),
 * checkSurface + reportViolation for slideshow / debug asserts.
 */
namespace DisplayQuality {

/** Long-edge ceiling for LQIP / "quick preview" (ThumbHash-scale). */
constexpr int kLqipMaxEdge = 96;

/** Soft ladder durable max (matches ThumtooCache::kGalleryLadderEdge). */
constexpr int kSoftMaxEdge = 512;

enum class Tier {
    Blank = 0,
    Lqip = 1,       ///< ≤ kLqipMaxEdge
    Soft = 2,       ///< ≤ kSoftMaxEdge
    Overview = 3,   ///< ≤ 1024
    Full = 4
};

Tier tierOf(int longEdge);

/** True when incoming is a meaningful upgrade over what is shown. */
bool isStrictUpgrade(int shownLongEdge, int incomingLongEdge);

/** ImageCache long edge for path, or 0. */
int hostLongEdge(const QString &path);

/**
 * Result of comparing shown pixels to host cache and the surface target edge.
 *
 * Ok              — shown is adequate relative to host and target, or climb is
 *                   already pending.
 * InstallHostBetter — shown is still below target and host holds a stricter
 *                   sample; surface should install it (not when shown already
 *                   meets target).
 * ScheduleClimb   — shown is below target and host has nothing better; climb
 *                   must be scheduled (or already pending). Not a contract
 *                   break — do not log as a quality violation.
 * StuckWeak       — shown is still LQIP-class (or blank) while target ≥ soft,
 *                   host may or may not have better; treat as a contract break
 *                   if climbPending is false for longer than the watchdog grace.
 */
enum class Verdict {
    Ok = 0,
    InstallHostBetter,
    ScheduleClimb,
    StuckWeak
};

struct Check {
    Verdict verdict = Verdict::Ok;
    int shownEdge = 0;
    int hostEdge = 0;
    int targetEdge = 0;
    Tier shownTier = Tier::Blank;
    Tier hostTier = Tier::Blank;
};

/**
 * Evaluate one surface binding.
 * @param climbPending  true if soft/PreferCache/full work is already in flight
 *                      for this path (host or PathRaster).
 */
Check checkSurface(const QString &path, int shownLongEdge, int targetLongEdge,
                   bool climbPending);

/**
 * Report a contract break. Debug builds assert on StuckWeak / InstallHostBetter
 * when @p assertHard is true. Always rate-limited qWarning in all builds.
 */
void reportViolation(const char *surface, const QString &path, const Check &check,
                     bool assertHard = false);

/** Human label for HUD / logs. */
QString verdictLabel(Verdict v);
QString tierLabel(Tier t);

} // namespace DisplayQuality

#endif
