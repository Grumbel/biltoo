// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/cachepreparedialog.h"
#include "host/thumtoocache.h"
#include "util/biltoo_thread.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QThreadPool>
#include <QVBoxLayout>

namespace {

/** User-facing detail → thumtoo min_scale (0 = full res tiles). */
int minScaleForDetailIndex(int index)
{
    // Higher min_scale = coarser pyramid only (less disk, weaker deep zoom).
    switch (index) {
    case 0:
        return 0; // Full detail
    case 1:
        return 1; // High
    case 2:
        return 2; // Medium
    case 3:
        return 3; // Overview
    default:
        return 0;
    }
}

} // namespace

CachePrepareDialog::CachePrepareDialog(const QStringList &sessionPaths, QWidget *parent)
    : QDialog(parent)
    , m_paths(sessionPaths)
{
    setWindowTitle(tr("Prepare tile cache"));
    setModal(true);
    resize(480, 320);

    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Generate zoom tiles for images in the current session so Gallery "
           "and Image zoom stay responsive. Small previews (LQIP) are filled "
           "automatically when tiles are built."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *statsBox = new QGroupBox(tr("Cache status"), this);
    auto *statsLay = new QVBoxLayout(statsBox);
    m_statsLabel = new QLabel(tr("Scanning…"), statsBox);
    m_statsLabel->setWordWrap(true);
    statsLay->addWidget(m_statsLabel);
    root->addWidget(statsBox);

    auto *opts = new QGroupBox(tr("Options"), this);
    auto *form = new QFormLayout(opts);
    m_detailCombo = new QComboBox(opts);
    m_detailCombo->addItem(tr("Full detail (all zoom levels)"));
    m_detailCombo->addItem(tr("High detail"));
    m_detailCombo->addItem(tr("Medium detail"));
    m_detailCombo->addItem(tr("Overview only"));
    m_detailCombo->setCurrentIndex(0);
    m_detailHint = new QLabel(
        tr("Full detail stores the finest tiles (more disk space). "
           "Coarser levels skip deep zoom but finish faster."),
        opts);
    m_detailHint->setWordWrap(true);
    m_detailHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(tr("Detail level:"), m_detailCombo);
    form->addRow(QString(), m_detailHint);
    root->addWidget(opts);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    root->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Ready."), this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(this);
    m_startBtn = buttons->addButton(tr("&Start"), QDialogButtonBox::ActionRole);
    m_cancelBtn = buttons->addButton(tr("Cancel"), QDialogButtonBox::ActionRole);
    m_closeBtn = buttons->addButton(QDialogButtonBox::Close);
    m_cancelBtn->setEnabled(false);
    root->addWidget(buttons);

    connect(m_startBtn, &QPushButton::clicked, this, &CachePrepareDialog::startPrepare);
    connect(m_cancelBtn, &QPushButton::clicked, this, &CachePrepareDialog::cancelPrepare);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (!ThumtooCache::isAvailable()) {
        m_statsLabel->setText(tr("Tile cache is not available in this build."));
        m_startBtn->setEnabled(false);
        m_statusLabel->setText(tr("thumtoo is required for durable tiles."));
        return;
    }
    if (m_paths.isEmpty()) {
        m_statsLabel->setText(tr("No images in the session."));
        m_startBtn->setEnabled(false);
        return;
    }

    refreshStats();
}

CachePrepareDialog::~CachePrepareDialog()
{
    m_cancel.store(true);
}

void CachePrepareDialog::reject()
{
    if (m_running) {
        cancelPrepare();
        return;
    }
    QDialog::reject();
}

void CachePrepareDialog::closeEvent(QCloseEvent *event)
{
    if (m_running) {
        cancelPrepare();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void CachePrepareDialog::applyStatsLabel(int total, int withTiles, int missing, int unsupported)
{
    m_statsLabel->setText(
        tr("%1 images in session\n"
           "%2 already have tiles\n"
           "%3 still need tiles%4")
            .arg(total)
            .arg(withTiles)
            .arg(missing)
            .arg(unsupported > 0 ? tr("\n%1 unsupported").arg(unsupported) : QString()));
    if (missing == 0 && total > 0 && !m_running) {
        m_statusLabel->setText(
            tr("All session images already have tiles. "
               "Start again to fill a finer detail level if needed."));
    }
}

void CachePrepareDialog::refreshStats()
{
    if (!ThumtooCache::isAvailable() || m_paths.isEmpty()) {
        return;
    }
    m_statsLabel->setText(tr("Scanning…"));
    const QStringList paths = m_paths;
    QPointer<CachePrepareDialog> self(this);
    QThreadPool::globalInstance()->start([self, paths]() {
        ASSERT_NOT_GUI_THREAD();
        const ThumtooCache::TilePrepareStats st =
            ThumtooCache::queryTilePrepareStats(paths);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, st]() {
                if (!self) {
                    return;
                }
                self->applyStatsLabel(st.total, st.withTiles, st.missingTiles,
                                      st.unsupported);
            },
            Qt::QueuedConnection);
    });
}

void CachePrepareDialog::setBusy(bool busy)
{
    m_running = busy;
    m_startBtn->setEnabled(!busy);
    m_cancelBtn->setEnabled(busy);
    m_closeBtn->setEnabled(!busy);
    m_detailCombo->setEnabled(!busy);
}

void CachePrepareDialog::startPrepare()
{
    if (m_running || m_paths.isEmpty() || !ThumtooCache::isAvailable()) {
        return;
    }
    m_cancel.store(false);
    setBusy(true);
    const int minScale = minScaleForDetailIndex(m_detailCombo->currentIndex());
    m_progress->setRange(0, qMax(1, m_paths.size()));
    m_progress->setValue(0);
    m_statusLabel->setText(tr("Starting…"));

    const QStringList paths = m_paths;
    std::atomic<bool> *cancel = &m_cancel;
    QPointer<CachePrepareDialog> self(this);
    QThreadPool::globalInstance()->start([self, paths, minScale, cancel]() {
        ASSERT_NOT_GUI_THREAD();
        ThumtooCache::prepareTiles(
            paths, minScale,
            [self](int done, int total, int ok, int skipped, int failed) {
                if (!self) {
                    return;
                }
                QMetaObject::invokeMethod(
                    self,
                    [self, done, total, ok, skipped, failed]() {
                        if (!self) {
                            return;
                        }
                        self->onProgress(done, total, ok, skipped, failed);
                    },
                    Qt::QueuedConnection);
            },
            cancel);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self]() {
                if (self) {
                    self->onFinished();
                }
            },
            Qt::QueuedConnection);
    });
}

void CachePrepareDialog::cancelPrepare()
{
    if (!m_running) {
        return;
    }
    m_cancel.store(true);
    m_statusLabel->setText(tr("Cancelling…"));
    m_cancelBtn->setEnabled(false);
}

void CachePrepareDialog::onProgress(int done, int total, int ok, int skipped, int failed)
{
    if (total > 0) {
        m_progress->setRange(0, total);
        m_progress->setValue(qBound(0, done, total));
    }
    m_statusLabel->setText(
        tr("Progress: %1 / %2  —  built %3, already cached %4, failed %5")
            .arg(done)
            .arg(total)
            .arg(ok)
            .arg(skipped)
            .arg(failed));
}

void CachePrepareDialog::onFinished()
{
    setBusy(false);
    if (m_cancel.load()) {
        m_statusLabel->setText(tr("Cancelled."));
    } else {
        m_statusLabel->setText(tr("Finished."));
    }
    refreshStats();
}
