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
#include <QSizePolicy>
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

    auto *root = new QVBoxLayout(this);
    // Grow with content; do not let the window shrink below the laid-out size
    // (word-wrapped labels were getting vertically squished at 480×320).
    root->setSizeConstraint(QLayout::SetMinimumSize);
    root->setSpacing(12);
    root->setContentsMargins(12, 12, 12, 12);

    auto *intro = new QLabel(
        tr("Generate zoom tiles for images in the current session so Gallery "
           "and Image zoom stay responsive. Small previews (LQIP) are filled "
           "automatically when tiles are built."),
        this);
    intro->setWordWrap(true);
    intro->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    root->addWidget(intro);

    auto *statsBox = new QGroupBox(tr("Cache status"), this);
    statsBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *statsLay = new QVBoxLayout(statsBox);
    m_statsLabel = new QLabel(tr("Scanning…"), statsBox);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setMinimumWidth(360);
    m_statsLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    statsLay->addWidget(m_statsLabel);
    root->addWidget(statsBox);

    auto *opts = new QGroupBox(tr("Options"), this);
    opts->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *form = new QFormLayout(opts);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_detailCombo = new QComboBox(opts);
    m_detailCombo->addItem(tr("Full detail (all zoom levels)"));
    m_detailCombo->addItem(tr("High detail"));
    m_detailCombo->addItem(tr("Medium detail"));
    m_detailCombo->addItem(tr("Overview only"));
    m_detailCombo->setCurrentIndex(0);
    m_detailCombo->setMinimumContentsLength(28);
    m_detailCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_detailCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_detailHint = new QLabel(
        tr("Full detail stores the finest tiles (more disk space). "
           "Coarser levels skip deep zoom but finish faster."),
        opts);
    m_detailHint->setWordWrap(true);
    m_detailHint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_detailHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(tr("Detail level:"), m_detailCombo);
    form->addRow(QString(), m_detailHint);
    root->addWidget(opts);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Ready."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    root->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(this);
    m_startBtn = buttons->addButton(tr("&Start"), QDialogButtonBox::ActionRole);
    m_cancelBtn = buttons->addButton(tr("Cancel"), QDialogButtonBox::ActionRole);
    m_closeBtn = buttons->addButton(QDialogButtonBox::Close);
    m_cancelBtn->setEnabled(false);
    buttons->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    root->addWidget(buttons);

    connect(m_startBtn, &QPushButton::clicked, this, &CachePrepareDialog::startPrepare);
    connect(m_cancelBtn, &QPushButton::clicked, this, &CachePrepareDialog::cancelPrepare);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setMinimumWidth(520);
    // Height from content, not a short fixed box that compresses labels.
    resize(560, qMax(400, sizeHint().height()));

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

void CachePrepareDialog::applyStatsLabel(const ThumtooCache::CacheCoverageStats &s)
{
    const int missingTiles = qMax(0, s.total - s.withTiles - s.unsupported);
    QString text = tr("%1 images in session
"
                      "%2 have durable tiles
"
                      "%3 have Store LQIP (ThumbHash)
"
                      "%4 have Store EMB (EXIF/container)
"
                      "%5 tiles but missing LQIP (will repair on Prepare)
"
                      "%6 still need tiles")
                       .arg(s.total)
                       .arg(s.withTiles)
                       .arg(s.withLqip)
                       .arg(s.withEmbedded)
                       .arg(s.tilesWithoutLqip)
                       .arg(missingTiles);
    if (s.unsupported > 0) {
        text += tr("
%1 unsupported").arg(s.unsupported);
    }
    m_statsLabel->setText(text);
}


void CachePrepareDialog::refreshStats()
{
    if (!ThumtooCache::isAvailable() || m_paths.isEmpty()) {
        return;
    }
    m_statsLabel->setText(tr("Scanning Store (tiles / LQIP / EMB)…"));
    const QStringList paths = m_paths;
    QPointer<CachePrepareDialog> self(this);
    QThreadPool::globalInstance()->start([self, paths]() {
        ASSERT_NOT_GUI_THREAD();
        const ThumtooCache::CacheCoverageStats s =
            ThumtooCache::scanCacheCoverage(paths);
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, s]() {
                if (self) {
                    self->applyStatsLabel(s);
                }
            },
            Qt::QueuedConnection);
    });
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
            [self](int done, int total, int ok, int skipped, int failed,
                   int lqipFilled) {
                if (!self) {
                    return;
                }
                QMetaObject::invokeMethod(
                    self,
                    [self, done, total, ok, skipped, failed, lqipFilled]() {
                        if (!self) {
                            return;
                        }
                        self->onProgress(done, total, ok, skipped, failed,
                                         lqipFilled);
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

void CachePrepareDialog::onProgress(int done, int total, int ok, int skipped,
                                      int failed, int lqipFilled)
{
    if (total > 0) {
        m_progress->setMaximum(total);
        m_progress->setValue(done);
    }
    m_statusLabel->setText(
        tr("Prepared %1 / %2 — pyramids ok %3, skipped (had tiles) %4, "
           "failed %5, LQIP filled this run %6")
            .arg(done)
            .arg(total)
            .arg(ok)
            .arg(skipped)
            .arg(failed)
            .arg(lqipFilled));
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
