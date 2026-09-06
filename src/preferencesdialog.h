// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QColor>
#include <QDialog>
#include <functional>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QToolButton;

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

    /** 0 = none, 1 = crossfade, 2 = fade through black */
    int slideshowTransitionIndex() const;
    void setSlideshowTransitionIndex(int index);

    int slideshowTransitionDurationMs() const;
    void setSlideshowTransitionDurationMs(int ms);

    /** 0 = off, 1 = pan&zoom, 2 = pan&scan */
    int slideshowMotionIndex() const;
    void setSlideshowMotionIndex(int index);

    double panZoomFactor() const;
    void setPanZoomFactor(double factor);

    /** 0 = fit, 1 = fill, 2 = 1:1 */
    int slideshowZoomIndex() const;
    void setSlideshowZoomIndex(int index);

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

    /** ImageView::LayoutMode int for Gallery (Horizontal=1 … Facing=11). */
    int defaultGalleryLayoutMode() const;
    void setDefaultGalleryLayoutMode(int layoutMode);

private:
    void syncSlideshowTransitionCap();
    void refreshDefaultAppsList();
    void onMimeItemChanged(QTreeWidgetItem *item, int column);
    void onSetAllAsDefault();
    void onRemoveAllAsDefault();
    void onSetImagesAsDefault();
    void onRemoveImagesAsDefault();
    void onSetArchivesAsDefault();
    void onRemoveArchivesAsDefault();
    void applySetDefaults(const QStringList &types);
    void applyClearDefaults(const QStringList &types);
    void updateColorButton(QPushButton *button, const QColor &color);
    void chooseBackgroundColor();
    void chooseBackgroundColorAlt();
    void chooseHudTextColor();
    void chooseHudPanelColor();
    void updateBackgroundControlsEnabled();
    /** Enable/disable per-row reset buttons from current control values. */
    void updateResetButtons();
    /** Field + small “reset to default” tool button. */
    QWidget *wrapWithReset(QWidget *field, QToolButton **resetBtnOut,
                           const std::function<void()> &resetFn);

    bool m_mimeTreeUpdating = false;

    QDoubleSpinBox *m_intervalSpin = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QCheckBox *m_workspaceCheck = nullptr;
    QCheckBox *m_slideshowFullscreenCheck = nullptr;
    QComboBox *m_slideshowTransitionCombo = nullptr;
    QDoubleSpinBox *m_slideshowTransitionMsSpin = nullptr;
    QToolButton *m_resetSlideshowTransitionBtn = nullptr;
    QToolButton *m_resetSlideshowTransitionMsBtn = nullptr;
    QComboBox *m_slideshowMotionCombo = nullptr;
    QDoubleSpinBox *m_panZoomFactorSpin = nullptr;
    QToolButton *m_resetSlideshowMotionBtn = nullptr;
    QToolButton *m_resetPanZoomFactorBtn = nullptr;
    QComboBox *m_slideshowZoomCombo = nullptr;
    QToolButton *m_resetSlideshowZoomBtn = nullptr;
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

    QToolButton *m_resetIntervalBtn = nullptr;
    QToolButton *m_resetSlideshowFsBtn = nullptr;
    QToolButton *m_resetSortBtn = nullptr;
    QToolButton *m_resetWorkspaceBtn = nullptr;
    QToolButton *m_resetImagePanBtn = nullptr;
    QToolButton *m_resetBgPatternBtn = nullptr;
    QToolButton *m_resetBgColorBtn = nullptr;
    QToolButton *m_resetBgColorAltBtn = nullptr;
    QToolButton *m_resetCheckerWsBtn = nullptr;
    QToolButton *m_resetHudFontBtn = nullptr;
    QToolButton *m_resetHudTextBtn = nullptr;
    QToolButton *m_resetHudPanelBtn = nullptr;
    QToolButton *m_resetScrollBarsBtn = nullptr;
    QToolButton *m_resetThumbLabelsBtn = nullptr;
    QToolButton *m_resetAdjPanelBtn = nullptr;
    QToolButton *m_resetLayoutPanelBtn = nullptr;
    QToolButton *m_resetThumbsWsBtn = nullptr;
    QToolButton *m_resetThumbsGalBtn = nullptr;
    QToolButton *m_resetThumbPosBtn = nullptr;
    QToolButton *m_resetGalleryLayoutBtn = nullptr;

    QTreeWidget *m_mimeTree = nullptr;
    QLabel *m_mimeStatusLabel = nullptr;
    QPushButton *m_setAllBtn = nullptr;
    QPushButton *m_removeAllBtn = nullptr;
    QPushButton *m_setImagesBtn = nullptr;
    QPushButton *m_removeImagesBtn = nullptr;
    QPushButton *m_setArchivesBtn = nullptr;
    QPushButton *m_removeArchivesBtn = nullptr;
};

#endif // PREFERENCESDIALOG_H
