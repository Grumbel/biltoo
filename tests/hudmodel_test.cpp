// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hud/hudmodel.h"
#include "display/displayedgepolicy.h"

#include <QtTest/QtTest>

class HudModelTest : public QObject
{
    Q_OBJECT
private slots:
    void qualityTiers_labels();
    void qualityLabel_galleryDetail();
    void qualityLabel_imagePartial();
    void qualityLabel_fullResolution();
    void sessionBadge_valid();
    void sessionBadge_invalid();
    void emptyCanvas_ready();
    void emptyCanvas_gallery();
    void multiItemHeader_gallery();
    void formatMultiItemStatus_basic();
    void formatImageModeStatus_basic();
    void formatImageModeStatus_editedFlip();
};

void HudModelTest::qualityTiers_labels()
{
    using Tier = DisplayEdgePolicy::QualityTier;
    QVERIFY(!HudModel::qualityTierLabel(Tier::Loading).isEmpty());
    QVERIFY(!HudModel::qualityTierLabel(Tier::FullResolution).isEmpty());
    QVERIFY(!HudModel::qualityTierLabel(Tier::Preview).isEmpty());
    QVERIFY(!HudModel::qualityTierLabel(Tier::Placeholder).isEmpty());
    // Loading and Full are terminal single-word paths in qualityLabelDetail
    QCOMPARE(HudModel::qualityLabelDetail(Tier::Loading, 0, 0, false, true),
             HudModel::qualityTierLabel(Tier::Loading));
    QCOMPARE(HudModel::qualityLabelDetail(Tier::FullResolution, 4000, 4000, false, true),
             HudModel::qualityTierLabel(Tier::FullResolution));
}

void HudModelTest::qualityLabel_galleryDetail()
{
    using Tier = DisplayEdgePolicy::QualityTier;
    const QString s = HudModel::qualityLabelDetail(
        Tier::Preview, 512, 4000, /*gallery*/ true, /*image*/ false, 800, 512);
    QVERIFY(s.contains(QStringLiteral("512")));
    QVERIFY(s.contains(QStringLiteral("800")));
    QVERIFY(s.contains(QStringLiteral("need")));
    // No pipeline jargon (show/have) after tiles-everywhere HUD cleanup.
    QVERIFY(!s.contains(QStringLiteral("show")));
    QVERIFY(!s.contains(QStringLiteral("have")));
    // Covered need → tier only.
    const QString covered = HudModel::qualityLabelDetail(
        Tier::Preview, 800, 4000, true, false, 800, 800);
    QCOMPARE(covered, HudModel::qualityTierLabel(Tier::Preview));
}

void HudModelTest::qualityLabel_imagePartial()
{
    using Tier = DisplayEdgePolicy::QualityTier;
    const QString s = HudModel::qualityLabelDetail(
        Tier::HighQuality, 1200, 4000, false, true);
    QVERIFY(s.contains(QStringLiteral("1200")));
    QVERIFY(s.contains(QStringLiteral("4000")));
    QVERIFY(s.contains(QStringLiteral("of")));
    QVERIFY(!s.contains(QStringLiteral("show")));
}

void HudModelTest::qualityLabel_fullResolution()
{
    using Tier = DisplayEdgePolicy::QualityTier;
    // FullResolution short-circuits even with gallery need/have
    const QString s = HudModel::qualityLabelDetail(
        Tier::FullResolution, 4000, 4000, true, false, 800, 4000);
    QCOMPARE(s, HudModel::qualityTierLabel(Tier::FullResolution));
}

void HudModelTest::sessionBadge_valid()
{
    QCOMPARE(HudModel::sessionBadge(0, 10), QStringLiteral("1/10"));
    QCOMPARE(HudModel::sessionBadge(9, 10), QStringLiteral("10/10"));
}

void HudModelTest::sessionBadge_invalid()
{
    QVERIFY(HudModel::sessionBadge(-1, 10).isEmpty());
    QVERIFY(HudModel::sessionBadge(0, 0).isEmpty());
}

void HudModelTest::emptyCanvas_ready()
{
    const QString s = HudModel::emptyCanvasStatus(
        false, {}, false, false, false, false);
    QVERIFY(!s.isEmpty());
}

void HudModelTest::emptyCanvas_gallery()
{
    const QString s = HudModel::emptyCanvasStatus(
        false, {}, false, false, true, false);
    QVERIFY(s.contains(QStringLiteral("Gallery")));
}

void HudModelTest::multiItemHeader_gallery()
{
    const QString s = HudModel::multiItemHeader(true, 42, 100);
    QVERIFY(s.contains(QStringLiteral("Gallery")));
    QVERIFY(s.contains(QStringLiteral("42")));
    QVERIFY(s.contains(QStringLiteral("100")));
}


void HudModelTest::formatMultiItemStatus_basic()
{
    const QString s = HudModel::formatMultiItemStatusLine(
        /*gallery*/ true, /*count*/ 3, /*zoom*/ 100,
        /*quality*/ QStringLiteral("Preview"), /*edge*/ 512, QSize(4000, 3000),
        /*galleryDbg*/ false, 0, 0, 0, 0, 0,
        /*pending*/ 0, /*loading*/ {},
        /*wsSel*/ false, 1.0, 1.0, 0.0,
        /*edited*/ false, /*dbg*/ {});
    QVERIFY(s.contains(QStringLiteral("Gallery")));
    QVERIFY(s.contains(QStringLiteral("3")));
    QVERIFY(s.contains(QStringLiteral("Preview")));
    QVERIFY(s.contains(QStringLiteral("4000")));
    QVERIFY(!s.contains(QStringLiteral("Edited")));

    // Quality string already carries px — must not append a second edge.
    const QString detailed = HudModel::formatMultiItemStatusLine(
        true, 3, 100,
        QStringLiteral("Preview · 128px (need 512px)"), 128, QSize(4000, 3000),
        false, 0, 0, 0, 0, 0, 0, {}, false, 1.0, 1.0, 0.0, false, {});
    QCOMPARE(detailed.count(QStringLiteral("128")), 1);
    QVERIFY(!detailed.contains(QStringLiteral("(128px)")));
}

void HudModelTest::formatImageModeStatus_basic()
{
    const QString s = HudModel::formatImageModeStatusLine(
        1920, 1080, 50,
        QStringLiteral("High quality"), 960, /*appendEdge*/ true,
        /*climb*/ {},
        /*rot*/ 0, false, false,
        /*edited*/ false, /*dbg*/ {});
    QVERIFY(s.contains(QStringLiteral("1920")));
    QVERIFY(s.contains(QStringLiteral("1080")));
    QVERIFY(s.contains(QStringLiteral("50")));
    QVERIFY(s.contains(QStringLiteral("High quality")));
}

void HudModelTest::formatImageModeStatus_editedFlip()
{
    const QString s = HudModel::formatImageModeStatusLine(
        800, 600, 100,
        QStringLiteral("Full"), 800, false,
        {},
        90.0, true, false,
        true, {});
    QVERIFY(s.contains(QStringLiteral("Edited")));
    // rotation / flip suffix present when non-zero
    QVERIFY(s.contains(QStringLiteral("90")) || s.contains(QStringLiteral("Flip")));
}


QTEST_MAIN(HudModelTest)
#include "hudmodel_test.moc"
