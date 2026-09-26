// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/rasterclimbsm.h"

#include <QtTest/QtTest>

using namespace RasterClimb;

class RasterClimbSmTest : public QObject
{
    Q_OBJECT
private slots:
    void covers_band();
    void soft_first_when_blank();
    void soft_then_display_under_overview();
    void soft_covered_high_need_prefers_before_full();
    void prefercache_soft_delivery_sets_plateau_then_full();
    void prefercache_tilesynth_mid_edge_then_full();
    void prefercache_covers_request_but_short_of_want();
    void full_shortfall_clears_done_in_plan();
    void host_lru_demotion_resets_queues();
    void reconcile_clears_sticky_queued();
    void escalate_resets_full_done();
    void gave_up_not_terminal_while_short();
    void soft_shortfall_does_not_reschedule();
    void soft_mid_rung_marks_attempted();
    void host_mid_soft_skips_softonly();
    void lqip_delivery_still_schedules_soft();
};

static constexpr int kSoft = 512;
static constexpr int kOverview = 1024;
static constexpr int kDispMax = 8192;

void RasterClimbSmTest::covers_band()
{
    QVERIFY(covers(512, 512));
    QVERIFY(covers(461, 512)); // ~90%
    QVERIFY(!covers(400, 512));
    QVERIFY(covers(1, 0));
    QVERIFY(!covers(0, 512));
}

void RasterClimbSmTest::soft_first_when_blank()
{
    Machine m;
    m.setWant(256, 0, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(0, kSoft);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleBand);
    QVERIFY(!p.scheduleFull);
}

void RasterClimbSmTest::soft_then_display_under_overview()
{
    Machine m;
    m.setWant(800, 0, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(0, kSoft);
    Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleBand);
    // PreferCache may also be requested while soft climbs
    QVERIFY(!p.scheduleFull);

    m.setHaveFromHost(512, kSoft);
    p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.scheduleBand);
    QVERIFY(p.scheduleDisplay);
    QVERIFY(!p.scheduleFull); // need 800 ≤ overview
}

void RasterClimbSmTest::soft_covered_high_need_prefers_before_full()
{
    Machine m;
    m.setWant(4096, 6048, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.scheduleBand);
    // Soft covered + high need: Prefer first — Full only after Prefer plateau
    // (same-plan Full starved intermediate paints).
    QVERIFY(p.scheduleDisplay);
    QVERIFY(!p.scheduleFull);
    QVERIFY(p.fullEdge > kOverview);
    QVERIFY(p.fullEdge <= 6048);

    m.noteDelivery(4096, 512, kSoft); // PreferCache plateaued at soft
    QVERIFY(m.state().preferGaveUp);
    p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleFull);
}

void RasterClimbSmTest::prefercache_soft_delivery_sets_plateau_then_full()
{
    Machine m;
    m.setWant(4096, 6048, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    m.noteDelivery(4096, 512, kSoft); // PreferCache returned soft
    QVERIFY(m.state().preferGaveUp);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleFull);
}

void RasterClimbSmTest::prefercache_tilesynth_mid_edge_then_full()
{
    // TileSynth PreferCache returned ~2048 for a Full-band want.
    Machine m;
    m.setWant(4096, 6048, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    m.noteDelivery(4096, 2048, kSoft);
    QVERIFY(m.state().preferGaveUp);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleFull);
    QVERIFY(!p.scheduleDisplay); // plateaued; Full band owns the path
}

void RasterClimbSmTest::prefercache_covers_request_but_short_of_want()
{
    // Prefer was scheduled at a clamped edge that TileSynth fully covers, while
    // host want still needs Full-band. Must still preferGaveUp → Full.
    Machine m;
    m.setWant(4096, 6048, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    m.noteDelivery(2048, 2048, kSoft); // covers request, short of want
    QVERIFY(m.state().preferGaveUp);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleFull);
}

void RasterClimbSmTest::full_shortfall_clears_done_in_plan()
{
    // Any Full shortfall is terminal for this cycle (no RETRY).
    Machine m;
    m.setWant(6048, 6048, Policy::EscalateToFull, kSoft, kOverview);
    m.setHaveFromHost(2048, kSoft);
    m.state().fullDone = true;
    m.state().fullQueued = false;
    Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.forgetFullSettled);
    QVERIFY(!p.scheduleFull);

    m.setHaveFromHost(kSoft, kSoft);
    m.state().fullDone = true;
    m.state().fullQueued = false;
    p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.forgetFullSettled);
    QVERIFY(!p.scheduleFull);
}

void RasterClimbSmTest::host_lru_demotion_resets_queues()
{
    Machine m;
    m.setWant(512, 0, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    m.state().bandQueued = true;
    m.setHaveFromHost(0, kSoft); // LRU eviction
    QCOMPARE(m.state().have, 0);
    QVERIFY(!m.state().bandQueued);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleBand);
}

void RasterClimbSmTest::reconcile_clears_sticky_queued()
{
    Machine m;
    m.setWant(512, 0, Policy::TileDisplay, kSoft, kOverview);
    m.state().bandQueued = true;
    m.state().displayQueued = true;
    PendingFlags none;
    m.reconcilePending(none);
    QVERIFY(!m.state().bandQueued);
    QVERIFY(!m.state().displayQueued);
}

void RasterClimbSmTest::escalate_resets_full_done()
{
    Machine m;
    m.setWant(2048, 6048, Policy::TileDisplay, kSoft, kOverview);
    m.state().fullDone = true;
    m.setWant(4096, 6048, Policy::EscalateToFull, kSoft, kOverview);
    QVERIFY(!m.state().fullDone);
}

void RasterClimbSmTest::gave_up_not_terminal_while_short()
{
    Machine m;
    m.setWant(4096, 6048, Policy::EscalateToFull, kSoft, kOverview);
    m.setHaveFromHost(2048, kSoft);
    m.state().preferGaveUp = true;
    m.state().fullDone = true;
    // Still short of need — must not report terminal give-up
    QVERIFY(!m.isGaveUp(kOverview));
}

void RasterClimbSmTest::soft_shortfall_does_not_reschedule()
{
    // SoftOnly returned 128 while softMax is 512 — must not loop SoftOnly.
    Machine m;
    m.setWant(256, 4000, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(0, kSoft);
    Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleBand);

    m.noteDelivery(/*requestEdge=*/512, /*got=*/128, kSoft);
    QVERIFY(m.state().bandAttempted);
    QCOMPARE(m.state().have, 128);
    QVERIFY(!m.state().preferGaveUp); // soft-band shortfall is not Prefer plateau
    p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.scheduleBand);
    // Prefer soft-band / display may still run; Full must not.
    QVERIFY(!p.scheduleFull);
    QVERIFY(!p.forgetBandSettled);
}

void RasterClimbSmTest::host_mid_soft_skips_softonly()
{
    // ImageCache already has mid soft (100): SoftOnly must not loop.
    Machine m;
    m.setWant(512, 4000, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(100, kSoft);
    QVERIFY(m.state().bandAttempted);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.scheduleBand);
    QVERIFY(p.scheduleDisplay);
    QCOMPARE(p.displayEdge, kSoft);
}

void RasterClimbSmTest::soft_mid_rung_marks_attempted()
{
    // SoftOnly returned 100 (above LQIP, below old 128 floor): bandAttempted
    // so Prefer soft at softMax runs instead of SoftOnly forever.
    Machine m;
    m.setWant(512, 4000, Policy::TileDisplay, kSoft, kOverview);
    m.noteDelivery(/*requestEdge=*/256, /*got=*/100, kSoft);
    QVERIFY(m.state().bandAttempted);
    QVERIFY(!m.state().preferGaveUp);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.scheduleBand);
    QVERIFY(p.scheduleDisplay);
    QCOMPARE(p.displayEdge, kSoft);
}

void RasterClimbSmTest::lqip_delivery_still_schedules_soft()
{
    // LQIP (≤96) must not freeze the soft PreferCache climb.
    Machine m;
    m.setWant(512, 4000, Policy::TileDisplay, kSoft, kOverview);
    m.setHaveFromHost(0, kSoft);
    Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleBand);

    m.noteDelivery(/*requestEdge=*/512, /*got=*/64, kSoft);
    QVERIFY(!m.state().bandAttempted);
    QVERIFY(!m.state().preferGaveUp);
    QCOMPARE(m.state().have, 64);
    p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleBand);
}

QTEST_MAIN(RasterClimbSmTest)
#include "rasterclimbsm_test.moc"
