// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CACHEPREPAREDIALOG_H
#define CACHEPREPAREDIALOG_H

#include <QDialog>
#include <QStringList>
#include <atomic>

class QCloseEvent;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

/**
 * Build durable zoom tiles for the current session (thumtoo-prepare --tiles).
 * LQIP is filled automatically during tile encode; size/ladder are not exposed.
 */
class CachePrepareDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CachePrepareDialog(const QStringList &sessionPaths, QWidget *parent = nullptr);
    ~CachePrepareDialog() override;

    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshStats();
    void startPrepare();
    void cancelPrepare();

private:
    void setBusy(bool busy);
    void applyStatsLabel(int total, int withTiles, int missing, int unsupported);
    void onProgress(int done, int total, int ok, int skipped, int failed);
    void onFinished();

    QStringList m_paths;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_detailHint = nullptr;
    QComboBox *m_detailCombo = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    std::atomic<bool> m_cancel{false};
    bool m_running = false;
};

#endif // CACHEPREPAREDIALOG_H
