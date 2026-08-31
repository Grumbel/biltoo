// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "imageview.h"

#include <QPainter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPageLayout>

namespace {

void renderViewToPrinter(ImageView *view, QPrinter *printer)
{
    if (!view || !printer) {
        return;
    }
    QPainter painter(printer);
    if (!painter.isActive()) {
        return;
    }
    const QRectF page = printer->pageRect(QPrinter::DevicePixel);
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
    m_imageView->setPageGuideFromPrinter(printer);

    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_imageView->setPageGuideFromPrinter(printer);
    renderViewToPrinter(m_imageView, &printer);
}

void MainWindow::printPreview()
{
    if (!m_imageView) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    m_imageView->setPageGuideFromPrinter(printer);

    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(tr("Print Preview"));
    connect(&preview, &QPrintPreviewDialog::paintRequested, this,
            [this](QPrinter *p) {
                if (m_imageView && p) {
                    m_imageView->setPageGuideFromPrinter(*p);
                    renderViewToPrinter(m_imageView, p);
                }
            });
    preview.exec();
}

void MainWindow::togglePageGuide()
{
    if (!m_imageView || !m_pageGuideAct) {
        return;
    }
    const bool on = m_pageGuideAct->isChecked();
    if (on) {
        QPrinter printer(QPrinter::HighResolution);
        m_imageView->setPageGuideFromPrinter(printer);
    }
    m_imageView->setPageGuideVisible(on);
}
