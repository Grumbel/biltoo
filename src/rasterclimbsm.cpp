// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rasterclimbsm.h"

#include <algorithm>

namespace RasterClimb {

int Machine::effectiveNeed() const
{
    int need = m_.want;
    if (m_.native > 0) {
        need = std::min(need, m_.native);
    }
    return std::max(0, need);
}

void Machine::setWant(int want, int native, Policy policy, int /*softMax*/,
                      int /*overviewCap*/)
{
    if (want <= 0) {
        return;
    }
    if (policy == Policy::EscalateToFull) {
        m_.policy = Policy::EscalateToFull;
    }
    if (native > 0) {
        m_.native = native;
    }
    if (want > m_.lastDisplayWant) {
        m_.preferGaveUp = false;
        m_.displayQueued = false;
        m_.fullDone = false;
        m_.fullQueued = false;
        m_.postTilePreferAttempts = 0;
    }
    if (policy == Policy::EscalateToFull) {
        m_.fullDone = false;
        m_.fullQueued = false;
    }
    m_.want = std::max(m_.want, want);
}

void Machine::setHaveFromHost(int hostHave, int softMax)
{
    if (hostHave < m_.have) {
        // Host lost the sample (LRU) — allow Soft/PreferCache again.
        m_.softQueued = false;
        m_.displayQueued = false;
        m_.preferGaveUp = false;
        m_.lastDisplayGot = 0;
        m_.fullDone = false;
        m_.fullQueued = false;
        if (hostHave <= 0) {
            m_.softAttempted = false;
        }
    }
    m_.have = std::max(0, hostHave);
    // Host only holds LQIP — soft PreferCache has not truly run yet.
    constexpr int kLqipCeiling = 96;
    if (m_.have > 0 && m_.have <= kLqipCeiling) {
        m_.softAttempted = false;
    }
    if (covers(m_.have, effectiveNeed())) {
        m_.preferGaveUp = false;
        m_.displayQueued = false;
    }
    // Soft covered: PreferCache soft plateau is irrelevant for Full path.
    if (covers(m_.have, softMax) || m_.have >= softMax) {
        // keep preferGaveUp for Display band only
    }
}

void Machine::reconcilePending(const PendingFlags &pending)
{
    if (m_.softQueued && !pending.soft) {
        m_.softQueued = false;
    }
    if (m_.displayQueued && !pending.display) {
        m_.displayQueued = false;
    }
    if (m_.fullQueued && !pending.full) {
        m_.fullQueued = false;
    }
}

void Machine::noteDelivery(int requestEdge, int got, int softMax)
{
    // LQIP is a stand-in only (≤96). Soft PreferCache must keep climbing.
    constexpr int kLqipCeiling = 96;
    if (got > 0) {
        m_.have = std::max(m_.have, got);
        if (requestEdge > 0) {
            m_.lastDisplayGot = std::max(m_.lastDisplayGot, got);
        }
        // Soft-band delivery below softMax: any sample above LQIP counts as
        // soft attempted so SoftOnly does not loop on 97–127 while Prefer soft
        // at softMax never runs. LQIP (≤96) must NOT set this.
        if (requestEdge <= softMax && got > kLqipCeiling) {
            m_.softAttempted = true;
        }
    }
    m_.displayQueued = false;
    m_.softQueued = false;
    m_.fullQueued = false;

    // PreferCache plateau is for *display* band (request > softMax), not soft
    // shortfalls. Soft-band got=128/LQIP used to set preferGaveUp and then
    // plan() returned empty while !softCovered → permanent LQIP freeze.
    if (requestEdge > softMax && got > 0
        && got * kCoverDenom < requestEdge * kCoverNumer) {
        if (got > kLqipCeiling
            || (m_.want > softMax && got <= softMax && got > kLqipCeiling)) {
            m_.preferGaveUp = true;
        }
    }
    // TileSynth / mid-ladder Prefer can cover the scheduled Prefer edge while
    // still short of host want (clamped request, or tiles assemble to overview
    // while want is Full-band). Soft-covered but short-of-want → Prefer plateau
    // so plan() escalates Full instead of re-requesting Prefer forever.
    // Use kLqipCeiling so pure LQIP never counts as a Prefer plateau.
    if (got > kLqipCeiling && m_.want > softMax
        && !covers(got, m_.want)
        && (covers(got, softMax) || got >= softMax)) {
        m_.preferGaveUp = true;
    }
    if (covers(m_.have, effectiveNeed())) {
        m_.preferGaveUp = false;
    }
}

bool Machine::isGaveUp(int overviewCap) const
{
    if (!m_.preferGaveUp) {
        return false;
    }
    if (effectiveNeed() > overviewCap && !m_.fullDone) {
        return false;
    }
    if (effectiveNeed() <= overviewCap && m_.postTilePreferAttempts < 2) {
        return false;
    }
    // Still short of need after Full — not a true give-up; Full shortfall retries.
    if (!covers(m_.have, effectiveNeed())) {
        return false;
    }
    return true;
}

bool Machine::isClimbPending(const PendingFlags &external, int overviewCap) const
{
    if (m_.softQueued || m_.displayQueued || m_.fullQueued) {
        return true;
    }
    if (external.soft || external.display || external.full) {
        return true;
    }
    if (m_.preferGaveUp && !isGaveUp(overviewCap)) {
        return true;
    }
    return false;
}

Plan Machine::plan(int softMax, int overviewCap, int displayMaxEdge) const
{
    Plan p;
    p.softEdge = softMax;
    p.displayEdge = m_.want;
    const int need = effectiveNeed();
    if (need <= 0 || covers(m_.have, need)) {
        return p;
    }

    const bool softCovered = covers(m_.have, softMax) || m_.have >= softMax;

    // --- Soft ---
    // softAttempted: SoftOnly already returned pixels (possibly < softMax).
    // Re-requesting SoftOnly when have is 85–128 and softMax is 512 spun forever
    // (DEBUG_OVERLAY soft 85x128 req=256/512 loop in Gallery).
    if (!softCovered && !m_.softQueued && !m_.softAttempted) {
        p.scheduleSoft = true;
        // Do not forget settled on shortfall — that re-opened SoftOnly forever.
        p.forgetSoftSettled = false;
    }

    // Full edge: min(want, native, displayMax)
    int fullEdge = need > 0 ? need : displayMaxEdge;
    if (m_.native > 0) {
        fullEdge = std::min(fullEdge, m_.native);
    }
    fullEdge = std::min(fullEdge, displayMaxEdge);
    fullEdge = std::max(fullEdge, overviewCap + 1);
    p.fullEdge = fullEdge;

    // Full shortfall is terminal for this ensure cycle (thumtoo must deliver
    // coverage on the first real Full encode). Soft-tier RETRY looped on PDF.
    bool fullDone = m_.fullDone;

    // --- PreferCache (intermediate) ---
    // Progressive ladder: Soft → Prefer delivery → Full only after Prefer
    // plateaus short of need. Scheduling Full in the same plan as first Prefer
    // starved intermediate paints (long wait, then jump to native).
    const bool needFullBand = need > overviewCap;
    if (!m_.preferGaveUp) {
        const bool displayBusy =
            m_.displayQueued && m_.lastDisplayWant == m_.want;
        if (!displayBusy) {
            p.scheduleDisplay = true;
            p.displayEdge = m_.want;
        }
        // Soft not covered: Soft + Prefer only (no Full).
        // Soft covered: wait for Prefer delivery/plateau before Full.
        return p;
    }
    if (!softCovered) {
        // PreferCache display plateaued, but soft max not covered (LQIP/soft
        // shortfall). SoftOnly is gated by softAttempted; Prefer soft-band
        // still needs to run or Gallery freezes on LQIP.
        if (!m_.softQueued && !m_.softAttempted) {
            p.scheduleSoft = true;
        } else if (!m_.displayQueued) {
            p.scheduleDisplay = true;
            p.displayEdge = softMax;
        }
        return p;
    }

    // --- Full band: soft covered and PreferCache already plateaued ---
    if (needFullBand) {
        if (!m_.tilesQueued) {
            p.scheduleTiles = true;
        }
        if (!m_.fullQueued && !fullDone) {
            p.scheduleFull = true;
            if (p.forgetFullSettled) {
                // already set
            }
        } else if (fullDone && m_.postTilePreferAttempts < 2 && !m_.displayQueued) {
            p.scheduleDisplay = true;
            p.forgetDisplaySettled = true;
            p.displayEdge = m_.want;
        }
        return p;
    }

    // want ≤ overview: PreferCache retries
    if (m_.postTilePreferAttempts < 2 && !m_.displayQueued) {
        p.scheduleDisplay = true;
        p.forgetDisplaySettled = true;
        p.displayEdge = m_.want;
    }
    return p;
}

void Machine::markScheduled(const Plan &plan)
{
    if (plan.scheduleSoft) {
        m_.softQueued = true;
    }
    if (plan.scheduleDisplay) {
        m_.displayQueued = true;
        m_.lastDisplayWant = plan.displayEdge > 0 ? plan.displayEdge : m_.want;
        if (plan.forgetDisplaySettled) {
            ++m_.postTilePreferAttempts;
            m_.preferGaveUp = false;
            m_.lastDisplayGot = 0;
        }
    }
    if (plan.scheduleTiles) {
        m_.tilesQueued = true;
    }
    if (plan.scheduleFull) {
        m_.fullQueued = true;
        m_.fullDone = true;
    }
    if (plan.forgetFullSettled) {
        m_.fullDone = false;
        // scheduleFull will set fullDone again if accepted
    }
}

} // namespace RasterClimb
