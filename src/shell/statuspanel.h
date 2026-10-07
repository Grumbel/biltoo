// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STATUSPANEL_H
#define STATUSPANEL_H

#include <QString>
#include <QWidget>

class QHideEvent;
class QLabel;
class QShowEvent;
class QTextBrowser;
class QTimer;
class ImageView;

/**
 * Panels → Status: how the focused source is being processed.
 *
 * Shows the source record (page analysis from thumtoo, every biltoo decision
 * with its reason), the focused view's tile state, the shared tile loader and
 * thumtoo's render / image-decode counts. Pure presentation of
 * tilelod::build_status_sections(); polls while visible, owns no state.
 * With BILTOO_STATUS_REPORT=<file> it also polls while hidden and rewrites
 * the plain-text report to that file whenever it changes.
 */
class StatusPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StatusPanel(QWidget *parent = nullptr);

    void setImageView(ImageView *view);

public slots:
    void refresh();
    void copyReport();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    ImageView *m_view = nullptr;
    QLabel *m_title = nullptr;
    QTextBrowser *m_body = nullptr;
    QTimer *m_timer = nullptr;
    QString m_lastHtml;
    QString m_lastText;
    /** BILTOO_STATUS_REPORT: keep writing the text report here (scripted QA). */
    QString m_reportFile;
};

#endif
