// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rasterclimbsm.h"

#include <QtTest/QtTest>

using namespace RasterClimb;

class RasterClimbSmTest : public QObject
{
    Q_OBJECT
private slots:
    void covers_band();
    void soft_first_when_blank();
    void soft_then_display_under_overview();
    void soft_covered_high_need_plans_full_same_tick();
    void prefercache_soft_delivery_sets_plateau_then_full();
    void full_shortfall_clears_done_in_plan();
    void host_lru_demotion_resets_queues();
    void reconcile_clears_sticky_queued();
    void escalate_resets_full_done();
    void gave_up_not_terminal_while_short();
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
    m.setWant(256, 0, Policy::SoftDisplay, kSoft, kOverview);
    m.setHaveFromHost(0, kSoft);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleSoft);
    QVERIFY(!p.scheduleFull);
}

void RasterClimbSmTest::soft_then_display_under_overview()
{
    Machine m;
    m.setWant(800, 0, Policy::SoftDisplay, kSoft, kOverview);
    m.setHaveFromHost(0, kSoft);
    Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleSoft);
    // PreferCache may also be requested while soft climbs
    QVERIFY(!p.scheduleFull);

    m.setHaveFromHost(512, kSoft);
    p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.scheduleSoft);
    QVERIFY(p.scheduleDisplay);
    QVERIFY(!p.scheduleFull); // need 800 ≤ overview
}

void RasterClimbSmTest::soft_covered_high_need_plans_full_same_tick()
{
    Machine m;
    m.setWant(4096, 6048, Policy::SoftDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.scheduleSoft);
    // PreferCache allowed as intermediate, but Full must appear same plan
    QVERIFY(p.scheduleFull);
    QVERIFY(p.fullEdge > kOverview);
    QVERIFY(p.fullEdge <= 6048);
}

void RasterClimbSmTest::prefercache_soft_delivery_sets_plateau_then_full()
{
    Machine m;
    m.setWant(4096, 6048, Policy::SoftDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    m.noteDelivery(4096, 512, kSoft); // PreferCache returned soft
    QVERIFY(m.state().preferGaveUp);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleFull);
}

void RasterClimbSmTest::full_shortfall_clears_done_in_plan()
{
    // Intermediate shortfall (TileSynth ~2048) is terminal — no RETRY loop.
    Machine m;
    m.setWant(6048, 6048, Policy::EscalateToFull, kSoft, kOverview);
    m.setHaveFromHost(2048, kSoft);
    m.state().fullDone = true;
    m.state().fullQueued = false;
    Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(!p.forgetFullSettled);
    QVERIFY(!p.scheduleFull);

    // Soft-tier shortfall (have covers soft band but not need) — Full never
    // landed at native; clear settled and retry. have==softMax is soft-tier.
    m.setHaveFromHost(kSoft, kSoft);
    m.state().fullDone = true;
    m.state().fullQueued = false;
    p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.forgetFullSettled);
    QVERIFY(p.scheduleFull);
}

void RasterClimbSmTest::host_lru_demotion_resets_queues()
{
    Machine m;
    m.setWant(512, 0, Policy::SoftDisplay, kSoft, kOverview);
    m.setHaveFromHost(512, kSoft);
    m.state().softQueued = true;
    m.setHaveFromHost(0, kSoft); // LRU eviction
    QCOMPARE(m.state().have, 0);
    QVERIFY(!m.state().softQueued);
    const Plan p = m.plan(kSoft, kOverview, kDispMax);
    QVERIFY(p.scheduleSoft);
}

void RasterClimbSmTest::reconcile_clears_sticky_queued()
{
    Machine m;
    m.setWant(512, 0, Policy::SoftDisplay, kSoft, kOverview);
    m.state().softQueued = true;
    m.state().displayQueued = true;
    PendingFlags none;
    m.reconcilePending(none);
    QVERIFY(!m.state().softQueued);
    QVERIFY(!m.state().displayQueued);
}

void RasterClimbSmTest::escalate_resets_full_done()
{
    Machine m;
    m.setWant(2048, 6048, Policy::SoftDisplay, kSoft, kOverview);
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

QTEST_MAIN(RasterClimbSmTest)
#include "rasterclimbsm_test.moc"
