// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RASTERCLIMBSM_H
#define RASTERCLIMBSM_H

/**
 * Pure path→raster climb state machine (no I/O).
 *
 * PathRasterService schedules; this class decides when TileSynth/pyramid,
 * PreferCache overview, or Full are legal so sticky flags and
 * PreferCache-before-Full deadlocks cannot recur.
 *
 * Product: Soft ladder encode is dead ([docs/KILL_SOFT.md]). Plan field
 * scheduleBand still means "first ≤softMax band via TileSynth or pyramid"
 * (PathRasterService wires it that way).
 *
 * Invariants:
 * 1. have comes only from setHaveFromHost / noteDelivery (host is authority).
 * 2. bandQueued/displayQueued/fullQueued are cleared when external pending is false.
 * 2b. bandAttempted: first band already returned pixels; do not re-issue while
 *     host still holds a sample (avoids loops when returned edge < softMax).
 * 3. When first band is covered and effectiveNeed > overviewCap, plan may emit
 *    Full in the same tick as PreferCache — PreferCache cannot monopolize the path.
 * 4. fullDone is cleared while have is a shortfall vs effectiveNeed.
 * 5. preferGaveUp is not terminal until Full was attempted when need > overview.
 *
 * Want must already be native/ladder-capped by the caller (capWant).
 */
namespace RasterClimb {

constexpr int kCoverNumer = 9;
constexpr int kCoverDenom = 10;

inline bool covers(int have, int need)
{
    if (need <= 0) {
        return have > 0;
    }
    if (have <= 0) {
        return false;
    }
    return have * kCoverDenom >= need * kCoverNumer;
}

enum class Policy {
    TileDisplay = 0,
    EscalateToFull = 1,
};

struct PendingFlags {
    bool band = false;
    bool display = false;
    bool full = false;
};

struct Plan {
    bool scheduleBand = false;
    bool scheduleDisplay = false;
    bool scheduleTiles = false;
    bool scheduleFull = false;
    bool forgetBandSettled = false;
    bool forgetDisplaySettled = false;
    bool forgetFullSettled = false;
    int bandEdge = 0;
    int displayEdge = 0;
    int fullEdge = 0;
};

struct State {
    int want = 0;
    int have = 0;
    int native = 0;
    int lastDisplayWant = 0;
    int lastDisplayGot = 0;
    int postTilePreferAttempts = 0;
    bool bandQueued = false;
    /** First ≤bandMax delivery already returned pixels (may be < bandMax). */
    bool bandAttempted = false;
    bool displayQueued = false;
    bool fullQueued = false;
    bool preferGaveUp = false;
    bool tilesQueued = false;
    bool fullDone = false;
    Policy policy = Policy::TileDisplay;
};

class Machine {
public:
    State &state() { return m_; }
    const State &state() const { return m_; }

    void setWant(int want, int native, Policy policy, int softMax, int overviewCap);
    void setHaveFromHost(int hostHave, int softMax);
    void reconcilePending(const PendingFlags &pending);
    void noteDelivery(int requestEdge, int got, int softMax);
    Plan plan(int softMax, int overviewCap, int displayMaxEdge) const;
    void markScheduled(const Plan &plan);

    bool isGaveUp(int overviewCap) const;
    bool isClimbPending(const PendingFlags &external, int overviewCap) const;
    int effectiveNeed() const;

private:
    State m_;
};

} // namespace RasterClimb

#endif
