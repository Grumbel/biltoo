// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILMSTRIPDRAGHANDLE_H
#define FILMSTRIPDRAGHANDLE_H

#include <QWidget>

namespace KDDockWidgets::QtWidgets {
class DockWidget;
}

/**
 * Narrow left-edge grip for the filmstrip dock (title bar is collapsed).
 * Mouse press starts a KDDockWidgets title-bar-style drag so the strip can
 * be redocked or floated without a top title bar.
 */
class FilmstripDragHandle final : public QWidget
{
    Q_OBJECT
public:
    explicit FilmstripDragHandle(KDDockWidgets::QtWidgets::DockWidget *dock,
                                 QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    KDDockWidgets::QtWidgets::DockWidget *m_dock = nullptr;
    bool m_hovered = false;
};

#endif
