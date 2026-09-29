// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PERFORMANCEPANEL_H
#define PERFORMANCEPANEL_H

#include <QWidget>

class QShowEvent;
class QHideEvent;
class QLabel;
class QFrame;
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
    QFrame *makeMetricCard(const QString &title, QLabel **valueOut);
    void setCardValue(QLabel *value, const QString &text, const QString &state);

    ImageView *m_view = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_poolValue = nullptr;
    QLabel *m_queueValue = nullptr;
    QLabel *m_focusValue = nullptr;
    QLabel *m_activityValue = nullptr;
    QLabel *m_deltaValue = nullptr;
    QLabel *m_detail = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QTimer *m_timer = nullptr;
};

#endif
