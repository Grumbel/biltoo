// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GALLERYSOFTSM_H
#define GALLERYSOFTSM_H

#include <QtGlobal>

/**
 * Pure Gallery soft/decode-window policy (no I/O).
 *
 * Separates "should we schedule PathRaster?" and "should pass1 install host
 * sample?" from ImageView so SoftPreview clamp loops and inflight stalls are
 * testable invariants.
 *
 * Invariants:
 * 1. needsSchedule is false when anyFull (tile already has FullSource).
 * 2. needsSchedule is false when have covers want and no blank tile.
 * 3. LQIP have never counts as PreferCache plateau (gaveUpWant ignored if have ≤ lqip).
 * 4. Host install uses FullSource when hostEdge > softMax — never SoftPreview
 *    for large host samples (SoftPreview clamp would leave shown << host forever).
 * 5. Host install is a no-op when shown already covers host (strict upgrade only).
 */
namespace GallerySoft {

constexpr int kCoverNumer = 9;
constexpr int kCoverDenom = 10;
constexpr int kDefaultLqipCeiling = 96;
constexpr int kSoftProgressFloor = 128;

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

inline bool isStrictUpgrade(int shown, int incoming)
{
    if (incoming <= 0) {
        return false;
    }
    if (shown <= 0) {
        return true;
    }
    return incoming * kCoverDenom > shown * kCoverNumer
        && incoming > shown;
}

struct State {
    int have = 0;
    int want = 0;
    int inflight = 0;
    int gaveUpWant = 0;
    bool failed = false;
    qint64 inflightSinceMs = 0;
    qint64 weakSinceMs = 0;
};

/** Decode-window: should PathRaster ensure run for this path? */
inline bool needsSchedule(const State &st, int wantEdge, bool anyBlank, bool anyFull,
                          int lqipCeiling = kDefaultLqipCeiling)
{
    if (st.failed || anyFull) {
        return false;
    }
    if (st.have >= wantEdge && !anyBlank) {
        return false;
    }
    if (st.gaveUpWant >= wantEdge && !anyBlank && st.have > lqipCeiling) {
        return false;
    }
    if (st.inflight > 0 && st.have >= kSoftProgressFloor && !anyBlank) {
        return false;
    }
    return true;
}

void noteLadderDelivery(State &st, int requestEdge, int gotEdge, int softFloor);

enum class InstallKind {
    None = 0,
    SoftPreview = 1,
    FullSource = 2,
};

struct InstallDecision {
    InstallKind kind = InstallKind::None;
    /** True when the caller should count this as pass1 work / viewport update. */
    bool meaningful = false;
};

/**
 * Pass1 host→tile install. hostEdge is ImageCache long edge; shownEdge is
 * what the tile currently paints.
 */
InstallDecision decideHostInstall(int shownEdge, int hostEdge, bool hasDisplay,
                                  bool hasFullDecoded, int softMax);

} // namespace GallerySoft

#endif
