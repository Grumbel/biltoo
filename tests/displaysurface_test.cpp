// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displaysurface.h"
#include "display/displayedgepolicy.h"

#include <QtTest/QtTest>

using DisplaySurface::ActionType;
using DisplaySurface::AttachedKind;
using DisplaySurface::State;

class DisplaySurfaceTest : public QObject
{
    Q_OBJECT

private slots:
    void decide_frozen_alwaysNone();
    void decide_fullSourceEqualWant_cropSettled_none();
    void decide_fullSourcePreferCachePlateau_climb();
    void decide_softEqualWant_largeHost_asyncOnly();
    void decide_softEqualWant_overviewHost_shortNeed_climb();
    void decide_softEqualWant_guiHost_attachFull();
    void decide_softEqualWant_noHost_climb();
    void decide_blank_noHost_climb();
    void decide_blank_largeHost_attachSoft();
    void decide_blank_guiHost_attachFull();
    void decide_wantChanged_full_async();
    void decide_climbPending_noDuplicateClimb();
    void controller_bindEvaluate();
    void controller_twoSurfacesIndependentWant();
    // Tile / PreferCache ownership (2234–2236)
    void tilesOwnDisplay_policy();
};

static ContentXform::Value identityXform()
{
    return ContentXform::Value{};
}

static ContentXform::Value cropXform()
{
    ContentXform::Value v;
    v.hasCrop = true;
    v.cropRect = QRect(10, 10, 100, 80);
    v.cropSourceSize = QSize(4000, 3000);
    return v;
}

void DisplaySurfaceTest::decide_frozen_alwaysNone()
{
    State s;
    s.frozen = true;
    s.hostLongEdge = 4000;
    s.needEdge = 1024;
    s.attachedKind = AttachedKind::None;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::None);

    s.attachedKind = AttachedKind::SoftPreview;
    s.haveDisplayEdge = 256;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::None);
}

void DisplaySurfaceTest::decide_fullSourceEqualWant_cropSettled_none()
{
    // Crop pulse root cause: host 4000 vs post-crop shown 200, but host already
    // covers need — FullSource matching want is settled (no soft demote).
    State s;
    s.want = cropXform();
    s.applied = cropXform();
    s.attachedKind = AttachedKind::FullSource;
    s.haveDisplayEdge = 200;
    s.hostLongEdge = 4000;
    s.needEdge = 2048;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::None);
}

void DisplaySurfaceTest::decide_fullSourcePreferCachePlateau_climb()
{
    // Stuck at PreferCache overview (1024) while viewport need is higher.
    State s;
    s.want = identityXform();
    s.applied = identityXform();
    s.attachedKind = AttachedKind::FullSource;
    s.haveDisplayEdge = 1024;
    s.hostLongEdge = 1024;
    s.needEdge = 2048;
    const DisplaySurface::Action a = DisplaySurface::decide(s);
    QCOMPARE(a.type, ActionType::ScheduleClimb);
    QCOMPARE(a.climbNeedEdge, 2048);
}

void DisplaySurfaceTest::decide_softEqualWant_largeHost_asyncOnly()
{
    State s;
    s.want = cropXform();
    s.applied = cropXform();
    s.attachedKind = AttachedKind::SoftPreview;
    s.haveDisplayEdge = 512;
    s.hostLongEdge = 4000;
    s.needEdge = 2048;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::ScheduleAsyncMaterialize);
}

void DisplaySurfaceTest::decide_softEqualWant_overviewHost_shortNeed_climb()
{
    State s;
    s.want = identityXform();
    s.applied = identityXform();
    s.attachedKind = AttachedKind::SoftPreview;
    s.haveDisplayEdge = 1024;
    s.hostLongEdge = 1024;
    s.needEdge = 2048;
    const DisplaySurface::Action a = DisplaySurface::decide(s);
    QCOMPARE(a.type, ActionType::ScheduleClimb);
    QCOMPARE(a.climbNeedEdge, 2048);
}

void DisplaySurfaceTest::decide_softEqualWant_guiHost_attachFull()
{
    // Name kept; soft-band host upgrades stay SoftPreview (not FullSource).
    State s;
    s.want = identityXform();
    s.applied = identityXform();
    s.attachedKind = AttachedKind::SoftPreview;
    s.haveDisplayEdge = 256;
    s.hostLongEdge = 512;
    s.needEdge = 512;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::AttachSoft);
}

void DisplaySurfaceTest::decide_softEqualWant_noHost_climb()
{
    State s;
    s.want = identityXform();
    s.applied = identityXform();
    s.attachedKind = AttachedKind::SoftPreview;
    s.haveDisplayEdge = 128;
    s.hostLongEdge = 0;
    s.needEdge = 512;
    const DisplaySurface::Action a = DisplaySurface::decide(s);
    QCOMPARE(a.type, ActionType::ScheduleClimb);
    QCOMPARE(a.climbNeedEdge, 512);
}

void DisplaySurfaceTest::decide_blank_noHost_climb()
{
    State s;
    s.needEdge = 1024;
    s.hostLongEdge = 0;
    const DisplaySurface::Action a = DisplaySurface::decide(s);
    QCOMPARE(a.type, ActionType::ScheduleClimb);
    QCOMPARE(a.climbNeedEdge, 1024);
}

void DisplaySurfaceTest::decide_blank_largeHost_attachSoft()
{
    State s;
    s.hostLongEdge = 4000;
    s.needEdge = 1024;
    s.want = cropXform();
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::AttachSoft);
}

void DisplaySurfaceTest::decide_blank_guiHost_attachFull()
{
    // Soft-band blank install is SoftPreview so Gallery can still climb.
    State s;
    s.hostLongEdge = 400;
    s.needEdge = 400;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::AttachSoft);
}

void DisplaySurfaceTest::decide_wantChanged_full_async()
{
    State s;
    s.applied = identityXform();
    s.want = cropXform();
    s.attachedKind = AttachedKind::FullSource;
    s.haveDisplayEdge = 2000;
    s.hostLongEdge = 4000;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::ScheduleAsyncMaterialize);
}

void DisplaySurfaceTest::decide_climbPending_noDuplicateClimb()
{
    State s;
    s.needEdge = 512;
    s.hostLongEdge = 0;
    s.climbPending = true;
    QCOMPARE(DisplaySurface::decide(s).type, ActionType::None);
}

void DisplaySurfaceTest::controller_bindEvaluate()
{
    DisplaySurfaceController ctl;
    const auto id = ctl.bind(DisplaySurface::Kind::ImageFocus, QStringLiteral("/a.jpg"),
                             SessionImageId(7));
    QVERIFY(id != DisplaySurface::kInvalidSurfaceId);
    QCOMPARE(ctl.surfaceCount(), 1);
    QVERIFY(ctl.setNeed(id, 1024));
    QVERIFY(ctl.setHostLongEdge(id, 4000));
    QVERIFY(ctl.setWant(id, cropXform()));
    QCOMPARE(ctl.evaluate(id).type, ActionType::AttachSoft);

    QVERIFY(ctl.setAttached(id, AttachedKind::FullSource, 200, cropXform()));
    QCOMPARE(ctl.evaluate(id).type, ActionType::None);

    QVERIFY(ctl.setFrozen(id, true));
    QVERIFY(ctl.setAttached(id, AttachedKind::None, 0, identityXform()));
    QCOMPARE(ctl.evaluate(id).type, ActionType::None);

    ctl.unbind(id);
    QCOMPARE(ctl.surfaceCount(), 0);
}

void DisplaySurfaceTest::controller_twoSurfacesIndependentWant()
{
    DisplaySurfaceController ctl;
    const QString path = QStringLiteral("/same.jpg");
    const auto a = ctl.bind(DisplaySurface::Kind::ImageFocus, path, SessionImageId(1));
    const auto b = ctl.bind(DisplaySurface::Kind::GalleryTile, path, SessionImageId(2));
    QVERIFY(ctl.setHostLongEdge(a, 4000));
    QVERIFY(ctl.setHostLongEdge(b, 4000));
    QVERIFY(ctl.setWant(a, cropXform()));
    QVERIFY(ctl.setWant(b, identityXform()));
    QVERIFY(ctl.setAttached(a, AttachedKind::FullSource, 200, cropXform()));
    QVERIFY(ctl.setAttached(b, AttachedKind::FullSource, 4000, identityXform()));
    QCOMPARE(ctl.evaluate(a).type, ActionType::None);
    QCOMPARE(ctl.evaluate(b).type, ActionType::None);
    // Appearance change on A only.
    ContentXform::Value other = cropXform();
    other.cropRect = QRect(0, 0, 50, 50);
    QVERIFY(ctl.setWant(a, other));
    QCOMPARE(ctl.evaluate(a).type, ActionType::ScheduleAsyncMaterialize);
    QCOMPARE(ctl.evaluate(b).type, ActionType::None);
}


void DisplaySurfaceTest::tilesOwnDisplay_policy()
{
    using DisplayEdgePolicy::tilesOwnDisplay;
    QVERIFY(!tilesOwnDisplay(false, false));
    QVERIFY(tilesOwnDisplay(true, false));
    QVERIFY(tilesOwnDisplay(false, true));
    QVERIFY(tilesOwnDisplay(true, true));
}

QTEST_MAIN(DisplaySurfaceTest)
#include "displaysurface_test.moc"
