// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "epublayoutdialog.h"

#include <thumtoo/uri.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

void fillAlignCombo(QComboBox *c)
{
    c->addItem(QObject::tr("Publisher default"), QStringLiteral("publisher"));
    c->addItem(QObject::tr("Left"), QStringLiteral("left"));
    c->addItem(QObject::tr("Right"), QStringLiteral("right"));
    c->addItem(QObject::tr("Center"), QStringLiteral("center"));
    c->addItem(QObject::tr("Justify"), QStringLiteral("justify"));
}

void fillFontCombo(QComboBox *c)
{
    c->addItem(QObject::tr("Publisher"), QStringLiteral("publisher"));
    c->addItem(QObject::tr("Serif"), QStringLiteral("serif"));
    c->addItem(QObject::tr("Sans"), QStringLiteral("sans"));
    c->addItem(QObject::tr("Mono"), QStringLiteral("mono"));
}

void fillThemeCombo(QComboBox *c)
{
    c->addItem(QObject::tr("Day"), QStringLiteral("day"));
    c->addItem(QObject::tr("Sepia"), QStringLiteral("sepia"));
    c->addItem(QObject::tr("Night"), QStringLiteral("night"));
}

void selectData(QComboBox *c, const QString &data)
{
    const int i = c->findData(data);
    c->setCurrentIndex(i >= 0 ? i : 0);
}

thumtoo::EpubLayout layoutFromWidgets(
    QSpinBox *w, QSpinBox *h, QSpinBox *fs, QSpinBox *lh, QSpinBox *cols, QSpinBox *cgap,
    QSpinBox *mt, QSpinBox *mr, QSpinBox *mb, QSpinBox *ml,
    QComboBox *align, QComboBox *font, QComboBox *theme, QCheckBox *pubcss)
{
    thumtoo::EpubLayout L;
    L.width_px = w->value();
    L.height_px = h->value();
    L.fs_pt = fs->value();
    L.lh_percent = lh->value();
    L.cols = cols->value();
    L.cgap_px = cgap->value();
    L.mt_px = mt->value();
    L.mr_px = mr->value();
    L.mb_px = mb->value();
    L.ml_px = ml->value();

    const QString a = align->currentData().toString();
    if (a == QLatin1String("left")) {
        L.align = thumtoo::EpubAlign::Left;
    } else if (a == QLatin1String("right")) {
        L.align = thumtoo::EpubAlign::Right;
    } else if (a == QLatin1String("center")) {
        L.align = thumtoo::EpubAlign::Center;
    } else if (a == QLatin1String("justify")) {
        L.align = thumtoo::EpubAlign::Justify;
    } else {
        L.align = thumtoo::EpubAlign::Publisher;
    }

    const QString f = font->currentData().toString();
    if (f == QLatin1String("serif")) {
        L.font = thumtoo::EpubFontFamily::Serif;
    } else if (f == QLatin1String("sans")) {
        L.font = thumtoo::EpubFontFamily::Sans;
    } else if (f == QLatin1String("mono")) {
        L.font = thumtoo::EpubFontFamily::Mono;
    } else {
        L.font = thumtoo::EpubFontFamily::Publisher;
    }

    const QString th = theme->currentData().toString();
    if (th == QLatin1String("sepia")) {
        L.theme = thumtoo::EpubTheme::Sepia;
    } else if (th == QLatin1String("night")) {
        L.theme = thumtoo::EpubTheme::Night;
    } else {
        L.theme = thumtoo::EpubTheme::Day;
    }

    L.use_document_css = pubcss->isChecked();
    return L;
}

void applyLayoutToWidgets(const thumtoo::EpubLayout &L,
                          QSpinBox *w, QSpinBox *h, QSpinBox *fs, QSpinBox *lh,
                          QSpinBox *cols, QSpinBox *cgap,
                          QSpinBox *mt, QSpinBox *mr, QSpinBox *mb, QSpinBox *ml,
                          QComboBox *align, QComboBox *font, QComboBox *theme,
                          QCheckBox *pubcss)
{
    w->setValue(L.width_px > 0 ? L.width_px : 900);
    h->setValue(L.height_px > 0 ? L.height_px : 1350);
    fs->setValue(L.fs_pt > 0 ? L.fs_pt : 15);
    lh->setValue(L.lh_percent > 0 ? L.lh_percent : 140);
    cols->setValue(L.cols >= 1 ? L.cols : 1);
    cgap->setValue(L.cgap_px >= 0 ? L.cgap_px : 0);
    mt->setValue(L.mt_px);
    mr->setValue(L.mr_px);
    mb->setValue(L.mb_px);
    ml->setValue(L.ml_px);

    QString a = QStringLiteral("publisher");
    switch (L.align) {
    case thumtoo::EpubAlign::Left: a = QStringLiteral("left"); break;
    case thumtoo::EpubAlign::Right: a = QStringLiteral("right"); break;
    case thumtoo::EpubAlign::Center: a = QStringLiteral("center"); break;
    case thumtoo::EpubAlign::Justify: a = QStringLiteral("justify"); break;
    default: break;
    }
    selectData(align, a);

    QString f = QStringLiteral("publisher");
    switch (L.font) {
    case thumtoo::EpubFontFamily::Serif: f = QStringLiteral("serif"); break;
    case thumtoo::EpubFontFamily::Sans: f = QStringLiteral("sans"); break;
    case thumtoo::EpubFontFamily::Mono: f = QStringLiteral("mono"); break;
    default: break;
    }
    selectData(font, f);

    QString th = QStringLiteral("day");
    switch (L.theme) {
    case thumtoo::EpubTheme::Sepia: th = QStringLiteral("sepia"); break;
    case thumtoo::EpubTheme::Night: th = QStringLiteral("night"); break;
    default: break;
    }
    selectData(theme, th);

    pubcss->setChecked(L.use_document_css);
}

QSpinBox *makeSpin(QWidget *parent, int min, int max, int step = 1)
{
    auto *s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    return s;
}

} // namespace

EpubLayoutDialog::EpubLayoutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("EPUB Layout"));
    setModal(true);

    m_widthSpin = makeSpin(this, 200, 4000, 10);
    m_heightSpin = makeSpin(this, 200, 6000, 10);
    m_fsSpin = makeSpin(this, 6, 48);
    m_lhSpin = makeSpin(this, 50, 400, 5);
    m_lhSpin->setSuffix(QStringLiteral(" %"));
    m_colsSpin = makeSpin(this, 1, 6);
    m_cgapSpin = makeSpin(this, 0, 400, 4);
    m_mtSpin = makeSpin(this, 0, 400, 4);
    m_mrSpin = makeSpin(this, 0, 400, 4);
    m_mbSpin = makeSpin(this, 0, 400, 4);
    m_mlSpin = makeSpin(this, 0, 400, 4);

    m_alignCombo = new QComboBox(this);
    fillAlignCombo(m_alignCombo);
    m_fontCombo = new QComboBox(this);
    fillFontCombo(m_fontCombo);
    m_themeCombo = new QComboBox(this);
    fillThemeCombo(m_themeCombo);
    m_pubCssCheck = new QCheckBox(tr("Use publication CSS"), this);
    m_pubCssCheck->setChecked(true);

    auto *pageBox = new QGroupBox(tr("Page"), this);
    auto *pageForm = new QFormLayout(pageBox);
    pageForm->addRow(tr("Width (px)"), m_widthSpin);
    pageForm->addRow(tr("Height (px)"), m_heightSpin);
    pageForm->addRow(tr("Font size (pt)"), m_fsSpin);
    pageForm->addRow(tr("Line height"), m_lhSpin);
    pageForm->addRow(tr("Columns"), m_colsSpin);
    pageForm->addRow(tr("Column gap (px)"), m_cgapSpin);

    auto *marginBox = new QGroupBox(tr("Margins (px)"), this);
    auto *marginForm = new QFormLayout(marginBox);
    marginForm->addRow(tr("Top"), m_mtSpin);
    marginForm->addRow(tr("Right"), m_mrSpin);
    marginForm->addRow(tr("Bottom"), m_mbSpin);
    marginForm->addRow(tr("Left"), m_mlSpin);

    auto *styleBox = new QGroupBox(tr("Style"), this);
    auto *styleForm = new QFormLayout(styleBox);
    styleForm->addRow(tr("Alignment"), m_alignCombo);
    styleForm->addRow(tr("Font"), m_fontCombo);
    styleForm->addRow(tr("Theme"), m_themeCombo);
    styleForm->addRow(m_pubCssCheck);

    auto *hint = new QLabel(
        tr("Changes rewrite //epub: in the session path and reload pages. "
           "Apply is not live — reflow is expensive."),
        this);
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    auto *defaultsBtn = buttons->addButton(tr("Defaults"), QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(defaultsBtn, &QPushButton::clicked, this, &EpubLayoutDialog::resetToDefaults);

    auto *root = new QVBoxLayout(this);
    root->addWidget(pageBox);
    root->addWidget(marginBox);
    root->addWidget(styleBox);
    root->addWidget(hint);
    root->addWidget(buttons);

    resetToDefaults();
    resize(420, minimumSizeHint().height());
}

void EpubLayoutDialog::setLayoutParams(const QString &params)
{
    thumtoo::EpubLayout L = params.isEmpty()
                                ? thumtoo::default_epub_layout()
                                : thumtoo::parse_epub_layout_params(params.toStdString());
    applyLayoutToWidgets(L, m_widthSpin, m_heightSpin, m_fsSpin, m_lhSpin, m_colsSpin,
                         m_cgapSpin, m_mtSpin, m_mrSpin, m_mbSpin, m_mlSpin, m_alignCombo,
                         m_fontCombo, m_themeCombo, m_pubCssCheck);
}

QString EpubLayoutDialog::layoutParams() const
{
    const thumtoo::EpubLayout L = layoutFromWidgets(
        m_widthSpin, m_heightSpin, m_fsSpin, m_lhSpin, m_colsSpin, m_cgapSpin, m_mtSpin,
        m_mrSpin, m_mbSpin, m_mlSpin, m_alignCombo, m_fontCombo, m_themeCombo,
        m_pubCssCheck);
    return QString::fromStdString(thumtoo::format_epub_layout_params(L));
}

void EpubLayoutDialog::resetToDefaults()
{
    setLayoutParams(QString());
}
