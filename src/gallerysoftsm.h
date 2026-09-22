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
 * 3. LQIP have never counts as PreferCache plateau (have ≤ lqip still schedules).
 * 4. Host install uses FullSource when hostEdge > softMax — never SoftPreview
 *    for large host samples (SoftPreview clamp would leave shown << host forever).
 * 5. Host install is a no-op when shown already covers host (strict upgrade only).
 * 6. After kMaxEnsureAttempts ensure cycles without settling, terminal=true —
 *    needsSchedule stays false (no soft storm). Clearing terminal only when
 *    want rises or the path is reset.
 */
namespace GallerySoft {

constexpr int kCoverNumer = 9;
constexpr int kCoverDenom = 10;
constexpr int kDefaultLqipCeiling = 96;
constexpr int kSoftProgressFloor = 128;
/** Hard cap: soft PreferCache ensure cycles per path per want band. */
constexpr int kMaxEnsureAttempts = 6;
/** Host LQIP installs per decode-window tick (steady state). */
constexpr int kMaxInstallsPerDecodeWindow = 24;
/** Host LQIP installs while size-resolve gate is still active. */
constexpr int kMaxInstallsDuringSizeResolve = 16;
/** Wall-clock budget for one decode-window GUI pass (ms). */
constexpr qint64 kDecodeWindowWallMs = 6;
/** Re-arm delay when more installs remain (ms). */
constexpr int kDecodeWindowRearmMs = 32;
/** Soft-state watchdog tick (ms). */
constexpr int kWatchdogIntervalMs = 1000;
/** Gallery status-line refresh after host soft install (ms). */
constexpr int kStatusRefreshMs = 100;
/** Decode-window rearm after scroll / wheel (ms). */
constexpr int kDecodeWindowScrollMs = 80;
/** Decode-window rearm after layout / mode settle (ms). */
constexpr int kDecodeWindowSettleMs = 48;
/** Decode-window rearm after soft install slice (ms). */
constexpr int kDecodeWindowSliceMs = 16;
/** Decode-window rearm for Image/Workspace (non-gallery) interest (ms). */
constexpr int kDecodeWindowImageMs = 150;
/** Decode-window rearm after pack / heavy layout (ms). */
constexpr int kDecodeWindowAfterPackMs = 180;

inline int maxHave(int a, int b)
{
    return qMax(a, b);
}

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
    int ensureAttempts = 0;
    /** True after failed/max attempts — needsSchedule must stay false. */
    bool terminal = false;
    bool failed = false;
};

/** Decode-window: should PathRaster ensure run for this path? */
inline bool needsSchedule(const State &st, int wantEdge, bool anyBlank, bool anyFull)
{
    if (st.failed || st.terminal || anyFull) {
        return false;
    }
    // Cap soft storms: blank may get one last attempt only if under the hard max.
    if (st.ensureAttempts >= kMaxEnsureAttempts) {
        return false;
    }
    if (st.have >= wantEdge && !anyBlank) {
        return false;
    }
    if (st.inflight > 0 && st.have >= kSoftProgressFloor && !anyBlank) {
        return false;
    }
    return true;
}

/** Call when PathRaster ensure is actually issued for this path. */
inline void noteEnsureScheduled(State &st, int wantEdge)
{
    if (wantEdge > st.want) {
        // New higher want band — allow a fresh budget.
        st.ensureAttempts = 0;
        st.terminal = false;
    }
    st.want = wantEdge > 0 ? wantEdge : st.want;
    ++st.ensureAttempts;
    if (st.ensureAttempts >= kMaxEnsureAttempts) {
        st.terminal = true;
        st.inflight = 0;
    }
}

/** Illegal to schedule soft after terminal without want rise / reset. */
inline void assertNotTerminalForSchedule(const State &st)
{
    Q_ASSERT_X(!st.terminal, "GallerySoft",
               "schedule after terminal soft state — soft storm / stuck loop");
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
