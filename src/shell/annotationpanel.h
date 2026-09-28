// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONPANEL_H
#define ANNOTATIONPANEL_H

#include <QColor>
#include <QWidget>

class QLabel;
class QSlider;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QButtonGroup;

/**
 * Side panel for annotation colour, stroke width, and layer visibility.
 * Does not own tools (toolbar / Image menu); mirrors AnnotationController state.
 */
class AnnotationPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AnnotationPanel(QWidget *parent = nullptr);

    void setColor(const QColor &c);
    QColor color() const { return m_color; }

    void setWidth(qreal w);
    qreal width() const { return m_width; }

    void setLayerVisible(bool on);
    bool layerVisible() const;

    /** Optional status line (e.g. active tool name). */
    void setStatusText(const QString &text);

signals:
    void colorChanged(const QColor &c);
    void widthChanged(qreal w);
    void layerVisibleChanged(bool on);

private:
    void rebuildSwatch();
    void emitColor(const QColor &c);
    void onWidthSlider(int value);
    void onWidthSpin(double value);
    void pickCustomColor();

    QColor m_color = QColor(246, 211, 45);
    qreal m_width = 18.0;
    bool m_block = false;

    QLabel *m_swatch = nullptr;
    QLabel *m_status = nullptr;
    QSlider *m_widthSlider = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QCheckBox *m_visibleCheck = nullptr;
    QPushButton *m_customColorBtn = nullptr;
};

#endif // ANNOTATIONPANEL_H
