// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QColor>
#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;
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

    int hudFontPointSize() const;
    void setHudFontPointSize(int pt);

    QColor hudTextColor() const;
    void setHudTextColor(const QColor &color);

    QColor hudPanelColor() const;
    void setHudPanelColor(const QColor &color);

    bool scrollBarsVisible() const;
    void setScrollBarsVisible(bool on);

    bool thumbnailLabelsVisible() const;
    void setThumbnailLabelsVisible(bool on);

    bool adjustmentsPanelVisible() const;
    void setAdjustmentsPanelVisible(bool on);

    bool layoutPanelPreferredInWorkspace() const;
    void setLayoutPanelPreferredInWorkspace(bool on);

    bool thumbnailsPreferredWorkspace() const;
    void setThumbnailsPreferredWorkspace(bool on);

    bool thumbnailsPreferredGallery() const;
    void setThumbnailsPreferredGallery(bool on);

    /** 0 = bottom, 1 = top, 2 = left, 3 = right */
    int thumbnailPositionIndex() const;
    void setThumbnailPositionIndex(int index);

    /** ImageView::LayoutMode int for Gallery (1…6). */
    int defaultGalleryLayoutMode() const;
    void setDefaultGalleryLayoutMode(int layoutMode);

private:
    void refreshDefaultAppsList();
    void onMimeItemChanged(QTreeWidgetItem *item, int column);
    void onSetAllAsDefault();
    void onRemoveAllAsDefault();
    void updateColorButton(QPushButton *button, const QColor &color);
    void chooseBackgroundColor();
    void chooseBackgroundColorAlt();
    void chooseHudTextColor();
    void chooseHudPanelColor();
    void updateBackgroundControlsEnabled();

    bool m_mimeTreeUpdating = false;

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

    QSpinBox *m_hudFontSpin = nullptr;
    QPushButton *m_hudTextColorBtn = nullptr;
    QPushButton *m_hudPanelColorBtn = nullptr;
    QColor m_hudTextColor{255, 255, 255};
    QColor m_hudPanelColor{0, 0, 0, 160};

    QCheckBox *m_scrollBarsCheck = nullptr;
    QCheckBox *m_thumbLabelsCheck = nullptr;
    QCheckBox *m_adjustmentsPanelCheck = nullptr;
    QCheckBox *m_layoutPanelCheck = nullptr;
    QCheckBox *m_thumbsWorkspaceCheck = nullptr;
    QCheckBox *m_thumbsGalleryCheck = nullptr;
    QComboBox *m_thumbPosCombo = nullptr;
    QComboBox *m_galleryLayoutCombo = nullptr;

    QTreeWidget *m_mimeTree = nullptr;
    QLabel *m_mimeStatusLabel = nullptr;
    QPushButton *m_setAllBtn = nullptr;
    QPushButton *m_removeAllBtn = nullptr;
};

#endif // PREFERENCESDIALOG_H
