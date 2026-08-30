// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QColor>
#include <QDialog>

class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QTreeWidget;
class QLabel;
class QPushButton;

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    /** sortModeIndex: 0 = name, 1 = mtime */
    explicit PreferencesDialog(QWidget *parent = nullptr);

    int slideshowIntervalMs() const;
    void setSlideshowIntervalMs(int ms);

    int sortModeIndex() const;
    void setSortModeIndex(int index);

    bool startInWorkspaceMode() const;
    void setStartInWorkspaceMode(bool on);

    bool slideshowFullscreen() const;
    void setSlideshowFullscreen(bool on);

    bool imageModeLeftDragPan() const;
    void setImageModeLeftDragPan(bool on);

    QColor backgroundColor() const;
    void setBackgroundColor(const QColor &color);

    QColor backgroundColorAlt() const;
    void setBackgroundColorAlt(const QColor &color);

    /** 0 = solid, 1 = checkerboard */
    int backgroundPatternIndex() const;
    void setBackgroundPatternIndex(int index);

    bool checkerboardWorkspaceOnly() const;
    void setCheckerboardWorkspaceOnly(bool on);

private:
    void setupDefaultAppsGroup();
    void refreshDefaultAppsList();
    void onSetDefaultForChecked();
    void onSetDefaultForAll();
    void updateColorButton(QPushButton *button, const QColor &color);
    void chooseBackgroundColor();
    void chooseBackgroundColorAlt();
    void updateBackgroundControlsEnabled();

    QDoubleSpinBox *m_intervalSpin = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QCheckBox *m_workspaceCheck = nullptr;
    QCheckBox *m_slideshowFullscreenCheck = nullptr;
    QCheckBox *m_imageModePanCheck = nullptr;

    QPushButton *m_bgColorBtn = nullptr;
    QPushButton *m_bgColorAltBtn = nullptr;
    QComboBox *m_bgPatternCombo = nullptr;
    QCheckBox *m_bgCheckerWorkspaceOnlyCheck = nullptr;
    QColor m_bgColor{42, 42, 42};
    QColor m_bgColorAlt{48, 48, 48};

    QTreeWidget *m_mimeTree = nullptr;
    QLabel *m_mimeStatusLabel = nullptr;
    QPushButton *m_setCheckedBtn = nullptr;
    QPushButton *m_setAllBtn = nullptr;
};

#endif // PREFERENCESDIALOG_H
