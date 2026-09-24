// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWSETTINGSDIALOG_H
#define SLIDESHOWSETTINGSDIALOG_H

#include <QDialog>
#include <QColor>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QToolButton;
class QWidget;

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

    bool loop() const;
    void setLoop(bool on);

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

    int letterboxFillIndex() const;
    void setLetterboxFillIndex(int index);

    QColor padColor() const;
    void setPadColor(const QColor &color);

signals:
    /** Emitted after any control changes (live apply). */
    void settingsChanged();

private:
    void emitChanged();
    void syncTransitionCap();
    void updateResetButtons();
    QWidget *wrapWithReset(QWidget *field, QToolButton **resetBtnOut,
                           const std::function<void()> &resetFn);

    QDoubleSpinBox *m_intervalSpin = nullptr;
    QCheckBox *m_fullscreenCheck = nullptr;
    QCheckBox *m_loopCheck = nullptr;
    QComboBox *m_transitionCombo = nullptr;
    QDoubleSpinBox *m_transitionMsSpin = nullptr;
    QComboBox *m_motionCombo = nullptr;
    QDoubleSpinBox *m_panZoomFactorSpin = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_letterboxCombo = nullptr;
    QPushButton *m_padColorBtn = nullptr;

    QToolButton *m_resetIntervalBtn = nullptr;
    QToolButton *m_resetFullscreenBtn = nullptr;
    QToolButton *m_resetLoopBtn = nullptr;
    QToolButton *m_resetTransitionBtn = nullptr;
    QToolButton *m_resetTransitionMsBtn = nullptr;
    QToolButton *m_resetMotionBtn = nullptr;
    QToolButton *m_resetPanZoomFactorBtn = nullptr;
    QToolButton *m_resetZoomBtn = nullptr;
    QToolButton *m_resetLetterboxBtn = nullptr;
    QToolButton *m_resetPadColorBtn = nullptr;

    QColor m_padColor{42, 42, 42};
    bool m_blockEmit = false;

    void updateLetterboxControls();
    static void styleColorButton(QPushButton *btn, const QColor &color);
};

#endif
