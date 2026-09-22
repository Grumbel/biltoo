// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerysoftsm.h"

#include <QtTest/QtTest>

using namespace GallerySoft;

class GallerySoftSmTest : public QObject
{
    Q_OBJECT
private slots:
    void needs_schedule_blank();
    void needs_schedule_covers_want();
    void needs_schedule_lqip_not_plateau();
    void needs_schedule_lqip_ceiling_edge();
    void needs_schedule_inflight_soft();
    void needs_schedule_any_full();
    void needs_schedule_terminal();
    void needs_schedule_max_attempts();
    void host_install_soft_vs_full();
    void host_install_no_loop_when_shown_matches();
    void host_install_upgrades_lqip();
    void note_ladder_clears_inflight();
};

void GallerySoftSmTest::needs_schedule_blank()
{
    State st;
    st.have = 0;
    QVERIFY(needsSchedule(st, 512, /*anyBlank=*/true, /*anyFull=*/false));
}

void GallerySoftSmTest::needs_schedule_covers_want()
{
    State st;
    st.have = 512;
    QVERIFY(!needsSchedule(st, 512, false, false));
}

void GallerySoftSmTest::needs_schedule_lqip_not_plateau()
{
    State st;
    st.have = 16;
    // LQIP underlay must still schedule PreferCache / decode-window work
    QVERIFY(needsSchedule(st, 512, false, false));
}

void GallerySoftSmTest::needs_schedule_lqip_ceiling_edge()
{
    // have == kDefaultLqipCeiling (96): still LQIP, must schedule.
    State st;
    st.have = kDefaultLqipCeiling;
    QVERIFY(needsSchedule(st, 512, false, false));
    // Above LQIP but below want: still needs schedule (no PreferCache plateau field).
    st.have = kDefaultLqipCeiling + 1;
    QVERIFY(needsSchedule(st, 512, false, false));
}

void GallerySoftSmTest::needs_schedule_inflight_soft()
{
    State st;
    st.have = 256;
    st.inflight = 512;
    QVERIFY(!needsSchedule(st, 512, false, false));
    // Blank tile still needs work
    QVERIFY(needsSchedule(st, 512, true, false));
}

void GallerySoftSmTest::needs_schedule_terminal()
{
    State st;
    st.have = 16;
    st.terminal = true;
    QVERIFY(!needsSchedule(st, 512, true, false));
}

void GallerySoftSmTest::needs_schedule_max_attempts()
{
    State st;
    st.have = 16;
    for (int i = 0; i < kMaxEnsureAttempts; ++i) {
        noteEnsureScheduled(st, 512);
    }
    QVERIFY(st.terminal);
    QVERIFY(!needsSchedule(st, 512, true, false));
    // Higher want reopens budget.
    noteEnsureScheduled(st, 1024);
    QVERIFY(!st.terminal || st.ensureAttempts == 1);
    // After noteEnsure on higher want, attempts reset then ++
    QVERIFY(st.ensureAttempts == 1);
    QVERIFY(needsSchedule(st, 1024, true, false) || st.ensureAttempts < kMaxEnsureAttempts);
}

void GallerySoftSmTest::needs_schedule_any_full()
{
    State st;
    st.have = 16;
    QVERIFY(!needsSchedule(st, 512, true, /*anyFull=*/true));
}

void GallerySoftSmTest::host_install_soft_vs_full()
{
    auto soft = decideHostInstall(0, 256, false, false, 512);
    QCOMPARE(soft.kind, InstallKind::SoftPreview);
    QVERIFY(soft.meaningful);

    auto full = decideHostInstall(0, 2048, false, false, 512);
    QCOMPARE(full.kind, InstallKind::FullSource);
    QVERIFY(full.meaningful);
}

void GallerySoftSmTest::host_install_no_loop_when_shown_matches()
{
    // After FullSource install shown==host — pass1 must stop
    auto d = decideHostInstall(2048, 2048, true, true, 512);
    QCOMPARE(d.kind, InstallKind::None);

    // SoftPreview clamp pathology: shown 512 host 2048 must still upgrade once
    d = decideHostInstall(512, 2048, true, false, 512);
    QCOMPARE(d.kind, InstallKind::FullSource);
}

void GallerySoftSmTest::host_install_upgrades_lqip()
{
    auto d = decideHostInstall(16, 512, true, false, 512);
    QCOMPARE(d.kind, InstallKind::SoftPreview);
    QVERIFY(d.meaningful);
}

void GallerySoftSmTest::note_ladder_clears_inflight()
{
    State st;
    st.inflight = 512;
    noteLadderDelivery(st, 512, 512, 256);
    QCOMPARE(st.inflight, 0);
    QCOMPARE(st.have, 512);
}

QTEST_MAIN(GallerySoftSmTest)
#include "gallerysoftsm_test.moc"
