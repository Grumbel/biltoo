// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FACEPANEL_H
#define FACEPANEL_H

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

/**
 * Dock panel for face detection: run on current page, overlay toggles,
 * score threshold, result list. Backend details come from FaceController.
 */
class FacePanel : public QWidget {
    Q_OBJECT
public:
    explicit FacePanel(QWidget *parent = nullptr);

    void setBackendSummary(const QString &text);
    void setBusy(bool busy);
    void setStatus(const QString &text);
    void setFacesSummary(const QStringList &lines);

    float scoreThreshold() const;
    void setScoreThreshold(float t);
    bool overlayVisible() const;
    void setOverlayVisible(bool on);
    bool showLandmarks() const;
    void setShowLandmarks(bool on);

signals:
    void detectRequested();
    void clearRequested();
    void scoreThresholdChanged(float value);
    void overlayVisibleChanged(bool on);
    void showLandmarksChanged(bool on);

private:
    void buildUi();

    QLabel *m_backend = nullptr;
    QLabel *m_status = nullptr;
    QDoubleSpinBox *m_threshold = nullptr;
    QCheckBox *m_overlay = nullptr;
    QCheckBox *m_landmarks = nullptr;
    QPushButton *m_detectBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QListWidget *m_list = nullptr;
    bool m_busy = false;
};

#endif
