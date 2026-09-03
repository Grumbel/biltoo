// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspacebackgrounddialog.h"
#include "icons.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const QColor kDefaultColor(0x2a, 0x2a, 0x2a);
const QColor kDefaultColorAlt(0x30, 0x30, 0x30);
} // namespace

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

WorkspaceBackgroundPreview::WorkspaceBackgroundPreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(240, 140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void WorkspaceBackgroundPreview::setBackground(const WorkspaceBackground &bg)
{
    m_bg = bg;
    if (bg.mode != WorkspaceBackgroundMode::ImageTile || bg.imagePath != m_tilePath) {
        m_tile = QPixmap();
        m_tilePath.clear();
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()
        && m_tilePath != bg.imagePath) {
        QPixmap px(bg.imagePath);
        if (!px.isNull()) {
            m_tile = px;
            m_tilePath = bg.imagePath;
        }
    }
    update();
}

void WorkspaceBackgroundPreview::setAppDefaultColors(const QColor &color, const QColor &colorAlt,
                                                     bool checker)
{
    m_appColor = color;
    m_appColorAlt = colorAlt;
    m_appChecker = checker;
    update();
}

void WorkspaceBackgroundPreview::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect r = rect();
    WorkspaceBackgroundMode mode = m_bg.mode;
    QColor a = m_bg.color;
    QColor b = m_bg.colorAlt;

    if (mode == WorkspaceBackgroundMode::AppDefault) {
        mode = m_appChecker ? WorkspaceBackgroundMode::Checkerboard
                            : WorkspaceBackgroundMode::Solid;
        a = m_appColor;
        b = m_appColorAlt;
    }

    if (mode == WorkspaceBackgroundMode::Solid) {
        p.fillRect(r, a.isValid() ? a : QColor(42, 42, 42));
    } else if (mode == WorkspaceBackgroundMode::Checkerboard) {
        const QColor ca = a.isValid() ? a : QColor(42, 42, 42);
        const QColor cb = b.isValid() ? b : ca.lighter(120);
        constexpr int cell = 16;
        for (int y = 0; y < r.height(); y += cell) {
            for (int x = 0; x < r.width(); x += cell) {
                const bool dark = (((x / cell) + (y / cell)) & 1) != 0;
                p.fillRect(QRect(x, y, cell, cell), dark ? ca : cb);
            }
        }
    } else if (mode == WorkspaceBackgroundMode::ImageTile) {
        if (m_tile.isNull() && !m_bg.imagePath.isEmpty()) {
            QPixmap px(m_bg.imagePath);
            if (!px.isNull()) {
                m_tile = px;
                m_tilePath = m_bg.imagePath;
            }
        }
        if (!m_tile.isNull()) {
            const int tw = qMax(1, m_tile.width());
            const int th = qMax(1, m_tile.height());
            for (int y = 0; y < r.height(); y += th) {
                for (int x = 0; x < r.width(); x += tw) {
                    p.drawPixmap(x, y, m_tile);
                }
            }
        } else {
            p.fillRect(r, QColor(42, 42, 42));
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, tr("No image"));
        }
    } else {
        p.fillRect(r, QColor(42, 42, 42));
    }

    p.setPen(QPen(QColor(120, 120, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

WorkspaceBackgroundDialog::WorkspaceBackgroundDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Workspace Background"));
    setModal(true);
    resize(420, 360);

    auto *layout = new QVBoxLayout(this);

    m_preview = new WorkspaceBackgroundPreview(this);
    layout->addWidget(m_preview, 1);

    auto *form = new QFormLayout;

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Application default"), int(WorkspaceBackgroundMode::AppDefault));
    m_modeCombo->addItem(tr("Solid colour"), int(WorkspaceBackgroundMode::Solid));
    m_modeCombo->addItem(tr("Checkerboard"), int(WorkspaceBackgroundMode::Checkerboard));
    m_modeCombo->addItem(tr("Image pattern"), int(WorkspaceBackgroundMode::ImageTile));
    m_modeCombo->setToolTip(
        tr("Application default uses Preferences and is not stored in the project."));
    form->addRow(tr("Mode:"),
                 wrapWithReset(m_modeCombo, [this]() {
                     const int idx = m_modeCombo->findData(int(WorkspaceBackgroundMode::AppDefault));
                     if (idx >= 0) {
                         m_modeCombo->setCurrentIndex(idx);
                     }
                 }));

    m_colorBtn = new QPushButton(this);
    m_colorBtn->setMinimumWidth(100);
    connect(m_colorBtn, &QPushButton::clicked, this, &WorkspaceBackgroundDialog::chooseColor);
    m_colorRow = wrapWithReset(m_colorBtn, [this]() {
        m_color = kDefaultColor;
        styleColorButton(m_colorBtn, m_color);
        updatePreview();
    });
    form->addRow(tr("Colour:"), m_colorRow);

    m_colorAltBtn = new QPushButton(this);
    m_colorAltBtn->setMinimumWidth(100);
    connect(m_colorAltBtn, &QPushButton::clicked, this, &WorkspaceBackgroundDialog::chooseColorAlt);
    m_colorAltRow = wrapWithReset(m_colorAltBtn, [this]() {
        m_colorAlt = kDefaultColorAlt;
        styleColorButton(m_colorAltBtn, m_colorAlt);
        updatePreview();
    });
    form->addRow(tr("Checker colour:"), m_colorAltRow);

    m_imageEdit = new QLineEdit(this);
    m_imageEdit->setPlaceholderText(tr("Path to tile image…"));
    m_browseBtn = new QPushButton(tr("Browse…"), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &WorkspaceBackgroundDialog::browseImage);
    connect(m_imageEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        updatePreview();
    });
    auto *imageInner = new QWidget(this);
    auto *imageLay = new QHBoxLayout(imageInner);
    imageLay->setContentsMargins(0, 0, 0, 0);
    imageLay->setSpacing(4);
    imageLay->addWidget(m_imageEdit, 1);
    imageLay->addWidget(m_browseBtn);
    m_imageRowWidget = wrapWithReset(imageInner, [this]() {
        m_imageEdit->clear();
        updatePreview();
    });
    form->addRow(tr("Image:"), m_imageRowWidget);

    layout->addLayout(form);

    auto *note = new QLabel(
        tr("Custom backgrounds are saved with the project. "
           "Application default follows Preferences."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateControlsEnabled();
        updatePreview();
    });

    styleColorButton(m_colorBtn, m_color);
    styleColorButton(m_colorAltBtn, m_colorAlt);
    updateControlsEnabled();
    updatePreview();
}

void WorkspaceBackgroundDialog::setBackground(const WorkspaceBackground &bg)
{
    m_blockPreviewEmit = true;
    const int idx = m_modeCombo->findData(int(bg.mode));
    if (idx >= 0) {
        m_modeCombo->setCurrentIndex(idx);
    }
    if (bg.color.isValid()) {
        m_color = bg.color;
    }
    if (bg.colorAlt.isValid()) {
        m_colorAlt = bg.colorAlt;
    }
    m_imageEdit->setText(bg.imagePath);
    styleColorButton(m_colorBtn, m_color);
    styleColorButton(m_colorAltBtn, m_colorAlt);
    updateControlsEnabled();
    updatePreview();
    m_blockPreviewEmit = false;
}

WorkspaceBackground WorkspaceBackgroundDialog::background() const
{
    WorkspaceBackground bg;
    bg.mode = WorkspaceBackgroundMode(m_modeCombo->currentData().toInt());
    bg.color = m_color;
    bg.colorAlt = m_colorAlt;
    bg.imagePath = m_imageEdit->text().trimmed();
    return bg;
}

void WorkspaceBackgroundDialog::setAppDefaultColors(const QColor &color, const QColor &colorAlt,
                                                    bool checker)
{
    m_appColor = color;
    m_appColorAlt = colorAlt;
    m_appChecker = checker;
    if (m_preview) {
        m_preview->setAppDefaultColors(color, colorAlt, checker);
    }
}

void WorkspaceBackgroundDialog::updateControlsEnabled()
{
    const auto mode = WorkspaceBackgroundMode(m_modeCombo->currentData().toInt());
    const bool solid = (mode == WorkspaceBackgroundMode::Solid);
    const bool checker = (mode == WorkspaceBackgroundMode::Checkerboard);
    const bool image = (mode == WorkspaceBackgroundMode::ImageTile);
    // Lock fields that the current mode does not use.
    if (m_colorRow) {
        m_colorRow->setEnabled(solid || checker);
    } else {
        m_colorBtn->setEnabled(solid || checker);
    }
    if (m_colorAltRow) {
        m_colorAltRow->setEnabled(checker);
    } else {
        m_colorAltBtn->setEnabled(checker);
    }
    if (m_imageRowWidget) {
        m_imageRowWidget->setEnabled(image);
    } else {
        m_imageEdit->setEnabled(image);
        m_browseBtn->setEnabled(image);
    }
}

void WorkspaceBackgroundDialog::updatePreview()
{
    const WorkspaceBackground bg = background();
    if (m_preview) {
        m_preview->setAppDefaultColors(m_appColor, m_appColorAlt, m_appChecker);
        m_preview->setBackground(bg);
    }
    if (!m_blockPreviewEmit) {
        emit backgroundChanged(bg);
    }
}

void WorkspaceBackgroundDialog::chooseColor()
{
    const QColor c = QColorDialog::getColor(m_color, this, tr("Background colour"));
    if (!c.isValid()) {
        return;
    }
    m_color = c;
    styleColorButton(m_colorBtn, m_color);
    updatePreview();
}

void WorkspaceBackgroundDialog::chooseColorAlt()
{
    const QColor c = QColorDialog::getColor(m_colorAlt, this, tr("Checker colour"));
    if (!c.isValid()) {
        return;
    }
    m_colorAlt = c;
    styleColorButton(m_colorAltBtn, m_colorAlt);
    updatePreview();
}

void WorkspaceBackgroundDialog::browseImage()
{
    const QString start =
        m_imageEdit->text().isEmpty() ? QDir::homePath() : m_imageEdit->text();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Background image"), start,
        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }
    m_imageEdit->setText(path);
    updatePreview();
}

void WorkspaceBackgroundDialog::styleColorButton(QPushButton *btn, const QColor &color)
{
    if (!btn) {
        return;
    }
    const QColor fg = (color.lightness() > 140) ? Qt::black : Qt::white;
    btn->setText(color.name(QColor::HexRgb));
    btn->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; }")
            .arg(color.name(QColor::HexRgb), fg.name(QColor::HexRgb)));
}

QWidget *WorkspaceBackgroundDialog::wrapWithReset(QWidget *field, const std::function<void()> &resetFn)
{
    auto *row = new QWidget(this);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    field->setParent(row);
    lay->addWidget(field, 1);
    auto *btn = new QToolButton(row);
    btn->setAutoRaise(true);
    btn->setIcon(themeIcon(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload));
    btn->setToolTip(tr("Reset to default"));
    btn->setAccessibleName(tr("Reset to default"));
    const int side = qMax(20, field->sizeHint().height());
    btn->setFixedSize(side, side);
    connect(btn, &QToolButton::clicked, this, [resetFn]() { resetFn(); });
    lay->addWidget(btn, 0, Qt::AlignVCenter);
    return row;
}

