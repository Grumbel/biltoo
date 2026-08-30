// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;

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

    int masonryColumnWidth() const;
    void setMasonryColumnWidth(int pixels);

    bool imageModeLeftDragPan() const;
    void setImageModeLeftDragPan(bool on);

private:
    QDoubleSpinBox *m_intervalSpin = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QCheckBox *m_workspaceCheck = nullptr;
    QCheckBox *m_slideshowFullscreenCheck = nullptr;
    QSpinBox *m_masonryWidthSpin = nullptr;
    QCheckBox *m_imageModePanCheck = nullptr;
};

#endif // PREFERENCESDIALOG_H
