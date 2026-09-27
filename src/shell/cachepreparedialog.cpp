// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/cachepreparedialog.h"
#include "host/thumtoocache.h"
#include "util/biltoo_thread.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThreadPool>
#include <QVBoxLayout>

namespace {

/** User-facing detail → thumtoo min_scale (0 = full-res tiles). */
int minScaleForDetailIndex(int index)
{
    // Higher min_scale = coarser pyramid only (less disk, weaker deep zoom).
    // Scale s means each tile cell covers 2^s source pixels on a side.
    switch (index) {
    case 0:
        return 0; // Full: 1:1
    case 1:
        return 1; // High: 2×2
    case 2:
        return 2; // Medium: 4×4
    case 3:
        return 3; // Overview: 8×8
    default:
        return 0;
    }
}

QString shortName(const QString &path)
{
    // Prefer archive member leaf; else file name.
    const int arch = path.lastIndexOf(QStringLiteral("//archive:"));
    if (arch >= 0) {
        return QFileInfo(path.mid(arch + 10)).fileName();
    }
    return QFileInfo(path).fileName();
}

QString yesNo(bool on)
{
    return on ? QStringLiteral("yes") : QStringLiteral("—");
}

QString tileScaleText(int minScale)
{
    if (minScale < 0) {
        return QStringLiteral("—");
    }
    // min_scale is finest stored level (0 = full res).
    return QStringLiteral("s≥%1").arg(minScale);
}

} // namespace

CachePrepareDialog::CachePrepareDialog(const QStringList &sessionPaths, QWidget *parent)
    : QDialog(parent)
    , m_paths(sessionPaths)
{
    setWindowTitle(tr("Prepare tile cache"));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetMinimumSize);
    root->setSpacing(12);
    root->setContentsMargins(12, 12, 12, 12);

    auto *intro = new QLabel(
        tr("<p><b>What this does</b></p>"
           "<p>Builds durable <b>256×256 zoom tiles</b> in the thumtoo Store so Gallery "
           "and Image mode stay sharp without re-decoding the whole file. "
           "<b>LQIP</b> (tiny ThumbHash placeholder) is written from free tile data "
           "when missing — including for images that already have tiles but no LQIP "
           "(e.g. after a crash mid-prepare).</p>"
           "<p>It does <b>not</b> open files only to make LQIP; tiles must exist or be "
           "generated first.</p>"),
        this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    root->addWidget(intro);

    auto *statsBox = new QGroupBox(tr("Session cache status"), this);
    auto *statsLay = new QVBoxLayout(statsBox);
    m_statsLabel = new QLabel(tr("Scanning…"), statsBox);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setMinimumWidth(420);
    statsLay->addWidget(m_statsLabel);

    m_table = new QTableWidget(0, 5, statsBox);
    m_table->setHorizontalHeaderLabels(
        {tr("Image"), tr("Tiles"), tr("Finest"), tr("LQIP"), tr("EMB")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setMinimumHeight(160);
    m_table->setToolTip(
        tr("Tiles = durable 256² pyramid in Store.\n"
           "Finest = coarsest-allowed scale stored (s=0 full res, higher = coarser).\n"
           "LQIP = ThumbHash/Handsum placeholder in Store.\n"
           "EMB = EXIF/container JPEG thumb in Store (optional)."));
    statsLay->addWidget(m_table);
    root->addWidget(statsBox);

    auto *opts = new QGroupBox(tr("How much detail to store"), this);
    auto *form = new QFormLayout(opts);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_detailCombo = new QComboBox(opts);
    m_detailCombo->addItem(tr("Full detail (scale 0 — 1:1)"));
    m_detailCombo->addItem(tr("High detail (scale 1 — 2× coarser)"));
    m_detailCombo->addItem(tr("Medium detail (scale 2 — 4× coarser)"));
    m_detailCombo->addItem(tr("Overview only (scale 3 — 8× coarser)"));
    m_detailCombo->setCurrentIndex(0);
    m_detailCombo->setMinimumContentsLength(36);
    m_detailCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_detailHint = new QLabel(opts);
    m_detailHint->setWordWrap(true);
    m_detailHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(tr("Detail level:"), m_detailCombo);
    form->addRow(QString(), m_detailHint);
    root->addWidget(opts);
    connect(m_detailCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CachePrepareDialog::onDetailIndexChanged);
    updateDetailHint(0);

    m_progress = new QProgressBar(this);
    m_progress->setMinimum(0);
    m_progress->setMaximum(1);
    m_progress->setValue(0);
    root->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Ready."), this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(this);
    m_startBtn = buttons->addButton(tr("Prepare"), QDialogButtonBox::AcceptRole);
    m_cancelBtn = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    m_closeBtn = buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);
    m_cancelBtn->setEnabled(false);
    root->addWidget(buttons);

    connect(m_startBtn, &QPushButton::clicked, this, &CachePrepareDialog::startPrepare);
    connect(m_cancelBtn, &QPushButton::clicked, this, &CachePrepareDialog::cancelPrepare);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setMinimumWidth(640);
    resize(720, qMax(520, sizeHint().height()));

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

void CachePrepareDialog::onDetailIndexChanged(int index)
{
    updateDetailHint(index);
}

void CachePrepareDialog::updateDetailHint(int index)
{
    switch (index) {
    case 0:
        m_detailHint->setText(
            tr("Stores the full pyramid down to 1:1 (scale 0). Best for deep Image zoom. "
               "Uses the most disk. Also fills missing LQIP from tiles."));
        break;
    case 1:
        m_detailHint->setText(
            tr("Stops at scale 1 (each tile covers a 2×2 source block). Good for most "
               "viewing; less disk than Full. Missing LQIP is still repaired."));
        break;
    case 2:
        m_detailHint->setText(
            tr("Stops at scale 2 (4× coarser). Enough for Gallery and moderate zoom. "
               "Missing LQIP is still repaired."));
        break;
    case 3:
        m_detailHint->setText(
            tr("Overview only (scale 3 — 8× coarser). Smallest cache: Gallery-sized "
               "cells and LQIP-from-tiles. Not enough for deep Image zoom. "
               "If tiles already exist at a finer level, this run skips encoding "
               "but still tries to fill missing LQIP."));
        break;
    default:
        m_detailHint->clear();
        break;
    }
}

void CachePrepareDialog::applyStats(const ThumtooCache::CacheCoverageStats &s)
{
    const int missingTiles = qMax(0, s.total - s.withTiles - s.unsupported);
    QString text =
        tr("%1 images in session\n"
           "%2 have durable tiles\n"
           "%3 have Store LQIP (ThumbHash)\n"
           "%4 have Store EMB (EXIF/container)\n"
           "%5 have tiles but missing LQIP (Prepare will repair)\n"
           "%6 still need tiles")
            .arg(s.total)
            .arg(s.withTiles)
            .arg(s.withLqip)
            .arg(s.withEmbedded)
            .arg(s.tilesWithoutLqip)
            .arg(missingTiles);
    if (s.unsupported > 0) {
        text += tr("\n%1 unsupported").arg(s.unsupported);
    }
    m_statsLabel->setText(text);

    if (!m_table) {
        return;
    }
    m_table->setRowCount(0);
    m_table->setRowCount(s.rows.size());
    for (int i = 0; i < s.rows.size(); ++i) {
        const ThumtooCache::PathCacheCoverage &r = s.rows.at(i);
        auto *nameItem = new QTableWidgetItem(shortName(r.path));
        nameItem->setToolTip(r.path);
        m_table->setItem(i, 0, nameItem);
        if (r.unsupported) {
            m_table->setItem(i, 1, new QTableWidgetItem(tr("unsupported")));
            m_table->setItem(i, 2, new QTableWidgetItem(QStringLiteral("—")));
            m_table->setItem(i, 3, new QTableWidgetItem(QStringLiteral("—")));
            m_table->setItem(i, 4, new QTableWidgetItem(QStringLiteral("—")));
            continue;
        }
        m_table->setItem(i, 1, new QTableWidgetItem(yesNo(r.hasTiles)));
        m_table->setItem(i, 2, new QTableWidgetItem(tileScaleText(r.tileMinScale)));
        m_table->setItem(i, 3, new QTableWidgetItem(yesNo(r.hasLqip)));
        m_table->setItem(i, 4, new QTableWidgetItem(yesNo(r.hasEmbedded)));
    }
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
                    self->applyStats(s);
                }
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
    if (m_table) {
        m_table->setEnabled(!busy);
    }
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
        tr("Prepared %1 / %2 — new pyramids %3, already had tiles %4, "
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
        m_statusLabel->setText(tr("Cancelled — refreshing status…"));
    } else {
        m_statusLabel->setText(tr("Finished — refreshing status…"));
    }
    refreshStats();
}
