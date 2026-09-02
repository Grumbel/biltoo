// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LAYOUTPANEL_H
#define LAYOUTPANEL_H

#include "gallerylayout.h"

#include <QWidget>

class QButtonGroup;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;

/**
 * Workspace side panel: choose a packaged layout and parameters, then Apply
 * to the current canvas selection (does not enter Gallery mode).
 */
class LayoutPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayoutPanel(QWidget *parent = nullptr);

    GalleryLayout::Params params() const;
    GalleryLayout::Mode selectedMode() const;

    void setWorkspaceActive(bool active);
    void setSelectionCount(int count);

signals:
    void applyRequested();

private:
    void updateControlsEnabled();

    QButtonGroup *m_modeGroup = nullptr;
    QToolButton *m_sideBySideBtn = nullptr;
    QToolButton *m_verticalBtn = nullptr;
    QToolButton *m_gridBtn = nullptr;
    QToolButton *m_gridCropBtn = nullptr;
    QToolButton *m_masonryBtn = nullptr;
    QToolButton *m_masonryRowsBtn = nullptr;
    QToolButton *m_masonryFillBtn = nullptr;
    QToolButton *m_masonryRowsFillBtn = nullptr;
    QLabel *m_columnsLabel = nullptr;
    QSpinBox *m_columnsSpin = nullptr;
    QLabel *m_rowsLabel = nullptr;
    QSpinBox *m_rowsSpin = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QLabel *m_hintLabel = nullptr;
    bool m_workspaceActive = false;
    int m_selectionCount = 0;
};

#endif // LAYOUTPANEL_H
