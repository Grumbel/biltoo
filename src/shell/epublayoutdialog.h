// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EPUBLAYOUTDIALOG_H
#define EPUBLAYOUTDIALOG_H

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QSpinBox;

/**
 * Cancel/Apply editor for thumtoo //epub: layout profiles.
 * Edits the layout parameter string only; the caller rewrites session URLs.
 */
class EpubLayoutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EpubLayoutDialog(QWidget *parent = nullptr);

    /** Payload after //epub: (e.g. "w=900,h=1350,fs=15,lh=140"). Empty = defaults. */
    void setLayoutParams(const QString &params);
    [[nodiscard]] QString layoutParams() const;

private:
    void resetToDefaults();

    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_heightSpin = nullptr;
    QSpinBox *m_fsSpin = nullptr;
    QSpinBox *m_lhSpin = nullptr;
    QSpinBox *m_colsSpin = nullptr;
    QSpinBox *m_cgapSpin = nullptr;
    QSpinBox *m_mtSpin = nullptr;
    QSpinBox *m_mrSpin = nullptr;
    QSpinBox *m_mbSpin = nullptr;
    QSpinBox *m_mlSpin = nullptr;
    QComboBox *m_alignCombo = nullptr;
    QComboBox *m_fontCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QCheckBox *m_pubCssCheck = nullptr;
};

#endif
