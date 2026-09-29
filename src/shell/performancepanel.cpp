// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/performancepanel.h"

#include "host/thumtoocache.h"
#include "imageview.h"
#include "util/backgroundworklog.h"

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
    grid->addWidget(makeMetricCard(tr("Activity"), &m_activityValue), 1, 1);
    grid->addWidget(makeMetricCard(tr("Δ / 0.4s"), &m_deltaValue), 2, 0, 1, 2);
    root->addLayout(grid);

    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
    root->addWidget(m_detail);

    auto *btnRow = new QHBoxLayout;
    auto *clearBtn = new QPushButton(tr("Clear counters"), this);
    clearBtn->setFlat(true);
    connect(clearBtn, &QPushButton::clicked, this, &PerformancePanel::clearLog);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch(1);
    auto *hint = new QLabel(tr("Samples only while this panel is open"), this);
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

    int pendingDecode = 0;
    if (m_view) {
        pendingDecode = m_view->pendingDecodeCount();
    }

    const quint64 actTotal = act.sizeQueued + act.sizeRunning + act.softQueued + act.softRunning
        + act.tileQueued + act.tileRunning + act.archiveReadRunning;
    const bool focusHot = q.focusFullInflight > 1; // cap is 1 after thumtoo fix
    const bool queueBusy = q.pending > 0 || q.inflight > 0 || q.focusFullInflight > 0;
    const bool poolBusy = snap.qtPoolActive > 0;
    const bool deltaBusy = (snap.delta.poolStarts + snap.delta.scheduleProbe
                            + snap.delta.scheduleRevalidate + snap.delta.schedulePixels
                            + snap.delta.scheduleTile + snap.delta.scheduleGalleryDecode) > 0;
    const bool tickOnly = !deltaBusy && snap.delta.tileLodTicks > 0;

    QString badgeText = tr("Settled");
    QString badgeBg = QStringLiteral("#2a8");
    if (focusHot || (q.focusFullInflight > 0 && q.inflight > 20)) {
        badgeText = tr("Hot");
        badgeBg = QStringLiteral("#c44");
    } else if (queueBusy || poolBusy || actTotal > 0 || deltaBusy) {
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
                 tr("pending %1 · inflight %2")
                     .arg(q.pending)
                     .arg(q.inflight),
                 queueBusy ? (q.inflight > 16 ? QStringLiteral("hot") : QStringLiteral("busy"))
                          : QStringLiteral("ok"));

    setCardValue(m_focusValue,
                 tr("%1 in flight (cap 1)").arg(q.focusFullInflight),
                 focusHot ? QStringLiteral("hot")
                          : (q.focusFullInflight > 0 ? QStringLiteral("busy") : QStringLiteral("ok")));

    setCardValue(m_activityValue,
                 tr("size %1/%2  soft %3/%4  tile %5/%6")
                     .arg(act.sizeQueued)
                     .arg(act.sizeRunning)
                     .arg(act.softQueued)
                     .arg(act.softRunning)
                     .arg(act.tileQueued)
                     .arg(act.tileRunning),
                 actTotal > 0 ? QStringLiteral("busy") : QStringLiteral("ok"));

    setCardValue(m_deltaValue,
                 tr("probe %1 · reval %2 · tile %3 · gallery %4 · tileLod %5 · pool %6")
                     .arg(snap.delta.scheduleProbe)
                     .arg(snap.delta.scheduleRevalidate)
                     .arg(snap.delta.scheduleTile)
                     .arg(snap.delta.scheduleGalleryDecode)
                     .arg(snap.delta.tileLodTicks)
                     .arg(snap.delta.poolStarts),
                 deltaBusy ? QStringLiteral("busy")
                           : (tickOnly ? QStringLiteral("busy") : QStringLiteral("ok")));

    QStringList detail;
    if (!act.tileRunningLabels.isEmpty()) {
        detail << tr("Tiles: %1").arg(act.tileRunningLabels.mid(0, 3).join(QLatin1String(" · ")));
    }
    if (!act.sizeRunningUris.isEmpty()) {
        detail << tr("Size: %1").arg(act.sizeRunningUris.mid(0, 2).join(QLatin1String(" · ")));
    }
    if (pendingDecode > 0) {
        detail << tr("Host pendingDecode=%1").arg(pendingDecode);
    }
    detail << tr("Totals  probe %1  tile %2  gallery %3  tileLod %4  poolStarts %5")
                  .arg(snap.totals.scheduleProbe)
                  .arg(snap.totals.scheduleTile)
                  .arg(snap.totals.scheduleGalleryDecode)
                  .arg(snap.totals.tileLodTicks)
                  .arg(snap.totals.poolStarts);
    const QString breakdown = ThumtooCache::loadingBreakdownLabel();
    if (!breakdown.isEmpty()) {
        detail << tr("Loading: %1").arg(breakdown);
    }
    if (focusHot) {
        detail << tr("⚠ FocusFull > 1 — rebuild thumtoo with focus-full-no-busy-wait fix");
    }
    m_detail->setText(detail.join(QLatin1Char('\n')));

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
