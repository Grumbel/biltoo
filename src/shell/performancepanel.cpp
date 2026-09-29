// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/performancepanel.h"

#include "host/thumtoocache.h"
#include "imageview.h"
#include "util/backgroundworklog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QHideEvent>

PerformancePanel::PerformancePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    m_summary = new QLabel(tr("Open this panel to sample background work."), this);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_summary);

    auto *btnRow = new QHBoxLayout;
    auto *clearBtn = new QPushButton(tr("Clear counters"), this);
    connect(clearBtn, &QPushButton::clicked, this, &PerformancePanel::clearLog);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(200);
    m_log->setPlaceholderText(tr("Recent schedule events appear here while the panel is open."));
    root->addWidget(m_log, 1);

    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &PerformancePanel::refresh);
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

    QStringList lines;
    lines << tr("Qt pool: %1 active / %2 max")
                 .arg(snap.qtPoolActive)
                 .arg(snap.qtPoolMax);
    lines << tr("Thumtoo queue: pending=%1 inflight=%2 focusFull=%3 sizeQ=%4 sizeR=%5")
                 .arg(q.pending)
                 .arg(q.inflight)
                 .arg(q.focusFullInflight)
                 .arg(q.sizeProbeQueued)
                 .arg(q.sizeProbeRunning);
    lines << tr("Activity: size %1q/%2r  soft %3q/%4r  tile %5q/%6r  archiveR=%7")
                 .arg(act.sizeQueued)
                 .arg(act.sizeRunning)
                 .arg(act.softQueued)
                 .arg(act.softRunning)
                 .arg(act.tileQueued)
                 .arg(act.tileRunning)
                 .arg(act.archiveReadRunning);
    if (!act.sizeRunningUris.isEmpty()) {
        lines << tr("Size running: %1").arg(act.sizeRunningUris.join(QLatin1String(", ")));
    }
    if (!act.tileRunningLabels.isEmpty()) {
        lines << tr("Tile running: %1").arg(act.tileRunningLabels.mid(0, 4).join(QLatin1String(", ")));
    }
    lines << tr("Host pendingDecode=%1").arg(pendingDecode);
    lines << tr("Δ0.4s poolStarts=%1 probe=%2 reval=%3 pixels=%4 tile=%5 gallery=%6 tileLodTick=%7")
                 .arg(snap.delta.poolStarts)
                 .arg(snap.delta.scheduleProbe)
                 .arg(snap.delta.scheduleRevalidate)
                 .arg(snap.delta.schedulePixels)
                 .arg(snap.delta.scheduleTile)
                 .arg(snap.delta.scheduleGalleryDecode)
                 .arg(snap.delta.tileLodTicks);
    lines << tr("Totals poolStarts=%1 probe=%2 reval=%3 pixels=%4 tile=%5 gallery=%6 tileLod=%7")
                 .arg(snap.totals.poolStarts)
                 .arg(snap.totals.scheduleProbe)
                 .arg(snap.totals.scheduleRevalidate)
                 .arg(snap.totals.schedulePixels)
                 .arg(snap.totals.scheduleTile)
                 .arg(snap.totals.scheduleGalleryDecode)
                 .arg(snap.totals.tileLodTicks);

    const QString breakdown = ThumtooCache::loadingBreakdownLabel();
    if (!breakdown.isEmpty()) {
        lines << tr("Loading: %1").arg(breakdown);
    }

    m_summary->setText(lines.join(QLatin1Char('\n')));

    // Append only new events (ring may rotate).
    static qint64 s_lastMs = 0;
    for (const BackgroundWorkLog::Event &e : snap.recent) {
        if (e.msEpoch <= s_lastMs) {
            continue;
        }
        s_lastMs = e.msEpoch;
        const QString t = QDateTime::fromMSecsSinceEpoch(e.msEpoch).toString(QStringLiteral("hh:mm:ss.zzz"));
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
