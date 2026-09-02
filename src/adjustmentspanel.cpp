// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "adjustmentspanel.h"
#include "metadatapanel.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QFrame>
#include <QtMath>

VectorScopeWidget::VectorScopeWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(140);
}
void VectorScopeWidget::clear() { m_plot = QImage(); update(); }
void VectorScopeWidget::setFromImage(const QImage &image)
{
    const int size = 256;
    m_plot = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
    m_plot.fill(QColor(20, 20, 24));
    if (image.isNull()) { update(); return; }
    QImage src = image.convertToFormat(QImage::Format_RGB32);
    const int step = qMax(1, qMax(src.width(), src.height()) / 120);
    QPainter p(&m_plot);
    for (int y = 0; y < src.height(); y += step) {
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < src.width(); x += step) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            const float cb = (-0.168736f * r - 0.331264f * g + 0.5f * b) / 128.f;
            const float cr = (0.5f * r - 0.418688f * g - 0.081312f * b) / 128.f;
            const int px = int((cb * 0.45f + 0.5f) * (size - 1));
            const int py = int((0.5f - cr * 0.45f) * (size - 1));
            if (px >= 0 && px < size && py >= 0 && py < size) {
                p.setPen(QColor(r, g, b, 180));
                p.drawPoint(px, py);
            }
        }
    }
    p.setPen(QColor(80, 80, 90));
    p.drawEllipse(QRect(8, 8, size - 16, size - 16));
    p.drawLine(size / 2, 0, size / 2, size);
    p.drawLine(0, size / 2, size, size / 2);
    update();
}
void VectorScopeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 24));
    // Keep a square plot so the scope stays circular.
    const int side = qMin(width(), height());
    const QRect square((width() - side) / 2, (height() - side) / 2, side, side);
    if (m_plot.isNull()) {
        p.setPen(QColor(140, 140, 150));
        p.drawText(square, Qt::AlignCenter, tr("No scope"));
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(square.adjusted(1, 1, -1, -1));
    p.setClipPath(clip);
    p.drawImage(square, m_plot);
    p.setClipping(false);
    p.setPen(QPen(QColor(80, 80, 90), 1));
    p.drawEllipse(square.adjusted(1, 1, -1, -1));
}

AdjustmentsPanel::AdjustmentsPanel(QWidget *parent) : QWidget(parent) { buildUi(); }

void AdjustmentsPanel::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *gradeBox = new QGroupBox(tr("Colour"), inner);
    auto *form = new QFormLayout(gradeBox);
    auto addRow = [&](const QString &name, QSlider **slider, QLabel **val, int min, int max, int value) {
        *slider = new QSlider(Qt::Horizontal, gradeBox);
        (*slider)->setRange(min, max);
        (*slider)->setValue(value);
        *val = new QLabel(QString::number(value), gradeBox);
        (*val)->setMinimumWidth(36);
        (*val)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *row = new QWidget(gradeBox);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(*slider, 1);
        hl->addWidget(*val);
        form->addRow(name, row);
        connect(*slider, &QSlider::valueChanged, this, [this, valueLabel = *val](int v) {
            if (valueLabel) {
                valueLabel->setText(QString::number(v));
            }
            emitIfChanged();
        });
    };
    addRow(tr("Brightness"), &m_brightness, &m_brightnessVal, -100, 100, 0);
    addRow(tr("Contrast"), &m_contrast, &m_contrastVal, 0, 200, 100);
    addRow(tr("Saturation"), &m_saturation, &m_saturationVal, 0, 200, 100);
    addRow(tr("Hue"), &m_hue, &m_hueVal, -180, 180, 0);
    m_gamma = new QSlider(Qt::Horizontal, gradeBox);
    m_gamma->setRange(10, 300);
    m_gamma->setValue(100);
    m_gammaVal = new QLabel(QStringLiteral("1.00"), gradeBox);
    m_gammaVal->setMinimumWidth(36);
    m_gammaVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        auto *row = new QWidget(gradeBox);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(m_gamma, 1);
        hl->addWidget(m_gammaVal);
        form->addRow(tr("Gamma"), row);
    }
    connect(m_gamma, &QSlider::valueChanged, this, [this](int v) {
        m_gammaVal->setText(QString::number(v / 100.0, 'f', 2));
        emitIfChanged();
    });
    m_resetBtn = new QPushButton(tr("Reset"), gradeBox);
    form->addRow(QString(), m_resetBtn);
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        setAdjustments(ColorAdjustments{});
        emit resetRequested();
        emit adjustmentsChanged(adjustments());
    });
    layout->addWidget(gradeBox);
    auto *histBox = new QGroupBox(tr("Histogram"), inner);
    auto *histLay = new QVBoxLayout(histBox);
    m_histogram = new ImageHistogramWidget(histBox);
    histLay->addWidget(m_histogram);
    layout->addWidget(histBox);
    auto *scopeBox = new QGroupBox(tr("Vectorscope"), inner);
    auto *scopeLay = new QVBoxLayout(scopeBox);
    m_scope = new VectorScopeWidget(scopeBox);
    scopeLay->addWidget(m_scope);
    layout->addWidget(scopeBox);
    layout->addStretch(1);
    scroll->setWidget(inner);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

ColorAdjustments AdjustmentsPanel::adjustments() const
{
    ColorAdjustments a;
    a.brightness = m_brightness ? m_brightness->value() : 0;
    a.contrast = m_contrast ? m_contrast->value() : 100;
    a.saturation = m_saturation ? m_saturation->value() : 100;
    a.hue = m_hue ? m_hue->value() : 0;
    a.gamma = m_gamma ? (m_gamma->value() / 100.0) : 1.0;
    return a;
}

void AdjustmentsPanel::setAdjustments(const ColorAdjustments &adj)
{
    m_block = true;
    if (m_brightness) m_brightness->setValue(adj.brightness);
    if (m_contrast) m_contrast->setValue(adj.contrast);
    if (m_saturation) m_saturation->setValue(adj.saturation);
    if (m_hue) m_hue->setValue(adj.hue);
    if (m_gamma) m_gamma->setValue(int(qBound(10.0, adj.gamma * 100.0, 300.0)));
    m_block = false;
}

void AdjustmentsPanel::emitIfChanged()
{
    if (!m_block) emit adjustmentsChanged(adjustments());
}

void AdjustmentsPanel::setPreviewImage(const QImage &image)
{
    if (m_histogram) m_histogram->setFromImage(image);
    if (m_scope) m_scope->setFromImage(image);
}

void AdjustmentsPanel::clearPreview()
{
    if (m_histogram) m_histogram->clear();
    if (m_scope) m_scope->clear();
}

void AdjustmentsPanel::setEnabledControls(bool on) { setEnabled(on); }
