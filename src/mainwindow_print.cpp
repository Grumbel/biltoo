// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "imageview.h"

#include <QFileDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QGuiApplication>
#include <QPainter>
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>
#include <QSettings>
#include <QScreen>
#include <QStatusBar>
#include <QMessageBox>

namespace {

constexpr auto kPrintGroup = "print";

void loadPrintSettings(QPrinter *printer)
{
    if (!printer) {
        return;
    }
    QSettings settings;
    settings.beginGroup(QLatin1String(kPrintGroup));

    QPageLayout layout = printer->pageLayout();

    const int sizeId = settings.value(QStringLiteral("pageSizeId"), -1).toInt();
    if (sizeId >= 0) {
        const QPageSize pageSize(static_cast<QPageSize::PageSizeId>(sizeId));
        if (pageSize.isValid()) {
            layout.setPageSize(pageSize);
        }
    } else {
        const QSizeF customMm = settings.value(QStringLiteral("pageSizeMm")).toSizeF();
        if (customMm.width() > 1.0 && customMm.height() > 1.0) {
            layout.setPageSize(QPageSize(customMm, QPageSize::Millimeter));
        }
    }

    const int orient = settings.value(QStringLiteral("orientation"), -1).toInt();
    if (orient == static_cast<int>(QPageLayout::Landscape)
        || orient == static_cast<int>(QPageLayout::Portrait)) {
        layout.setOrientation(static_cast<QPageLayout::Orientation>(orient));
    }

    // Whole sheet: matches Workspace page guide 1:1.
    layout.setMode(QPageLayout::FullPageMode);
    printer->setPageLayout(layout);
    printer->setFullPage(true);

    settings.endGroup();
}

void savePrintSettings(const QPrinter *printer)
{
    if (!printer) {
        return;
    }
    const QPageLayout layout = printer->pageLayout();
    QSettings settings;
    settings.beginGroup(QLatin1String(kPrintGroup));

    const QPageSize ps = layout.pageSize();
    if (ps.id() != QPageSize::Custom) {
        settings.setValue(QStringLiteral("pageSizeId"), static_cast<int>(ps.id()));
        settings.remove(QStringLiteral("pageSizeMm"));
    } else {
        settings.setValue(QStringLiteral("pageSizeId"), -1);
        settings.setValue(QStringLiteral("pageSizeMm"),
                          ps.size(QPageSize::Millimeter));
    }
    settings.setValue(QStringLiteral("orientation"),
                      static_cast<int>(layout.orientation()));
    settings.endGroup();
}

void preparePrinter(QPrinter *printer)
{
    loadPrintSettings(printer);
    QPageLayout layout = printer->pageLayout();
    layout.setMode(QPageLayout::FullPageMode);
    printer->setPageLayout(layout);
    printer->setFullPage(true);
}

void syncPageGuide(ImageView *view, QPrinter *printer)
{
    if (!view || !printer) {
        return;
    }
    view->setPageGuideFromPrinter(*printer);
}

void renderViewToPrinter(ImageView *view, QPrinter *printer)
{
    if (!view || !printer) {
        return;
    }
    QPageLayout layout = printer->pageLayout();
    layout.setMode(QPageLayout::FullPageMode);
    printer->setPageLayout(layout);
    printer->setFullPage(true);

    QPainter painter(printer);
    if (!painter.isActive()) {
        return;
    }

    QRectF page = printer->pageRect(QPrinter::DevicePixel);
    if (!page.isValid() || page.width() <= 0 || page.height() <= 0) {
        const QRect pr = printer->pageLayout().fullRectPixels(printer->resolution());
        page = QRectF(pr);
    }
    if (!page.isValid() || page.width() <= 0 || page.height() <= 0) {
        return;
    }
    view->renderForPrint(&painter, page);
}

} // namespace

void MainWindow::pageSetup()
{
    QPrinter printer(QPrinter::HighResolution);
    preparePrinter(&printer);

    QPageSetupDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Page Setup"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    savePrintSettings(&printer);
    preparePrinter(&printer);
    if (m_imageView) {
        syncPageGuide(m_imageView, &printer);
    }
}

void MainWindow::printDocument()
{
    if (!m_imageView) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    preparePrinter(&printer);
    syncPageGuide(m_imageView, &printer);

    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print"));
    // Page size for physical devices is often forced by the driver/tray.
    // App paper size is still applied; use Export PDF for a guaranteed size.
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // Keep *our* page size as the document layout even if the dialog changed
    // the destination device — re-apply saved setup, then only honour
    // destination-related changes from the dialog by saving after.
    savePrintSettings(&printer);
    preparePrinter(&printer);
    syncPageGuide(m_imageView, &printer);
    renderViewToPrinter(m_imageView, &printer);
}

void MainWindow::printPreview()
{
    if (!m_imageView) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    preparePrinter(&printer);
    syncPageGuide(m_imageView, &printer);

    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(tr("Print Preview"));
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QSize avail = screen->availableGeometry().size();
        preview.resize(qMax(900, avail.width() * 3 / 4),
                       qMax(700, avail.height() * 3 / 4));
    } else {
        preview.resize(1000, 750);
    }
    connect(&preview, &QPrintPreviewDialog::paintRequested, this,
            [this](QPrinter *p) {
                if (!m_imageView || !p) {
                    return;
                }
                // Preview may edit layout; treat that as the new app page setup.
                QPageLayout layout = p->pageLayout();
                layout.setMode(QPageLayout::FullPageMode);
                p->setPageLayout(layout);
                p->setFullPage(true);
                savePrintSettings(p);
                syncPageGuide(m_imageView, p);
                renderViewToPrinter(m_imageView, p);
            });
    preview.exec();
    savePrintSettings(&printer);
    syncPageGuide(m_imageView, &printer);
}

void MainWindow::exportPdf()
{
    if (!m_imageView) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export PDF"),
        QString(),
        tr("PDF files (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    preparePrinter(&printer);
    syncPageGuide(m_imageView, &printer);
    renderViewToPrinter(m_imageView, &printer);

    if (statusBar()) {
        statusBar()->showMessage(tr("Exported PDF: %1").arg(path), 5000);
    }
}

void MainWindow::togglePageGuide()
{
    if (!m_imageView || !m_pageGuideAct) {
        return;
    }
    const bool on = m_pageGuideAct->isChecked();
    if (on) {
        QPrinter printer(QPrinter::HighResolution);
        preparePrinter(&printer);
        syncPageGuide(m_imageView, &printer);
    }
    m_imageView->setPageGuideVisible(on);
}


void MainWindow::exportPng()
{
    if (!m_imageView) {
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Export PNG"));
    auto *layout = new QFormLayout(&dlg);

    auto *boundsCombo = new QComboBox(&dlg);
    boundsCombo->addItem(tr("Content (tight)"), 0);
    boundsCombo->addItem(tr("Page guide"), 1);
    // Default: content for ad-hoc Workspace; page guide if user turned it on.
    if (m_imageView->isWorkspaceMode() && m_imageView->pageGuideVisible()) {
        boundsCombo->setCurrentIndex(1);
    }
    layout->addRow(tr("Region:"), boundsCombo);

    auto *widthSpin = new QSpinBox(&dlg);
    widthSpin->setRange(16, 16384);
    widthSpin->setSingleStep(64);
    widthSpin->setValue(2048);
    widthSpin->setSuffix(tr(" px"));
    layout->addRow(tr("Width:"), widthSpin);

    auto *heightLabel = new QLabel(&dlg);
    layout->addRow(tr("Height:"), heightLabel);

    auto *transparentCheck = new QCheckBox(tr("Transparent background"), &dlg);
    transparentCheck->setChecked(true);
    layout->addRow(QString(), transparentCheck);

    auto updateHeight = [&]() {
        QRectF source;
        if (boundsCombo->currentData().toInt() == 1) {
            source = m_imageView->pageGuideSceneRect();
        } else {
            source = m_imageView->contentExportBounds();
        }
        if (!source.isValid() || source.height() < 1e-3 || source.width() < 1e-3) {
            heightLabel->setText(tr("—"));
            return;
        }
        const int w = widthSpin->value();
        const int h = qMax(1, qRound(w * (source.height() / source.width())));
        heightLabel->setText(tr("%1 px").arg(h));
    };
    QObject::connect(widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, updateHeight);
    QObject::connect(boundsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg,
                     updateHeight);
    updateHeight();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QRectF source;
    if (boundsCombo->currentData().toInt() == 1) {
        source = m_imageView->pageGuideSceneRect();
    } else {
        source = m_imageView->contentExportBounds();
    }
    if (!source.isValid() || source.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Nothing to export."), 4000);
        }
        return;
    }

    const int w = widthSpin->value();
    const int h = qMax(1, qRound(w * (source.height() / source.width())));
    const QImage img = m_imageView->renderExportImage(QSize(w, h), source,
                                                      transparentCheck->isChecked());
    if (img.isNull()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Export failed."), 4000);
        }
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PNG"), QString(), tr("PNG images (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    QString out = path;
    if (!out.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) {
        out += QStringLiteral(".png");
    }
    if (!img.save(out, "PNG")) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Could not write %1").arg(out), 5000);
        }
        return;
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Exported PNG: %1 (%2×%3)").arg(out).arg(w).arg(h), 5000);
    }
}


void MainWindow::fitPageGuideToContent()
{
    if (!m_imageView) {
        return;
    }
    if (!isWorkspaceMode()) {
        enterWorkspaceMode();
    }
    m_imageView->fitPageGuideToContent(16.0);
    if (m_pageGuideAct) {
        m_pageGuideAct->setChecked(true);
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Page guide fitted to content."), 3000);
    }
}
