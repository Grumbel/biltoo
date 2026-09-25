// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CROPPANEL_H
#define CROPPANEL_H

#include "crop/croprecipe.h"
#include "item/batchtargets.h"

#include <QSize>
#include <QWidget>

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QLabel;
class QPushButton;
class QGroupBox;

/**
 * Dock panel for crop parameters. Live editing targets the current page only
 * when wired; bulk write uses Apply to selection (same pattern as AdjustmentsPanel).
 */
class CropPanel : public QWidget {
    Q_OBJECT
public:
    explicit CropPanel(QWidget *parent = nullptr);

    CropPanelRecipe recipe() const;
    void setRecipe(const CropPanelRecipe &r);
    void setEnabledControls(bool on);
    void setApplyToSelectionEnabled(bool on);
    /** Logical page size for px ↔ normalised sync and status (invalid = unknown). */
    void setPageSize(const QSize &logical);
    void setStatusText(const QString &text);
    BatchTargets::Mode targetMode() const;
    void setTargetMode(BatchTargets::Mode mode);
    int rangeFrom() const;
    int rangeTo() const;
    void setSessionLength(int n);

signals:
    void recipeChanged(const CropPanelRecipe &recipe);
    void applyToSelectionRequested(const CropPanelRecipe &recipe);
    void resetCropOnSelectionRequested();
    void applyToCurrentRequested(const CropPanelRecipe &recipe);
    void resetCropOnCurrentRequested();

private:
    void buildUi();
    void emitIfChanged();
    void syncModeVisibility();
    void onMarginPxChanged();
    void onMarginNormChanged();
    void syncNormFromPx();
    void syncPxFromNorm();

    QComboBox *m_mode = nullptr;
    QGroupBox *m_manualBox = nullptr;
    QSpinBox *m_marginL = nullptr;
    QSpinBox *m_marginT = nullptr;
    QSpinBox *m_marginR = nullptr;
    QSpinBox *m_marginB = nullptr;
    QDoubleSpinBox *m_normL = nullptr;
    QDoubleSpinBox *m_normT = nullptr;
    QDoubleSpinBox *m_normR = nullptr;
    QDoubleSpinBox *m_normB = nullptr;
    QGroupBox *m_autoBox = nullptr;
    QSlider *m_threshold = nullptr;
    QLabel *m_thresholdVal = nullptr;
    QSpinBox *m_extraL = nullptr;
    QSpinBox *m_extraT = nullptr;
    QSpinBox *m_extraR = nullptr;
    QSpinBox *m_extraB = nullptr;
    QLabel *m_status = nullptr;
    QComboBox *m_targetMode = nullptr;
    QSpinBox *m_rangeFrom = nullptr;
    QSpinBox *m_rangeTo = nullptr;
    QGroupBox *m_rangeBox = nullptr;
    QPushButton *m_applyCurrentBtn = nullptr;
    QPushButton *m_resetCurrentBtn = nullptr;
    QPushButton *m_applySelectionBtn = nullptr;
    QPushButton *m_resetSelectionBtn = nullptr;
    QSize m_pageSize;
    bool m_block = false;
    bool m_syncingNorm = false;
};

#endif
