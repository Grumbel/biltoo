// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ADJUSTMENTSPANEL_H
#define ADJUSTMENTSPANEL_H
#include "coloradjust.h"
#include <QWidget>
class QLabel;
class QSlider;
class QPushButton;
class ImageHistogramWidget;
class VectorScopeWidget;
class AdjustmentsPanel : public QWidget {
    Q_OBJECT
public:
    explicit AdjustmentsPanel(QWidget *parent = nullptr);
    void setAdjustments(const ColorAdjustments &adj);
    ColorAdjustments adjustments() const;
    void setPreviewImage(const QImage &image);
    void clearPreview();
    void setEnabledControls(bool on);
signals:
    void adjustmentsChanged(const ColorAdjustments &adj);
    void resetRequested();
private:
    void buildUi();
    void emitIfChanged();
    QSlider *m_brightness = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_hue = nullptr;
    QSlider *m_gamma = nullptr;
    QLabel *m_brightnessVal = nullptr;
    QLabel *m_contrastVal = nullptr;
    QLabel *m_saturationVal = nullptr;
    QLabel *m_hueVal = nullptr;
    QLabel *m_gammaVal = nullptr;
    QPushButton *m_resetBtn = nullptr;
    ImageHistogramWidget *m_histogram = nullptr;
    VectorScopeWidget *m_scope = nullptr;
    bool m_block = false;
};
class VectorScopeWidget : public QWidget {
    Q_OBJECT
public:
    explicit VectorScopeWidget(QWidget *parent = nullptr);
    void clear();
    void setFromImage(const QImage &image);
protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override { return QSize(200, 200); }
    QSize minimumSizeHint() const override { return QSize(120, 120); }
private:
    QImage m_plot;
};
#endif
