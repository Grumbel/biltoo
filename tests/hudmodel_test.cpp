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
}

void HudModelTest::qualityLabel_imagePartial()
{
    using Tier = DisplayEdgePolicy::QualityTier;
    const QString s = HudModel::qualityLabelDetail(
        Tier::HighQuality, 1200, 4000, false, true);
    QVERIFY(s.contains(QStringLiteral("1200")));
    QVERIFY(s.contains(QStringLiteral("4000")));
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

QTEST_MAIN(HudModelTest)
#include "hudmodel_test.moc"
