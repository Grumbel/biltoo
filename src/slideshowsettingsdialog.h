// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWSETTINGSDIALOG_H
#define SLIDESHOWSETTINGSDIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

/**
 * Mid-session slideshow controls. Changes apply immediately (no OK); the
 * dialog is dismissed with Close. Same keys as Preferences → Slideshow.
 */
class SlideshowSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SlideshowSettingsDialog(QWidget *parent = nullptr);

    int intervalMs() const;
    void setIntervalMs(int ms);

    bool startFullscreen() const;
    void setStartFullscreen(bool on);

    int transitionIndex() const;
    void setTransitionIndex(int index);

    int transitionDurationMs() const;
    void setTransitionDurationMs(int ms);

    int motionIndex() const;
    void setMotionIndex(int index);

    double panZoomFactor() const;
    void setPanZoomFactor(double factor);

    int zoomIndex() const;
    void setZoomIndex(int index);

signals:
    /** Emitted after any control changes (live apply). */
    void settingsChanged();

private:
    void emitChanged();
    void syncTransitionCap();

    QDoubleSpinBox *m_intervalSpin = nullptr;
    QCheckBox *m_fullscreenCheck = nullptr;
    QComboBox *m_transitionCombo = nullptr;
    QSpinBox *m_transitionMsSpin = nullptr;
    QComboBox *m_motionCombo = nullptr;
    QDoubleSpinBox *m_panZoomFactorSpin = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    bool m_blockEmit = false;
};

#endif
