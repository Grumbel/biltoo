// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"
#include "shell/mainwindow.h"
#include "imageview.h"
#include "shell/thumbnailbar.h"
#include "session/sessionexport.h"

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
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QDesktopServices>

#include <QThreadPool>
#include <QProgressDialog>
#include <QTimer>
#include <QRadioButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>

#include <algorithm>
#include <memory>
#include <atomic>

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

    QSettings settings;
    settings.beginGroup(QStringLiteral("exportImages"));

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Export Images"));
    dlg.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dlg);

    // --- Scope ---
    const QList<int> selectedRows = sessionSelectionIndices();
    const int selCount = selectedRows.size();
    auto *scopeBox = new QGroupBox(tr("Scope"), &dlg);
    auto *scopeLay = new QVBoxLayout(scopeBox);
    auto *scopeAll = new QRadioButton(
        tr("Entire session (%n image(s))", "", m_session.size()), scopeBox);
    auto *scopeSel = new QRadioButton(
        tr("Selection only (%n image(s))", "", qMax(selCount, 1)), scopeBox);
    if (selCount == 0) {
        scopeSel->setText(tr("Selection only (none)"));
        scopeSel->setEnabled(false);
        scopeAll->setChecked(true);
    } else if (selCount < m_session.size() && selCount > 0) {
        scopeSel->setChecked(true);
    } else {
        scopeAll->setChecked(true);
    }
    scopeLay->addWidget(scopeAll);
    scopeLay->addWidget(scopeSel);
    layout->addWidget(scopeBox);

    // --- Output ---
    auto *outBox = new QGroupBox(tr("Output"), &dlg);
    auto *outForm = new QFormLayout(outBox);

    auto *containerCombo = new QComboBox(outBox);
    containerCombo->addItem(tr("Folder of images"),
                            static_cast<int>(SessionExport::Container::Directory));
    containerCombo->addItem(tr("Comic book archive (.cbz)"),
                            static_cast<int>(SessionExport::Container::Cbz));
    containerCombo->addItem(tr("Multi-page PDF"),
                            static_cast<int>(SessionExport::Container::Pdf));
    {
        const int saved = settings.value(QStringLiteral("container"), 0).toInt();
        const int idx = containerCombo->findData(saved);
        if (idx >= 0) {
            containerCombo->setCurrentIndex(idx);
        }
    }
    outForm->addRow(tr("Container"), containerCombo);

    auto *formatCombo = new QComboBox(outBox);
    formatCombo->addItem(tr("JPEG"), static_cast<int>(SessionExport::Format::Jpeg));
    formatCombo->addItem(tr("PNG"), static_cast<int>(SessionExport::Format::Png));
    {
        const int saved = settings.value(QStringLiteral("format"), 0).toInt();
        const int idx = formatCombo->findData(saved);
        if (idx >= 0) {
            formatCombo->setCurrentIndex(idx);
        }
    }
    outForm->addRow(tr("Image format"), formatCombo);

    auto *qualitySpin = new QSpinBox(outBox);
    qualitySpin->setRange(1, 100);
    qualitySpin->setValue(settings.value(QStringLiteral("jpegQuality"), 90).toInt());
    qualitySpin->setSuffix(tr("%"));
    qualitySpin->setToolTip(tr("JPEG quality (ignored for PNG and PDF)."));
    outForm->addRow(tr("JPEG quality"), qualitySpin);

    auto *edgeCombo = new QComboBox(outBox);
    edgeCombo->setEditable(false);
    edgeCombo->addItem(tr("Native (after bake)"), 0);
    edgeCombo->addItem(tr("2048 px long edge"), 2048);
    edgeCombo->addItem(tr("4096 px long edge"), 4096);
    edgeCombo->addItem(tr("8192 px long edge"), 8192);
    edgeCombo->addItem(tr("Custom…"), -1);
    auto *edgeSpin = new QSpinBox(outBox);
    edgeSpin->setRange(64, 16384);
    edgeSpin->setSingleStep(64);
    edgeSpin->setValue(settings.value(QStringLiteral("maxLongEdge"), 0).toInt());
    if (edgeSpin->value() <= 0) {
        edgeSpin->setValue(2048);
    }
    edgeSpin->setSuffix(tr(" px"));
    {
        const int savedEdge = settings.value(QStringLiteral("maxLongEdge"), 0).toInt();
        const int preset = edgeCombo->findData(savedEdge);
        if (savedEdge <= 0) {
            edgeCombo->setCurrentIndex(0); // Native
            edgeSpin->setVisible(false);
        } else if (preset >= 0) {
            edgeCombo->setCurrentIndex(preset);
            edgeSpin->setVisible(false);
        } else {
            edgeCombo->setCurrentIndex(edgeCombo->findData(-1));
            edgeSpin->setValue(savedEdge);
            edgeSpin->setVisible(true);
        }
    }
    auto *edgeRow = new QHBoxLayout;
    edgeRow->addWidget(edgeCombo, 1);
    edgeRow->addWidget(edgeSpin);
    outForm->addRow(tr("Max long edge"), edgeRow);
    outForm->addRow(QString(), new QLabel(
        tr("Sources are never overwritten — only new files are written."), outBox));

    layout->addWidget(outBox);

    // --- Destination ---
    auto *destBox = new QGroupBox(tr("Destination"), &dlg);
    auto *destLay = new QVBoxLayout(destBox);
    auto *destEdit = new QLineEdit(destBox);
    destEdit->setClearButtonEnabled(true);
    auto *browseBtn = new QPushButton(tr("Browse…"), destBox);
    auto *destRow = new QHBoxLayout;
    destRow->addWidget(destEdit, 1);
    destRow->addWidget(browseBtn);
    destLay->addLayout(destRow);
    auto *openAfter = new QCheckBox(tr("Open destination when finished"), destBox);
    openAfter->setChecked(settings.value(QStringLiteral("openAfter"), false).toBool());
    destLay->addWidget(openAfter);
    layout->addWidget(destBox);

    auto *summary = new QLabel(&dlg);
    summary->setWordWrap(true);
    summary->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    layout->addWidget(buttons);

    const auto containerOf = [containerCombo]() {
        return static_cast<SessionExport::Container>(containerCombo->currentData().toInt());
    };
    const auto formatOf = [formatCombo]() {
        return static_cast<SessionExport::Format>(formatCombo->currentData().toInt());
    };
    const auto longEdgeOf = [edgeCombo, edgeSpin]() -> int {
        const int d = edgeCombo->currentData().toInt();
        if (d == 0) {
            return 0;
        }
        if (d > 0) {
            return d;
        }
        return edgeSpin->value();
    };
    const auto itemCount = [scopeAll, scopeSel, selCount, this]() -> int {
        if (scopeSel->isChecked() && scopeSel->isEnabled()) {
            return selCount;
        }
        return m_session.size();
    };
    const auto defaultStem = [this]() -> QString {
        if (m_session.isEmpty()) {
            return QStringLiteral("export");
        }
        return SessionExport::fileStem(m_session.pathAt(0));
    };
    const auto suggestDest = [&]() {
        const QString lastDir = settings.value(QStringLiteral("lastDir")).toString();
        QString base = lastDir;
        if (base.isEmpty()) {
            const QFileInfo fi(m_session.pathAt(0));
            // Virtual paths: use parent of the document when possible.
            QString p = fi.absolutePath();
            if (p.isEmpty() || p == QLatin1String(".")) {
                p = QDir::homePath();
            }
            base = p;
        }
        const auto c = containerOf();
        if (c == SessionExport::Container::Directory) {
            return QDir(base).filePath(defaultStem() + QStringLiteral("_export"));
        }
        if (c == SessionExport::Container::Cbz) {
            return QDir(base).filePath(defaultStem() + QStringLiteral(".cbz"));
        }
        return QDir(base).filePath(defaultStem() + QStringLiteral(".pdf"));
    };

    // Seed destination from last path or a sensible default.
    {
        const QString lastDest = settings.value(QStringLiteral("lastDest")).toString();
        if (!lastDest.isEmpty()) {
            destEdit->setText(lastDest);
        } else {
            destEdit->setText(suggestDest());
        }
        destEdit->setPlaceholderText(suggestDest());
    }

    const auto updateControls = [&]() {
        const auto c = containerOf();
        const bool raster = (c != SessionExport::Container::Pdf);
        formatCombo->setEnabled(raster);
        const bool jpeg = raster
            && formatOf() == SessionExport::Format::Jpeg;
        qualitySpin->setEnabled(jpeg);
        edgeSpin->setVisible(edgeCombo->currentData().toInt() < 0);

        const int n = itemCount();
        QString fmtName;
        if (c == SessionExport::Container::Pdf) {
            fmtName = tr("PDF");
        } else if (formatOf() == SessionExport::Format::Png) {
            fmtName = tr("PNG");
        } else {
            fmtName = tr("JPEG");
        }
        QString destHint = destEdit->text().trimmed();
        if (destHint.isEmpty()) {
            destHint = tr("(choose destination)");
        }
        summary->setText(
            tr("Will export %n image(s) as %1 → %2", "", n)
                .arg(fmtName, destHint));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(n > 0);
    };

    QObject::connect(containerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &dlg, [&](int) {
                         // Refresh suggestion when container type changes and
                         // the field still matches an old extension/folder pattern.
                         const QString cur = destEdit->text().trimmed();
                         const auto c = containerOf();
                         if (cur.isEmpty()
                             || cur.endsWith(QLatin1String(".cbz"), Qt::CaseInsensitive)
                             || cur.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)
                             || QFileInfo(cur).isDir()
                             || cur.endsWith(QLatin1String("_export"))) {
                             destEdit->setText(suggestDest());
                         } else if (c == SessionExport::Container::Cbz
                                    && !cur.endsWith(QLatin1String(".cbz"), Qt::CaseInsensitive)) {
                             destEdit->setText(cur + QStringLiteral(".cbz"));
                         } else if (c == SessionExport::Container::Pdf
                                    && !cur.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
                             destEdit->setText(cur + QStringLiteral(".pdf"));
                         }
                         destEdit->setPlaceholderText(suggestDest());
                         updateControls();
                     });
    QObject::connect(formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &dlg, [&](int) { updateControls(); });
    QObject::connect(edgeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &dlg, [&](int) { updateControls(); });
    QObject::connect(edgeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                     &dlg, [&](int) { updateControls(); });
    QObject::connect(scopeAll, &QRadioButton::toggled, &dlg, [&](bool) { updateControls(); });
    QObject::connect(scopeSel, &QRadioButton::toggled, &dlg, [&](bool) { updateControls(); });
    QObject::connect(destEdit, &QLineEdit::textChanged, &dlg, [&](const QString &) {
        updateControls();
    });
    updateControls();

    QObject::connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
        const auto c = containerOf();
        const QString start = destEdit->text().trimmed().isEmpty()
            ? suggestDest()
            : destEdit->text().trimmed();
        if (c == SessionExport::Container::Directory) {
            const QString d = QFileDialog::getExistingDirectory(
                &dlg, tr("Export Folder"), start);
            if (!d.isEmpty()) {
                destEdit->setText(d);
            }
        } else if (c == SessionExport::Container::Cbz) {
            const QString f = QFileDialog::getSaveFileName(
                &dlg, tr("Export CBZ"), start, tr("Comic book (*.cbz)"));
            if (!f.isEmpty()) {
                destEdit->setText(f);
            }
        } else {
            const QString f = QFileDialog::getSaveFileName(
                &dlg, tr("Export PDF"), start, tr("PDF (*.pdf)"));
            if (!f.isEmpty()) {
                destEdit->setText(f);
            }
        }
    });

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        settings.endGroup();
        return;
    }

    QString dest = destEdit->text().trimmed();
    if (dest.isEmpty()) {
        settings.endGroup();
        if (statusBar()) {
            statusBar()->showMessage(tr("No destination chosen."), 4000);
        }
        return;
    }

    SessionExport::Options opt;
    opt.container = containerOf();
    opt.format = formatOf();
    opt.jpegQuality = qualitySpin->value();
    opt.maxLongEdge = longEdgeOf();
    opt.destPath = dest;
    if (opt.container == SessionExport::Container::Cbz
        && !opt.destPath.endsWith(QLatin1String(".cbz"), Qt::CaseInsensitive)) {
        opt.destPath += QStringLiteral(".cbz");
    }
    if (opt.container == SessionExport::Container::Pdf
        && !opt.destPath.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
        opt.destPath += QStringLiteral(".pdf");
    }

    // Confirm overwrite for file containers.
    if (opt.container != SessionExport::Container::Directory
        && QFileInfo::exists(opt.destPath)) {
        const auto ans = QMessageBox::question(
            this, tr("Export Images"),
            tr("“%1” already exists. Overwrite?").arg(QFileInfo(opt.destPath).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            settings.endGroup();
            return;
        }
    }

    settings.setValue(QStringLiteral("container"), static_cast<int>(opt.container));
    settings.setValue(QStringLiteral("format"), static_cast<int>(opt.format));
    settings.setValue(QStringLiteral("jpegQuality"), opt.jpegQuality);
    settings.setValue(QStringLiteral("maxLongEdge"), opt.maxLongEdge);
    settings.setValue(QStringLiteral("lastDest"), opt.destPath);
    settings.setValue(QStringLiteral("openAfter"), openAfter->isChecked());
    {
        const QFileInfo fi(opt.destPath);
        settings.setValue(QStringLiteral("lastDir"),
                          fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
    }
    const bool openWhenDone = openAfter->isChecked();
    settings.endGroup();

    QList<int> indices;
    if (scopeSel->isChecked() && scopeSel->isEnabled() && !selectedRows.isEmpty()) {
        indices = selectedRows;
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
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

    auto *progress = new QProgressDialog(
        tr("Exporting images…"), tr("Cancel"), 0, items.size(), this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(400);
    progress->setValue(0);

    // Shared cancel flag for dialog button + worker.
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(progress, &QProgressDialog::canceled, this, [cancelFlag]() {
        cancelFlag->store(true, std::memory_order_relaxed);
    });

    const QVector<SessionExport::Item> itemsCopy = items;
    const SessionExport::Options optCopy = opt;
    QPointer<MainWindow> guard(this);
    QPointer<QProgressDialog> progressGuard(progress);

    QThreadPool::globalInstance()->start([guard, progressGuard, itemsCopy, optCopy,
                                          cancelFlag, openWhenDone]() {
        SessionExport::ProgressFn progressFn =
            [progressGuard](int completed, int total) -> bool {
                if (!progressGuard) {
                    return false;
                }
                QMetaObject::invokeMethod(
                    progressGuard.data(),
                    [progressGuard, completed, total]() {
                        if (!progressGuard) {
                            return;
                        }
                        progressGuard->setMaximum(total);
                        progressGuard->setValue(completed);
                        progressGuard->setLabelText(
                            QObject::tr("Exporting %1 / %2…")
                                .arg(completed)
                                .arg(total));
                    },
                    Qt::QueuedConnection);
                return true;
            };

        const SessionExport::Result result = SessionExport::exportItems(
            itemsCopy, optCopy, progressFn, cancelFlag.get());

        if (!guard) {
            return;
        }
        QTimer::singleShot(0, guard, [guard, progressGuard, result, openWhenDone]() {
            MainWindow *host = guard.data();
            if (progressGuard) {
                progressGuard->reset();
                progressGuard->deleteLater();
            }
            if (!host || !host->statusBar()) {
                return;
            }
            if (result.cancelled) {
                host->statusBar()->showMessage(
                    QObject::tr("Export cancelled (%1 written, %2 failed)")
                        .arg(result.written)
                        .arg(result.failed),
                    6000);
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
                if (!result.errors.isEmpty()) {
                    QMessageBox::warning(
                        host, QObject::tr("Export Images"),
                        QObject::tr("Exported with errors.\n%1")
                            .arg(result.errors.join(QLatin1Char('\n'))));
                }
            } else {
                const QString detail = result.errors.isEmpty()
                    ? QObject::tr("unknown error")
                    : result.errors.join(QLatin1Char('\n'));
                host->statusBar()->showMessage(
                    QObject::tr("Export failed: %1").arg(detail), 8000);
                QMessageBox::warning(
                    host, QObject::tr("Export Images"),
                    QObject::tr("Export failed.\n%1").arg(detail));
            }
            if (openWhenDone && result.written > 0 && !result.destPath.isEmpty()) {
                QString openPath = result.destPath;
                const QFileInfo fi(openPath);
                if (!fi.isDir()) {
                    openPath = fi.absolutePath();
                }
                QDesktopServices::openUrl(QUrl::fromLocalFile(openPath));
            }
        });
    });
}

