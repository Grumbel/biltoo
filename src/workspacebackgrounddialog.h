// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WORKSPACEBACKGROUNDDIALOG_H
#define WORKSPACEBACKGROUNDDIALOG_H

#include "imageview_types.h"

#include <QDialog>
#include <functional>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QWidget;

/** Live-preview swatch for WorkspaceBackground. */
class WorkspaceBackgroundPreview : public QWidget
{
    Q_OBJECT
public:
    explicit WorkspaceBackgroundPreview(QWidget *parent = nullptr);

    void setBackground(const WorkspaceBackground &bg);
    void setAppDefaultColors(const QColor &color, const QColor &colorAlt, bool checker);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    WorkspaceBackground m_bg;
    QColor m_appColor{42, 42, 42};
    QColor m_appColorAlt{48, 48, 48};
    bool m_appChecker = true;
    QPixmap m_tile;
    QString m_tilePath;
};

/**
 * Edit per-Workspace canvas background (project state).
 * AppDefault uses Preferences colours/pattern and is not stored in the project.
 */
class WorkspaceBackgroundDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WorkspaceBackgroundDialog(QWidget *parent = nullptr);

    void setBackground(const WorkspaceBackground &bg);
    WorkspaceBackground background() const;

    /** Colours used when mode is AppDefault (from Preferences). */
    void setAppDefaultColors(const QColor &color, const QColor &colorAlt, bool checker);

signals:
    /** Emitted when the user changes any control (live preview on canvas). */
    void backgroundChanged(const WorkspaceBackground &bg);

private:
    void updateControlsEnabled();
    void updatePreview();
    void chooseColor();
    void chooseColorAlt();
    void browseImage();
    static void styleColorButton(QPushButton *btn, const QColor &color);
    QWidget *wrapWithReset(QWidget *field, const std::function<void()> &resetFn);

    QComboBox *m_modeCombo = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QPushButton *m_colorAltBtn = nullptr;
    QLineEdit *m_imageEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QWidget *m_colorRow = nullptr;
    QWidget *m_colorAltRow = nullptr;
    QWidget *m_imageRowWidget = nullptr;
    WorkspaceBackgroundPreview *m_preview = nullptr;

    QColor m_color{42, 42, 42};
    QColor m_colorAlt{48, 48, 48};
    QColor m_appColor{42, 42, 42};
    QColor m_appColorAlt{48, 48, 48};
    bool m_appChecker = true;
    bool m_blockPreviewEmit = false;
};

#endif // WORKSPACEBACKGROUNDDIALOG_H
