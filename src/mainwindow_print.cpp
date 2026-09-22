// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"
#include "mainwindow.h"
#include "imageview.h"
#include "sessionexport.h"

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

#include <QThreadPool>
#include <QTimer>
#include <QRadioButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>

#include <algorithm>

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
    if (m_imageView->isWorkspaceMode() && m_imageView->hostPageGuide().isVisible()) {
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
    // Prefer the visible Workspace/canvas background unless the user opts out.
    bool defaultTransparent = true;
    if (m_imageView->isWorkspaceMode()
        && !m_imageView->hostCanvasBg().workspaceRef().isAppDefault()) {
        defaultTransparent = false;
    }
    transparentCheck->setChecked(defaultTransparent);
    transparentCheck->setToolTip(
        tr("When unchecked, the Workspace (or app) canvas background is included"));
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
        const int h = ContentXform::heightForAspectWidth(w, source.width(), source.height());
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
    const int h = ContentXform::heightForAspectWidth(w, source.width(), source.height());
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

void MainWindow::updateFileExportActions()
{
    const bool hasSession = m_session.size() > 0;
    const bool workspace = isWorkspaceMode();
    const bool gallery = isGalleryMode();
    const bool image = isImageMode();

    if (m_pageGuideAct) {
        m_pageGuideAct->setEnabled(workspace);
        m_pageGuideAct->setVisible(workspace);
    }
    if (m_exportPngAct) {
        // Page/sheet export: Workspace primary (also allow Image single-frame later).
        m_exportPngAct->setEnabled(workspace);
        m_exportPngAct->setVisible(workspace);
    }
    if (m_exportPdfAct) {
        m_exportPdfAct->setEnabled(workspace);
        m_exportPdfAct->setVisible(workspace);
    }
    if (m_pageSetupAct) {
        m_pageSetupAct->setEnabled(workspace);
        m_pageSetupAct->setVisible(workspace);
    }
    if (m_exportSessionImagesAct) {
        m_exportSessionImagesAct->setEnabled(hasSession && (gallery || image || workspace));
        m_exportSessionImagesAct->setVisible(true);
    }
    // Print still useful in Image (current) and Workspace (page).
    if (m_printAct) {
        m_printAct->setEnabled(hasSession || workspace);
    }
    if (m_printPreviewAct) {
        m_printPreviewAct->setEnabled(hasSession || workspace);
    }
}

void MainWindow::exportSessionImages()
{
    if (m_session.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("No images in the session to export."), 4000);
        }
        return;
    }
    if (!m_imageView) {
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Export Images"));
    auto *layout = new QFormLayout(&dlg);

    auto *scopeAll = new QRadioButton(tr("Entire session (%n image(s))", "", m_session.size()), &dlg);
    auto *scopeSel = new QRadioButton(tr("Selection only"), &dlg);
    scopeAll->setChecked(true);
    QList<int> selectedRows;
    if (m_thumbnailBar) {
        selectedRows = m_thumbnailBar->selectedIndices();
    }
    if (selectedRows.isEmpty()) {
        scopeSel->setEnabled(false);
    }
    auto *scopeBox = new QGroupBox(tr("Scope"), &dlg);
    auto *scopeLay = new QVBoxLayout(scopeBox);
    scopeLay->addWidget(scopeAll);
    scopeLay->addWidget(scopeSel);
    layout->addRow(scopeBox);

    auto *containerCombo = new QComboBox(&dlg);
    containerCombo->addItem(tr("Folder of images"), static_cast<int>(SessionExport::Container::Directory));
    containerCombo->addItem(tr("Comic book archive (.cbz)"), static_cast<int>(SessionExport::Container::Cbz));
    containerCombo->addItem(tr("Multi-page PDF"), static_cast<int>(SessionExport::Container::Pdf));
    layout->addRow(tr("Output"), containerCombo);

    auto *formatCombo = new QComboBox(&dlg);
    formatCombo->addItem(tr("JPEG"), static_cast<int>(SessionExport::Format::Jpeg));
    formatCombo->addItem(tr("PNG"), static_cast<int>(SessionExport::Format::Png));
    layout->addRow(tr("Image format"), formatCombo);

    auto *qualitySpin = new QSpinBox(&dlg);
    qualitySpin->setRange(1, 100);
    qualitySpin->setValue(90);
    qualitySpin->setSuffix(tr("%"));
    layout->addRow(tr("JPEG quality"), qualitySpin);

    auto *edgeSpin = new QSpinBox(&dlg);
    edgeSpin->setRange(0, 16384);
    edgeSpin->setValue(0);
    edgeSpin->setSpecialValueText(tr("Native (after bake)"));
    edgeSpin->setToolTip(tr("0 = full resolution after rotate/flip/crop. "
                            "Otherwise long edge is limited."));
    layout->addRow(tr("Max long edge"), edgeSpin);

    auto *destEdit = new QLineEdit(&dlg);
    auto *browseBtn = new QPushButton(tr("Browse…"), &dlg);
    auto *destRow = new QHBoxLayout;
    destRow->addWidget(destEdit);
    destRow->addWidget(browseBtn);
    layout->addRow(tr("Destination"), destRow);

    auto updateDestFilter = [&]() {
        const auto c = static_cast<SessionExport::Container>(
            containerCombo->currentData().toInt());
        formatCombo->setEnabled(c != SessionExport::Container::Pdf);
        qualitySpin->setEnabled(
            c != SessionExport::Container::Pdf
            && formatCombo->currentData().toInt()
                == static_cast<int>(SessionExport::Format::Jpeg));
    };
    QObject::connect(containerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &dlg, updateDestFilter);
    QObject::connect(formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &dlg, updateDestFilter);
    updateDestFilter();

    QObject::connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
        const auto c = static_cast<SessionExport::Container>(
            containerCombo->currentData().toInt());
        if (c == SessionExport::Container::Directory) {
            const QString d = QFileDialog::getExistingDirectory(
                &dlg, tr("Export Folder"), destEdit->text());
            if (!d.isEmpty()) {
                destEdit->setText(d);
            }
        } else if (c == SessionExport::Container::Cbz) {
            const QString f = QFileDialog::getSaveFileName(
                &dlg, tr("Export CBZ"), destEdit->text(),
                tr("Comic book (*.cbz)"));
            if (!f.isEmpty()) {
                destEdit->setText(f);
            }
        } else {
            const QString f = QFileDialog::getSaveFileName(
                &dlg, tr("Export PDF"), destEdit->text(),
                tr("PDF (*.pdf)"));
            if (!f.isEmpty()) {
                destEdit->setText(f);
            }
        }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QString dest = destEdit->text().trimmed();
    if (dest.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("No destination chosen."), 4000);
        }
        return;
    }

    SessionExport::Options opt;
    opt.container = static_cast<SessionExport::Container>(
        containerCombo->currentData().toInt());
    opt.format = static_cast<SessionExport::Format>(formatCombo->currentData().toInt());
    opt.jpegQuality = qualitySpin->value();
    opt.maxLongEdge = edgeSpin->value();
    opt.destPath = dest;
    if (opt.container == SessionExport::Container::Cbz
        && !opt.destPath.endsWith(QLatin1String(".cbz"), Qt::CaseInsensitive)) {
        opt.destPath += QStringLiteral(".cbz");
    }
    if (opt.container == SessionExport::Container::Pdf
        && !opt.destPath.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
        opt.destPath += QStringLiteral(".pdf");
    }

    QList<int> indices;
    if (scopeSel->isChecked() && !selectedRows.isEmpty()) {
        indices = selectedRows;
        std::sort(indices.begin(), indices.end());
    } else {
        for (int i = 0; i < m_session.size(); ++i) {
            indices.append(i);
        }
    }

    QVector<SessionExport::Item> items;
    items.reserve(indices.size());
    for (int idx : indices) {
        if (idx < 0 || idx >= m_session.size()) {
            continue;
        }
        SessionExport::Item it;
        it.path = m_session.pathAt(idx);
        it.id = m_session.idAt(idx);
        if (it.id != kInvalidSessionImageId) {
            it.appearance = m_imageView->sessionAppearanceValue(it.id);
        }
        items.append(it);
    }
    if (items.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Nothing to export."), 4000);
        }
        return;
    }

    if (statusBar()) {
        statusBar()->showMessage(
            tr("Exporting %n image(s)…", "", items.size()), 0);
    }

    // Snapshot for worker — do not touch session/UI from the pool thread.
    const SessionExport::Options optCopy = opt;
    const QVector<SessionExport::Item> itemsCopy = items;
    QPointer<MainWindow> guard(this);
    QThreadPool::globalInstance()->start([guard, optCopy, itemsCopy]() {
        const SessionExport::Result result =
            SessionExport::exportItems(itemsCopy, optCopy);
        if (!guard) {
            return;
        }
        QTimer::singleShot(0, guard, [guard, result]() {
            MainWindow *host = guard.data();
            if (!host || !host->statusBar()) {
                return;
            }
            if (result.written > 0 && result.failed == 0) {
                host->statusBar()->showMessage(
                    QObject::tr("Exported %1 image(s) → %2")
                        .arg(result.written)
                        .arg(result.destPath),
                    6000);
            } else if (result.written > 0) {
                host->statusBar()->showMessage(
                    QObject::tr("Exported %1, failed %2 → %3")
                        .arg(result.written)
                        .arg(result.failed)
                        .arg(result.destPath),
                    8000);
            } else {
                const QString detail = result.errors.isEmpty()
                    ? QObject::tr("unknown error")
                    : result.errors.first();
                host->statusBar()->showMessage(
                    QObject::tr("Export failed: %1").arg(detail), 8000);
                QMessageBox::warning(host, QObject::tr("Export Images"),
                                     QObject::tr("Export failed.\n%1").arg(detail));
            }
        });
    });
}
