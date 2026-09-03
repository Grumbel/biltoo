// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef METADATAPANEL_H
#define METADATAPANEL_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QImage>

class QTreeWidget;
class QLabel;

/** Compact luminance / RGB histogram for the metadata side panel. */
class ImageHistogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageHistogramWidget(QWidget *parent = nullptr);
    void clear();
    void setFromImage(const QImage &image);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override { return QSize(200, 72); }
    QSize minimumSizeHint() const override { return QSize(120, 56); }

private:
    QVector<int> m_luma;
    QVector<int> m_r;
    QVector<int> m_g;
    QVector<int> m_b;
    int m_peak = 0;
};

/** Colour table swatches for indexed images (e.g. palette PNG/GIF). */
class ImagePaletteWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImagePaletteWidget(QWidget *parent = nullptr);
    void clear();
    void setColors(const QVector<QColor> &colors);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(120, 24); }

private:
    QVector<QColor> m_colors;
};

/**
 * Side panel listing file info, image structure, histogram, palette, and
 * embedded metadata (Exif/IPTC/XMP via libexiv2 when available).
 */
/** Quiet or enable verbose logs from libexiv2 (call once from main). */
void configureMetadataLibraryLogging(bool verbose);

class MetadataPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MetadataPanel(QWidget *parent = nullptr);

    void clear();
    /**
     * Schedule metadata refresh for @p path.
     * Heavy work (decode, histogram, Exiv2) is debounced; rapid path changes
     * cancel the previous pending update. Pass @p decodedHint when the
     * canvas already holds pixels for this path so structure/histogram need
     * not decode again.
     */
    void setImagePath(const QString &path, const QImage &decodedHint = QImage());

private:
    void addRow(const QString &key, const QString &value);
    void fillImageAnalysis(const QString &path, const QImage &decodedHint);
    void applyPendingPath();

    QLabel *m_header = nullptr;
    ImageHistogramWidget *m_histogram = nullptr;
    QLabel *m_histogramLabel = nullptr;
    ImagePaletteWidget *m_palette = nullptr;
    QLabel *m_paletteLabel = nullptr;
    QTreeWidget *m_tree = nullptr;

    QString m_pendingPath;
    QImage m_pendingDecoded;
    class QTimer *m_applyTimer = nullptr;
};

#endif // METADATAPANEL_H
