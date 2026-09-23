// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "content/contentxform.h"

#include <QtTest/QtTest>

#include <cmath>

/**
 * Paint-path geometry contract for content orient (quarter turns + flips).
 *
 * Mirrors ImageItem tile paint without Qt Widgets:
 *   layout  = ContentXform::layoutSize(native, x)     // contentRect size
 *   offset  = (-layout.w/2, -layout.h/2)
 *   dest    = mapSourceRectToOriented(src, native, x).translated(offset)
 *   clip    = contentRect = QRectF(offset, layout)
 *
 * If dests fall outside contentRect after a turn, tiles look "clipped as if
 * unrotated" or the content box refuses to follow the pixels.
 */
class ContentOrientPaintContractTest : public QObject
{
    Q_OBJECT

private slots:
    void layoutSize_allTurns_landscapeNative();
    void layoutSize_allTurns_portraitNative();
    void fullFrameOrientedAabb_matchesLayoutSize_allTurns();
    void fullFrameOrientedAabb_matchesLayoutSize_allFlipsAndTurns();
    void contentRectContainsMappedFullFrame_allTurns();
    void tileGridDestsCoverContentRect_allTurns();
    void displayToSourceRoundTrip_fullContent_allTurns();
    void successivePlusOneTurns_layoutCycles();
    void axisAlignedCrop_layoutFollowsMappedCrop_allTurns();
};

namespace {

ContentXform::Value orientOnly(int turns, bool hFlip = false, bool vFlip = false)
{
    ContentXform::Value x;
    x.quarterTurns = ContentXform::normalizeQuarterTurns(turns);
    x.hFlip = hFlip;
    x.vFlip = vFlip;
    return x;
}

QRectF contentRectFor(const QSize &layout)
{
    return QRectF(-layout.width() / 2.0, -layout.height() / 2.0,
                  layout.width(), layout.height());
}

QRectF fullSource(const QSize &native)
{
    return QRectF(0, 0, native.width(), native.height());
}

/** Tile grid cells covering native (inclusive grid of cellSize). */
QVector<QRectF> tileCells(const QSize &native, int cellSize)
{
    QVector<QRectF> out;
    if (native.width() < 1 || native.height() < 1 || cellSize < 1) {
        return out;
    }
    for (int y = 0; y < native.height(); y += cellSize) {
        for (int x = 0; x < native.width(); x += cellSize) {
            const int w = qMin(cellSize, native.width() - x);
            const int h = qMin(cellSize, native.height() - y);
            out.push_back(QRectF(x, y, w, h));
        }
    }
    return out;
}

bool nearlyContains(const QRectF &outer, const QRectF &inner, qreal pad = 1.5)
{
    const QRectF o = outer.adjusted(-pad, -pad, pad, pad);
    return o.contains(inner.topLeft()) && o.contains(inner.topRight())
        && o.contains(inner.bottomLeft()) && o.contains(inner.bottomRight());
}

} // namespace

void ContentOrientPaintContractTest::layoutSize_allTurns_landscapeNative()
{
    const QSize native(800, 600);
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(0)), QSize(800, 600));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(1)), QSize(600, 800));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(2)), QSize(800, 600));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(3)), QSize(600, 800));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(4)), QSize(800, 600));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(-1)), QSize(600, 800));
}

void ContentOrientPaintContractTest::layoutSize_allTurns_portraitNative()
{
    const QSize native(400, 900);
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(0)), QSize(400, 900));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(1)), QSize(900, 400));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(2)), QSize(400, 900));
    QCOMPARE(ContentXform::layoutSize(native, orientOnly(3)), QSize(900, 400));
}

void ContentOrientPaintContractTest::fullFrameOrientedAabb_matchesLayoutSize_allTurns()
{
    const QSize natives[] = {QSize(800, 600), QSize(600, 800), QSize(1024, 768),
                             QSize(1, 1), QSize(2, 5), QSize(1920, 1080)};
    for (const QSize &native : natives) {
        for (int turns = 0; turns < 4; ++turns) {
            const ContentXform::Value x = orientOnly(turns);
            const QSize lay = ContentXform::layoutSize(native, x);
            const QRectF ori =
                ContentXform::mapSourceRectToOriented(fullSource(native), native, x);
            QVERIFY2(!ori.isEmpty(),
                     qPrintable(QStringLiteral("empty orient AABB native=%1x%2 turns=%3")
                                    .arg(native.width())
                                    .arg(native.height())
                                    .arg(turns)));
            // AABB must match layout size (item contentRect).
            QVERIFY2(qAbs(ori.width() - lay.width()) < 1.01
                         && qAbs(ori.height() - lay.height()) < 1.01,
                     qPrintable(QStringLiteral(
                                    "orient AABB %1x%2 vs layout %3x%4 native=%5x%6 turns=%7")
                                    .arg(ori.width())
                                    .arg(ori.height())
                                    .arg(lay.width())
                                    .arg(lay.height())
                                    .arg(native.width())
                                    .arg(native.height())
                                    .arg(turns)));
            // Origin of oriented full frame is (0,0) in orient space.
            QVERIFY2(qAbs(ori.left()) < 1.01 && qAbs(ori.top()) < 1.01,
                     qPrintable(QStringLiteral("orient origin %1,%2 turns=%3")
                                    .arg(ori.left())
                                    .arg(ori.top())
                                    .arg(turns)));
        }
    }
}

void ContentOrientPaintContractTest::fullFrameOrientedAabb_matchesLayoutSize_allFlipsAndTurns()
{
    const QSize native(640, 480);
    for (int hf = 0; hf < 2; ++hf) {
        for (int vf = 0; vf < 2; ++vf) {
            for (int turns = 0; turns < 4; ++turns) {
                const ContentXform::Value x = orientOnly(turns, hf != 0, vf != 0);
                const QSize lay = ContentXform::layoutSize(native, x);
                // Flips must not change layout size (only turns).
                QCOMPARE(lay, ContentXform::layoutSize(native, orientOnly(turns)));
                const QRectF ori =
                    ContentXform::mapSourceRectToOriented(fullSource(native), native, x);
                QVERIFY2(qAbs(ori.width() - lay.width()) < 1.01
                             && qAbs(ori.height() - lay.height()) < 1.01,
                         qPrintable(QStringLiteral(
                                        "flip+turn AABB %1x%2 layout %3x%4 h=%5 v=%6 t=%7")
                                        .arg(ori.width())
                                        .arg(ori.height())
                                        .arg(lay.width())
                                        .arg(lay.height())
                                        .arg(hf)
                                        .arg(vf)
                                        .arg(turns)));
            }
        }
    }
}

void ContentOrientPaintContractTest::contentRectContainsMappedFullFrame_allTurns()
{
    const QSize native(800, 600);
    for (int turns = 0; turns < 4; ++turns) {
        const ContentXform::Value x = orientOnly(turns);
        const QSize lay = ContentXform::layoutSize(native, x);
        const QRectF cr = contentRectFor(lay);
        const QPointF off = cr.topLeft();
        const QRectF ori =
            ContentXform::mapSourceRectToOriented(fullSource(native), native, x);
        const QRectF dest(ori.x() + off.x(), ori.y() + off.y(), ori.width(),
                          ori.height());
        QVERIFY2(nearlyContains(cr, dest, 1.5),
                 qPrintable(QStringLiteral(
                                "full dest outside contentRect turns=%1 dest=%2,%3 %4x%5 "
                                "cr=%6,%7 %8x%9")
                                .arg(turns)
                                .arg(dest.left())
                                .arg(dest.top())
                                .arg(dest.width())
                                .arg(dest.height())
                                .arg(cr.left())
                                .arg(cr.top())
                                .arg(cr.width())
                                .arg(cr.height())));
        // Dest should essentially *be* contentRect.
        QVERIFY2(qAbs(dest.left() - cr.left()) < 1.5
                     && qAbs(dest.top() - cr.top()) < 1.5
                     && qAbs(dest.width() - cr.width()) < 1.5
                     && qAbs(dest.height() - cr.height()) < 1.5,
                 qPrintable(QStringLiteral("full dest != contentRect turns=%1").arg(turns)));
    }
}

void ContentOrientPaintContractTest::tileGridDestsCoverContentRect_allTurns()
{
    const QSize native(512, 384);
    constexpr int kCell = 256;
    for (int turns = 0; turns < 4; ++turns) {
        const ContentXform::Value x = orientOnly(turns);
        const QSize lay = ContentXform::layoutSize(native, x);
        const QRectF cr = contentRectFor(lay);
        const QPointF off = cr.topLeft();

        QRectF unionDest;
        bool any = false;
        for (const QRectF &cell : tileCells(native, kCell)) {
            const QRectF ori =
                ContentXform::mapSourceRectToOriented(cell, native, x);
            QVERIFY2(!ori.isEmpty(),
                     qPrintable(QStringLiteral("empty cell orient turns=%1").arg(turns)));
            const QRectF dest(ori.x() + off.x(), ori.y() + off.y(), ori.width(),
                              ori.height());
            QVERIFY2(nearlyContains(cr, dest, 2.0),
                     qPrintable(QStringLiteral(
                                    "tile dest outside contentRect turns=%1 cell=%2,%3 "
                                    "dest=%4,%5 %6x%7")
                                    .arg(turns)
                                    .arg(cell.left())
                                    .arg(cell.top())
                                    .arg(dest.left())
                                    .arg(dest.top())
                                    .arg(dest.width())
                                    .arg(dest.height())));
            if (!any) {
                unionDest = dest;
                any = true;
            } else {
                unionDest = unionDest.united(dest);
            }
        }
        QVERIFY(any);
        // Union of tile dests must cover the content box (within a pixel).
        QVERIFY2(nearlyContains(unionDest.adjusted(-2, -2, 2, 2), cr, 0.0)
                     || (qAbs(unionDest.width() - cr.width()) < 3.0
                         && qAbs(unionDest.height() - cr.height()) < 3.0
                         && qAbs(unionDest.center().x() - cr.center().x()) < 3.0
                         && qAbs(unionDest.center().y() - cr.center().y()) < 3.0),
                 qPrintable(QStringLiteral(
                                "tile union does not cover contentRect turns=%1 "
                                "union=%2,%3 %4x%5 cr=%6,%7 %8x%9")
                                .arg(turns)
                                .arg(unionDest.left())
                                .arg(unionDest.top())
                                .arg(unionDest.width())
                                .arg(unionDest.height())
                                .arg(cr.left())
                                .arg(cr.top())
                                .arg(cr.width())
                                .arg(cr.height())));
    }
}

void ContentOrientPaintContractTest::displayToSourceRoundTrip_fullContent_allTurns()
{
    const QSize native(800, 600);
    for (int turns = 0; turns < 4; ++turns) {
        const ContentXform::Value x = orientOnly(turns);
        const QSize lay = ContentXform::layoutSize(native, x);
        // Display space before item offset: (0,0)..layout
        const QRectF display(0, 0, lay.width(), lay.height());
        const QRectF back = ContentXform::mapDisplayRectToSource(display, native, x);
        QVERIFY2(!back.isEmpty(),
                 qPrintable(QStringLiteral("empty inverse turns=%1").arg(turns)));
        // Should recover the full native frame.
        QVERIFY2(qAbs(back.width() - native.width()) < 2.5
                     && qAbs(back.height() - native.height()) < 2.5,
                 qPrintable(QStringLiteral(
                                "inverse size %1x%2 vs native %3x%4 turns=%5")
                                .arg(back.width())
                                .arg(back.height())
                                .arg(native.width())
                                .arg(native.height())
                                .arg(turns)));
        QVERIFY2(qAbs(back.left()) < 2.5 && qAbs(back.top()) < 2.5,
                 qPrintable(QStringLiteral("inverse origin %1,%2 turns=%3")
                                .arg(back.left())
                                .arg(back.top())
                                .arg(turns)));
    }
}

void ContentOrientPaintContractTest::successivePlusOneTurns_layoutCycles()
{
    const QSize native(1000, 400);
    ContentXform::Value x;
    QSize lay = ContentXform::layoutSize(native, x);
    QCOMPARE(lay, native);
    for (int step = 1; step <= 8; ++step) {
        x.quarterTurns = ContentXform::normalizeQuarterTurns(x.quarterTurns + 1);
        lay = ContentXform::layoutSize(native, x);
        const QSize expect = ContentXform::layoutSize(native, orientOnly(step));
        QCOMPARE(lay, expect);
        const QRectF ori =
            ContentXform::mapSourceRectToOriented(fullSource(native), native, x);
        QVERIFY2(qAbs(ori.width() - lay.width()) < 1.01
                     && qAbs(ori.height() - lay.height()) < 1.01,
                 qPrintable(QStringLiteral("step %1 AABB mismatch").arg(step)));
    }
}

void ContentOrientPaintContractTest::axisAlignedCrop_layoutFollowsMappedCrop_allTurns()
{
    // Crop in oriented space after turns must define layoutSize; paint dests
    // are crop-local after subtract origin (same as ImageItem tile path).
    //
    // Do *not* round-trip AABB inverse→forward: mapDisplayRectToSource returns an
    // AABB that expands under odd turns, so forward mapSourceRectToOriented of
    // that AABB can miss the crop window. Test the real forward paint path only.
    const QSize native(800, 600);
    for (int turns = 0; turns < 4; ++turns) {
        ContentXform::Value x = orientOnly(turns);
        const QSize oriented =
            ContentXform::swapsAspect(x) ? QSize(native.height(), native.width())
                                         : native;
        // Crop a 200×150 window in oriented space.
        x.hasCrop = true;
        x.cropRect = QRect(50, 40, 200, 150);
        x.cropSourceSize = oriented;
        x.cropRotation = 0.0;

        const QSize lay = ContentXform::layoutSize(native, x);
        QCOMPARE(lay, QSize(200, 150));

        const QRectF cr = contentRectFor(lay);
        const QPointF off = cr.topLeft();
        const QRect crop = x.cropRect.normalized();
        const QRectF cropLocal(0.0, 0.0, crop.width(), crop.height());

        int hit = 0;
        for (const QRectF &cell : tileCells(native, 128)) {
            const QRectF ori =
                ContentXform::mapSourceRectToOriented(cell, native, x);
            if (ori.isEmpty()) {
                continue;
            }
            QRectF local = ori.translated(-crop.x(), -crop.y());
            local = local.intersected(cropLocal);
            if (local.isEmpty()) {
                continue; // cell outside the crop window
            }
            ++hit;
            const QRectF dest(local.x() + off.x(), local.y() + off.y(),
                              local.width(), local.height());
            QVERIFY2(nearlyContains(cr, dest, 2.0),
                     qPrintable(QStringLiteral(
                                    "crop dest outside contentRect turns=%1 "
                                    "cell=%2,%3 dest=%4,%5 %6x%7")
                                    .arg(turns)
                                    .arg(cell.left())
                                    .arg(cell.top())
                                    .arg(dest.left())
                                    .arg(dest.top())
                                    .arg(dest.width())
                                    .arg(dest.height())));

            // mapSourceRectToDisplay must agree with crop-local + (0,0) origin.
            const QRectF viaDisplay =
                ContentXform::mapSourceRectToDisplay(cell, native, x);
            QVERIFY2(!viaDisplay.isEmpty(),
                     qPrintable(QStringLiteral(
                                    "mapSourceRectToDisplay empty turns=%1").arg(turns)));
            QVERIFY2(nearlyContains(cropLocal, viaDisplay, 2.0),
                     qPrintable(QStringLiteral(
                                    "display map outside crop-local turns=%1").arg(turns)));
        }
        QVERIFY2(hit > 0,
                 qPrintable(QStringLiteral(
                                "no tile cells intersected crop turns=%1").arg(turns)));
    }
}

QTEST_MAIN(ContentOrientPaintContractTest)
#include "content_orient_paint_contract_test.moc"
