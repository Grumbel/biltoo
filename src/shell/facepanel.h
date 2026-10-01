// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FACEPANEL_H
#define FACEPANEL_H

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/**
 * Dock panel for face detection + recognition: detect, overlay, enroll, match.
 */
class FacePanel : public QWidget {
    Q_OBJECT
public:
    explicit FacePanel(QWidget *parent = nullptr);

    void setBackendSummary(const QString &text);
    void setBusy(bool busy);
    void setStatus(const QString &text);
    void setFacesSummary(const QStringList &lines);
    void setGallerySummary(const QString &text);

    float scoreThreshold() const;
    void setScoreThreshold(float t);
    float matchThreshold() const;
    void setMatchThreshold(float t);
    bool overlayVisible() const;
    void setOverlayVisible(bool on);
    bool showLandmarks() const;
    void setShowLandmarks(bool on);
    QString enrollName() const;
    int selectedFaceIndex() const;

signals:
    void detectRequested();
    void clearRequested();
    void enrollRequested();
    void scoreThresholdChanged(float value);
    void matchThresholdChanged(float value);
    void overlayVisibleChanged(bool on);
    void showLandmarksChanged(bool on);

private:
    void buildUi();

    QLabel *m_backend = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_gallery = nullptr;
    QDoubleSpinBox *m_threshold = nullptr;
    QDoubleSpinBox *m_matchThreshold = nullptr;
    QCheckBox *m_overlay = nullptr;
    QCheckBox *m_landmarks = nullptr;
    QLineEdit *m_name = nullptr;
    QPushButton *m_detectBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_enrollBtn = nullptr;
    QListWidget *m_list = nullptr;
    bool m_busy = false;
};

#endif
