// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell/statuspanel.h"

#include "host/pagepath.h"
#include "imageitem.h"
#include "imageview.h"
#include "tilelod/page_profile_service.hpp"
#include "tilelod/source_status.hpp"
#include "tilelod/tile_lod_controller.hpp"

#include <thumtoo/djvu.hpp>
#include <thumtoo/pdf.hpp>

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString toneColor(tilelod::StatusTone tone)
{
    switch (tone) {
    case tilelod::StatusTone::Good:
        return QStringLiteral("#2a8");
    case tilelod::StatusTone::Warn:
        return QStringLiteral("#c90");
    case tilelod::StatusTone::Bad:
        return QStringLiteral("#c44");
    case tilelod::StatusTone::Normal:
        break;
    }
    return {};
}

QString sectionsToHtml(const std::vector<tilelod::StatusSection> &sections)
{
    QString html = QStringLiteral("<html><body style='margin:0'>");
    for (const tilelod::StatusSection &sec : sections) {
        html += QStringLiteral("<h4 style='margin:10px 0 4px 0'>%1</h4>")
                    .arg(QString::fromStdString(sec.title).toHtmlEscaped());
        html += QStringLiteral("<table cellspacing='0' cellpadding='2' width='100%'>");
        for (const tilelod::StatusRow &row : sec.rows) {
            const QString color = toneColor(row.tone);
            const QString value = QString::fromStdString(row.value).toHtmlEscaped();
            html += QStringLiteral("<tr><td valign='top' style='color:palette(mid);"
                                   "white-space:nowrap;padding-right:10px'>%1</td>"
                                   "<td>%2</td></tr>")
                        .arg(QString::fromStdString(row.label).toHtmlEscaped(),
                             color.isEmpty()
                                 ? value
                                 : QStringLiteral("<span style='color:%1;font-weight:600'>%2</span>")
                                       .arg(color, value));
        }
        html += QStringLiteral("</table>");
    }
    html += QStringLiteral("</body></html>");
    return html;
}

} // namespace

StatusPanel::StatusPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *header = new QHBoxLayout;
    m_title = new QLabel(tr("No image"), this);
    {
        QFont f = m_title->font();
        f.setBold(true);
        m_title->setFont(f);
    }
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_title->setWordWrap(true);
    header->addWidget(m_title, 1);
    auto *copyBtn = new QPushButton(tr("Copy report"), this);
    connect(copyBtn, &QPushButton::clicked, this, &StatusPanel::copyReport);
    header->addWidget(copyBtn);
    root->addLayout(header);

    m_body = new QTextBrowser(this);
    m_body->setOpenLinks(false);
    m_body->setFrameShape(QFrame::NoFrame);
    root->addWidget(m_body, 1);

    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &StatusPanel::refresh);

    m_reportFile = qEnvironmentVariable("BILTOO_STATUS_REPORT");
    if (!m_reportFile.isEmpty()) {
        m_timer->start();
    }
}

void StatusPanel::setImageView(ImageView *view)
{
    m_view = view;
    refresh();
}

void StatusPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_timer->start();
    refresh();
}

void StatusPanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_reportFile.isEmpty()) {
        m_timer->stop();
    }
}

void StatusPanel::refresh()
{
    if ((!isVisible() && m_reportFile.isEmpty()) || !m_view) {
        return;
    }
    ImageItem *item = m_view->targetItem();
    if (!item) {
        item = m_view->primaryItem();
    }
    if (!item || item->path().isEmpty()) {
        m_title->setText(tr("No image selected"));
        if (!m_lastHtml.isEmpty()) {
            m_body->clear();
            m_lastHtml.clear();
            m_lastText.clear();
        }
        return;
    }
    const QString path = item->path();
    tilelod::SourceStatus status;
    status.record = tilelod::PageProfileService::instance().observe(path);
    if (tilelod::TileLodController *lod = item->tileLodController()) {
        if (const tilelod::TileSession *session = lod->session()) {
            status.tiles = session->debug_snapshot();
            status.tile_error = session->first_error();
        }
    }
    if (!status.record.document_file.empty()) {
        if (status.record.kind == tilelod::SourceKind::PdfPage) {
            status.render = thumtoo::pdf_document_render_stats(status.record.document_file);
        } else if (status.record.kind == tilelod::SourceKind::DjvuPage) {
            status.render = thumtoo::djvu_document_render_stats(status.record.document_file);
        }
    }
    const auto sections = tilelod::build_status_sections(status);
    m_title->setText(PagePath::displayName(path));
    const QString html = sectionsToHtml(sections);
    if (html != m_lastHtml) {
        const int scroll = m_body->verticalScrollBar() ? m_body->verticalScrollBar()->value() : 0;
        m_body->setHtml(html);
        if (m_body->verticalScrollBar()) {
            m_body->verticalScrollBar()->setValue(scroll);
        }
        m_lastHtml = html;
        m_lastText = QString::fromStdString(tilelod::format_status_text(sections));
        if (!m_reportFile.isEmpty()) {
            QSaveFile out(m_reportFile);
            if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
                out.write(m_lastText.toUtf8());
                out.commit();
            }
        }
    }
}

void StatusPanel::copyReport()
{
    refresh();
    if (!m_lastText.isEmpty()) {
        QApplication::clipboard()->setText(m_lastText);
    }
}
