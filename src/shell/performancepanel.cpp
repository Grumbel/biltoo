// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/performancepanel.h"

#include "host/thumtoocache.h"
#include "imageview.h"
#include "util/backgroundworklog.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString stateColor(const QString &state)
{
    if (state == QLatin1String("hot")) {
        return QStringLiteral("#c44");
    }
    if (state == QLatin1String("busy")) {
        return QStringLiteral("#c90");
    }
    if (state == QLatin1String("ok")) {
        return QStringLiteral("#2a8");
    }
    return QStringLiteral("#888");
}

} // namespace

PerformancePanel::PerformancePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Background work"), this);
    {
        QFont f = title->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 1);
        title->setFont(f);
    }
    header->addWidget(title);
    m_statusBadge = new QLabel(tr("Idle"), this);
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setMinimumWidth(72);
    m_statusBadge->setStyleSheet(
        QStringLiteral("QLabel { padding: 2px 10px; border-radius: 10px; "
                       "background: #2a8; color: white; font-weight: 600; }"));
    header->addStretch(1);
    header->addWidget(m_statusBadge);
    root->addLayout(header);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    grid->addWidget(makeMetricCard(tr("Qt pool"), &m_poolValue), 0, 0);
    grid->addWidget(makeMetricCard(tr("Thumtoo queue"), &m_queueValue), 0, 1);
    grid->addWidget(makeMetricCard(tr("FocusFull"), &m_focusValue), 1, 0);
    grid->addWidget(makeMetricCard(tr("Size probes"), &m_probeValue), 1, 1);
    grid->addWidget(makeMetricCard(tr("Activity"), &m_activityValue), 2, 0);
    grid->addWidget(makeMetricCard(tr("Δ / 0.4s"), &m_deltaValue), 2, 1);
    root->addLayout(grid);

    auto *intentTitle = new QLabel(tr("What the system is trying to do"), this);
    {
        QFont f = intentTitle->font();
        f.setBold(true);
        intentTitle->setFont(f);
    }
    root->addWidget(intentTitle);

    m_intent = new QLabel(this);
    m_intent->setWordWrap(true);
    m_intent->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_intent->setStyleSheet(
        QStringLiteral("QLabel { background: palette(base); border: 1px solid palette(mid); "
                       "border-radius: 6px; padding: 8px; }"));
    root->addWidget(m_intent);

    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
    root->addWidget(m_detail);

    auto *btnRow = new QHBoxLayout;
    auto *copyBtn = new QPushButton(tr("Copy report"), this);
    connect(copyBtn, &QPushButton::clicked, this, &PerformancePanel::copyReport);
    auto *clearBtn = new QPushButton(tr("Clear counters"), this);
    clearBtn->setFlat(true);
    connect(clearBtn, &QPushButton::clicked, this, &PerformancePanel::clearLog);
    btnRow->addWidget(copyBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch(1);
    auto *hint = new QLabel(tr("Samples only while open"), this);
    hint->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); font-size: 11px; }"));
    btnRow->addWidget(hint);
    root->addLayout(btnRow);

    auto *logTitle = new QLabel(tr("Recent schedules"), this);
    {
        QFont f = logTitle->font();
        f.setBold(true);
        logTitle->setFont(f);
    }
    root->addWidget(logTitle);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(250);
    m_log->setPlaceholderText(tr("Schedule events appear here while sampling…"));
    {
        QFont mono = m_log->font();
        mono.setFamily(QStringLiteral("Monospace"));
        mono.setStyleHint(QFont::TypeWriter);
        mono.setPointSize(qMax(9, mono.pointSize() - 1));
        m_log->setFont(mono);
    }
    m_log->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background: palette(base); border: 1px solid palette(mid); "
                       "border-radius: 4px; padding: 4px; }"));
    root->addWidget(m_log, 1);

    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &PerformancePanel::refresh);
}

QFrame *PerformancePanel::makeMetricCard(const QString &title, QLabel **valueOut)
{
    auto *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(
        QStringLiteral("QFrame { background: palette(base); border: 1px solid palette(mid); "
                       "border-radius: 6px; }"));
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(2);
    auto *cap = new QLabel(title, card);
    cap->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); border: none; font-size: 11px; }"));
    auto *val = new QLabel(QStringLiteral("—"), card);
    {
        QFont f = val->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 1);
        val->setFont(f);
    }
    val->setStyleSheet(QStringLiteral("QLabel { border: none; }"));
    val->setTextInteractionFlags(Qt::TextSelectableByMouse);
    val->setWordWrap(true);
    lay->addWidget(cap);
    lay->addWidget(val);
    *valueOut = val;
    return card;
}

void PerformancePanel::setCardValue(QLabel *value, const QString &text, const QString &state)
{
    if (!value) {
        return;
    }
    value->setText(text);
    value->setStyleSheet(
        QStringLiteral("QLabel { border: none; color: %1; font-weight: 600; }")
            .arg(stateColor(state)));
}

void PerformancePanel::setImageView(ImageView *view)
{
    m_view = view;
}

void PerformancePanel::clearLog()
{
    BackgroundWorkLog::clear();
    if (m_log) {
        m_log->clear();
    }
    refresh();
}

void PerformancePanel::copyReport()
{
    refresh();
    if (QClipboard *clip = QApplication::clipboard()) {
        clip->setText(m_lastReport.isEmpty() ? buildReportText() : m_lastReport);
    }
}

QString PerformancePanel::buildReportText() const
{
    return m_lastReport;
}

void PerformancePanel::refresh()
{
    if (!isVisible()) {
        if (m_timer && m_timer->isActive()) {
            m_timer->stop();
        }
        return;
    }
    if (m_timer && !m_timer->isActive()) {
        m_timer->start();
    }

    const BackgroundWorkLog::Snapshot snap = BackgroundWorkLog::takeSnapshot();
    const ThumtooCache::WorkActivity act = ThumtooCache::workActivity();
    const ThumtooCache::HostQueuePressure q = ThumtooCache::hostQueuePressure();
    const ThumtooCache::ProbeQueueSnapshot pq = ThumtooCache::probeQueueSnapshot();

    int pendingDecode = 0;
    QString mode = QStringLiteral("?");
    if (m_view) {
        pendingDecode = m_view->pendingDecodeCount();
        if (m_view->isGalleryMode()) {
            mode = QStringLiteral("Gallery");
        } else if (m_view->isImageMode()) {
            mode = QStringLiteral("Image");
        } else if (m_view->isWorkspaceMode()) {
            mode = QStringLiteral("Workspace");
        }
    }

    const quint64 actTotal = act.sizeQueued + act.sizeRunning + act.softQueued + act.softRunning
        + act.tileQueued + act.tileRunning + act.archiveReadRunning;
    const bool focusHot = q.focusFullInflight > 1;
    const bool queueBusy = q.pending > 0 || q.inflight > 0 || q.focusFullInflight > 0;
    const bool poolBusy = snap.qtPoolActive > 0;
    const bool probeBusy = pq.busy;
    const bool deltaBusy = (snap.delta.poolStarts + snap.delta.scheduleProbe
                            + snap.delta.scheduleRevalidate + snap.delta.schedulePixels
                            + snap.delta.scheduleTile + snap.delta.scheduleGalleryDecode) > 0;
    const bool tickOnly = !deltaBusy && snap.delta.tileLodTicks > 0;

    QString badgeText = tr("Settled");
    QString badgeBg = QStringLiteral("#2a8");
    if (focusHot || (q.focusFullInflight > 0 && q.inflight > 20)) {
        badgeText = tr("Hot");
        badgeBg = QStringLiteral("#c44");
    } else if (queueBusy || poolBusy || actTotal > 0 || deltaBusy || probeBusy) {
        badgeText = tr("Working");
        badgeBg = QStringLiteral("#c90");
    } else if (tickOnly) {
        badgeText = tr("Idle tick");
        badgeBg = QStringLiteral("#68a");
    }
    m_statusBadge->setText(badgeText);
    m_statusBadge->setStyleSheet(
        QStringLiteral("QLabel { padding: 2px 10px; border-radius: 10px; "
                       "background: %1; color: white; font-weight: 600; }")
            .arg(badgeBg));

    setCardValue(m_poolValue,
                 tr("%1 active / %2 max").arg(snap.qtPoolActive).arg(snap.qtPoolMax),
                 poolBusy ? QStringLiteral("busy") : QStringLiteral("ok"));
    setCardValue(m_queueValue,
                 tr("pending %1 · inflight %2").arg(q.pending).arg(q.inflight),
                 queueBusy ? (q.inflight > 16 ? QStringLiteral("hot") : QStringLiteral("busy"))
                          : QStringLiteral("ok"));
    setCardValue(m_focusValue,
                 tr("%1 in flight (cap 1)").arg(q.focusFullInflight),
                 focusHot ? QStringLiteral("hot")
                          : (q.focusFullInflight > 0 ? QStringLiteral("busy") : QStringLiteral("ok")));
    setCardValue(m_probeValue,
                 tr("%1 queued · %2 in flight").arg(pq.queued).arg(pq.inflight),
                 probeBusy ? QStringLiteral("busy") : QStringLiteral("ok"));
    setCardValue(m_activityValue,
                 tr("size %1/%2  soft %3/%4  tile %5/%6")
                     .arg(act.sizeQueued).arg(act.sizeRunning)
                     .arg(act.softQueued).arg(act.softRunning)
                     .arg(act.tileQueued).arg(act.tileRunning),
                 actTotal > 0 ? QStringLiteral("busy") : QStringLiteral("ok"));
    setCardValue(m_deltaValue,
                 tr("probe %1 · reval %2 · tile %3 · gallery %4 · tileLod %5")
                     .arg(snap.delta.scheduleProbe)
                     .arg(snap.delta.scheduleRevalidate)
                     .arg(snap.delta.scheduleTile)
                     .arg(snap.delta.scheduleGalleryDecode)
                     .arg(snap.delta.tileLodTicks),
                 deltaBusy || tickOnly ? QStringLiteral("busy") : QStringLiteral("ok"));

    // Intent narrative
    QStringList goals;
    goals << tr("Mode: %1").arg(mode);
    if (probeBusy) {
        goals << tr("• Size-resolve: discovering native width×height for session paths "
                    "(%1 queued, %2 in flight). Needed before Gallery pack / tiles. "
                    "Hot SQLite still paid this when process size memos were cold.")
                    .arg(pq.queued)
                    .arg(pq.inflight);
    } else {
        goals << tr("• Size-resolve: idle (process memos cover known sizes).");
    }
    if (q.focusFullInflight > 0) {
        goals << tr("• FocusFull: building durable tile pyramids (full-res decode at scale 0). "
                    "Should be rare in Gallery overview — Image primary / explicit prepare only.");
    } else {
        goals << tr("• FocusFull: none (good for overview).");
    }
    if (act.tileQueued + act.tileRunning > 0) {
        goals << tr("• Interactive tiles: filling on-screen cells (or filmstrip). "
                    "Archive members at scale>0 use JPEG DCT shrink; scale 0 is full decode.");
        if (!act.tileRunningLabels.isEmpty()) {
            goals << tr("  Running: %1").arg(act.tileRunningLabels.mid(0, 4).join(QLatin1String("; ")));
        }
    } else {
        goals << tr("• Interactive tiles: idle.");
    }
    if (snap.delta.scheduleRevalidate > 0) {
        goals << tr("• Background revalidate: comparing source mtime/size to Store locator "
                    "(%1 starts this interval) — can re-queue probes on NFS.").arg(snap.delta.scheduleRevalidate);
    }
    if (snap.delta.tileLodTicks > 0 && !deltaBusy) {
        goals << tr("• Tile LOD timer still ticking (coverage not marked complete for some cells).");
    }
    if (pendingDecode > 0) {
        goals << tr("• Host pendingDecode=%1").arg(pendingDecode);
    }
    m_intent->setText(goals.join(QLatin1Char('\n')));

    QStringList detail;
    detail << tr("Totals  probe %1  reval %2  tile %3  gallery %4  tileLod %5  poolStarts %6")
                  .arg(snap.totals.scheduleProbe)
                  .arg(snap.totals.scheduleRevalidate)
                  .arg(snap.totals.scheduleTile)
                  .arg(snap.totals.scheduleGalleryDecode)
                  .arg(snap.totals.tileLodTicks)
                  .arg(snap.totals.poolStarts);
    const QString breakdown = ThumtooCache::loadingBreakdownLabel();
    if (!breakdown.isEmpty()) {
        detail << tr("Loading: %1").arg(breakdown);
    }
    if (focusHot) {
        detail << tr("⚠ FocusFull > 1 — link thumtoo with focus-full-no-busy-wait");
    }
    m_detail->setText(detail.join(QLatin1Char('\n')));

    // Clipboard report
    QStringList report;
    report << QStringLiteral("biltoo Performance report");
    report << QStringLiteral("time=%1 status=%2 mode=%3")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODate), badgeText, mode);
    report << QStringLiteral("qt_pool active=%1 max=%2").arg(snap.qtPoolActive).arg(snap.qtPoolMax);
    report << QStringLiteral("thumtoo pending=%1 inflight=%2 focusFull=%3 sizeQ=%4 sizeR=%5")
                  .arg(q.pending).arg(q.inflight).arg(q.focusFullInflight)
                  .arg(q.sizeProbeQueued).arg(q.sizeProbeRunning);
    report << QStringLiteral("probe_fifo queued=%1 inflight=%2").arg(pq.queued).arg(pq.inflight);
    report << QStringLiteral("activity size=%1/%2 soft=%3/%4 tile=%5/%6 archiveR=%7")
                  .arg(act.sizeQueued).arg(act.sizeRunning)
                  .arg(act.softQueued).arg(act.softRunning)
                  .arg(act.tileQueued).arg(act.tileRunning)
                  .arg(act.archiveReadRunning);
    report << QStringLiteral("delta probe=%1 reval=%2 tile=%3 gallery=%4 tileLod=%5 pool=%6")
                  .arg(snap.delta.scheduleProbe).arg(snap.delta.scheduleRevalidate)
                  .arg(snap.delta.scheduleTile).arg(snap.delta.scheduleGalleryDecode)
                  .arg(snap.delta.tileLodTicks).arg(snap.delta.poolStarts);
    report << QStringLiteral("totals probe=%1 reval=%2 tile=%3 gallery=%4 tileLod=%5 poolStarts=%6")
                  .arg(snap.totals.scheduleProbe).arg(snap.totals.scheduleRevalidate)
                  .arg(snap.totals.scheduleTile).arg(snap.totals.scheduleGalleryDecode)
                  .arg(snap.totals.tileLodTicks).arg(snap.totals.poolStarts);
    report << QStringLiteral("--- intent ---");
    report << goals;
    if (!act.tileRunningLabels.isEmpty()) {
        report << QStringLiteral("tile_running=%1").arg(act.tileRunningLabels.join(QLatin1Char('|')));
    }
    m_lastReport = report.join(QLatin1Char('\n'));

    static qint64 s_lastMs = 0;
    for (const BackgroundWorkLog::Event &e : snap.recent) {
        if (e.msEpoch <= s_lastMs) {
            continue;
        }
        s_lastMs = e.msEpoch;
        const QString t = QDateTime::fromMSecsSinceEpoch(e.msEpoch).toString(
            QStringLiteral("hh:mm:ss.zzz"));
        QString line = t + QLatin1Char(' ') + e.kind;
        if (!e.detail.isEmpty()) {
            line += QLatin1Char(' ') + e.detail;
        }
        m_log->appendPlainText(line);
    }
}

void PerformancePanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_timer) {
        m_timer->start();
    }
    refresh();
}

void PerformancePanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_timer) {
        m_timer->stop();
    }
}
