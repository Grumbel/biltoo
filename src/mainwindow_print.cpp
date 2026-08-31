// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "imageview.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>
#include <QSettings>
#include <QScreen>

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

    // Paint the whole sheet so the Workspace page guide matches the print
    // mapping 1:1 (no margin-only rescaling).
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

void renderViewToPrinter(ImageView *view, QPrinter *printer)
{
    if (!view || !printer) {
        return;
    }
    // Ensure full-page coordinates match the guide (full sheet).
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

void MainWindow::printDocument()
{
    if (!m_imageView) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    preparePrinter(&printer);
    m_imageView->setPageGuideFromPrinter(printer);

    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    savePrintSettings(&printer);
    preparePrinter(&printer); // re-assert FullPageMode after dialog
    m_imageView->setPageGuideFromPrinter(printer);
    renderViewToPrinter(m_imageView, &printer);
}

void MainWindow::printPreview()
{
    if (!m_imageView) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    preparePrinter(&printer);
    m_imageView->setPageGuideFromPrinter(printer);

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
                // Preview may change page size/orientation; keep full-page mode
                // and refresh the on-canvas guide to match.
                QPageLayout layout = p->pageLayout();
                layout.setMode(QPageLayout::FullPageMode);
                p->setPageLayout(layout);
                p->setFullPage(true);
                m_imageView->setPageGuideFromPrinter(*p);
                renderViewToPrinter(m_imageView, p);
            });
    preview.exec();
    savePrintSettings(&printer);
    m_imageView->setPageGuideFromPrinter(printer);
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
        m_imageView->setPageGuideFromPrinter(printer);
    }
    m_imageView->setPageGuideVisible(on);
}
