// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PERFORMANCEPANEL_H
#define PERFORMANCEPANEL_H

#include <QWidget>

class QShowEvent;
class QHideEvent;

class QLabel;
class QPlainTextEdit;
class QTimer;
class ImageView;

/**
 * Debug dock: live Qt pool + thumtoo work activity + schedule counters.
 * Open from Panels → Performance. Samples only while visible.
 */
class PerformancePanel : public QWidget
{
    Q_OBJECT
public:
    explicit PerformancePanel(QWidget *parent = nullptr);

    void setImageView(ImageView *view);

public slots:
    void refresh();
    void clearLog();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    ImageView *m_view = nullptr;
    QLabel *m_summary = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QTimer *m_timer = nullptr;
};

#endif
