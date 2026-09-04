// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadatapanel.h"
#include "archivepath.h"
#include "imageloader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPaintEvent>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#ifdef BILTOO_HAVE_EXIV2
#  include <exiv2/exiv2.hpp>
#  include <string>
#  include <atomic>
#  include <cstdio>
#  include <mutex>
#endif

#ifdef BILTOO_HAVE_EXIV2
namespace biltoo_exivlog {
std::atomic<bool> g_exivVerbose{false};
thread_local const char *g_exivCurrentPath = nullptr;

void exiv2LogHandler(int level, const char *msg)
{
    // Exiv2 levels: mute=0, error=1, warn=2, info=3, debug=4.
    // Default: only real errors. --debug also shows warnings (IFD oddities, etc.).
    if (!g_exivVerbose.load(std::memory_order_relaxed)
        && level > static_cast<int>(Exiv2::LogMsg::error)) {
        return;
    }
    if (!msg) {
        return;
    }
    if (g_exivCurrentPath && g_exivCurrentPath[0] != '\0') {
        std::fprintf(stderr, "biltoo:exiv2: %s: %s\n", g_exivCurrentPath, msg);
    } else {
        std::fprintf(stderr, "biltoo:exiv2: %s\n", msg);
    }
}

void ensureExiv2LogHandler()
{
    static std::once_flag once;
    std::call_once(once, [] {
        Exiv2::LogMsg::setHandler(exiv2LogHandler);
        // Deliver warn/debug to the handler; it filters when not verbose.
        Exiv2::LogMsg::setLevel(Exiv2::LogMsg::debug);
    });
}

struct Exiv2PathScope {
    explicit Exiv2PathScope(const QString &path)
        : m_utf8(path.toUtf8())
    {
        g_exivCurrentPath = m_utf8.constData();
    }
    ~Exiv2PathScope()
    {
        g_exivCurrentPath = nullptr;
    }
    QByteArray m_utf8;
};
} // namespace biltoo_exivlog
#endif

void configureMetadataLibraryLogging(bool verbose)
{
#ifdef BILTOO_HAVE_EXIV2
    biltoo_exivlog::g_exivVerbose.store(verbose, std::memory_order_relaxed);
    biltoo_exivlog::ensureExiv2LogHandler();
#else
    Q_UNUSED(verbose);
#endif
}

namespace {

QTreeWidgetItem *ensureGroup(QTreeWidget *tree, const QString &title)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = tree->topLevelItem(i);
        if (it && it->text(0) == title) {
            return it;
        }
    }
    auto *group = new QTreeWidgetItem(tree);
    group->setText(0, title);
    group->setFirstColumnSpanned(true);
    QFont f = group->font(0);
    f.setBold(true);
    group->setFont(0, f);
    group->setExpanded(true);
    return group;
}

void addChildRow(QTreeWidgetItem *parent, const QString &key, const QString &value)
{
    if (!parent || value.isEmpty()) {
        return;
    }
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, key);
    item->setText(1, value);
    item->setToolTip(0, key);
    item->setToolTip(1, value);
}

/** Human-friendly labels for frequently useful camera tags. */
QString friendlyExifLabel(const QString &key)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("Exif.Image.Make"), QObject::tr("Camera make")},
        {QStringLiteral("Exif.Image.Model"), QObject::tr("Camera model")},
        {QStringLiteral("Exif.Image.DateTime"), QObject::tr("Date/time")},
        {QStringLiteral("Exif.Image.Orientation"), QObject::tr("Orientation")},
        {QStringLiteral("Exif.Image.Software"), QObject::tr("Software")},
        {QStringLiteral("Exif.Image.Artist"), QObject::tr("Artist")},
        {QStringLiteral("Exif.Image.Copyright"), QObject::tr("Copyright")},
        {QStringLiteral("Exif.Photo.DateTimeOriginal"), QObject::tr("Taken")},
        {QStringLiteral("Exif.Photo.DateTimeDigitized"), QObject::tr("Digitized")},
        {QStringLiteral("Exif.Photo.ExposureTime"), QObject::tr("Exposure")},
        {QStringLiteral("Exif.Photo.FNumber"), QObject::tr("Aperture")},
        {QStringLiteral("Exif.Photo.ISOSpeedRatings"), QObject::tr("ISO")},
        {QStringLiteral("Exif.Photo.PhotographicSensitivity"), QObject::tr("ISO")},
        {QStringLiteral("Exif.Photo.FocalLength"), QObject::tr("Focal length")},
        {QStringLiteral("Exif.Photo.FocalLengthIn35mmFilm"), QObject::tr("Focal length (35mm eq.)")},
        {QStringLiteral("Exif.Photo.ExposureProgram"), QObject::tr("Exposure program")},
        {QStringLiteral("Exif.Photo.MeteringMode"), QObject::tr("Metering")},
        {QStringLiteral("Exif.Photo.Flash"), QObject::tr("Flash")},
        {QStringLiteral("Exif.Photo.WhiteBalance"), QObject::tr("White balance")},
        {QStringLiteral("Exif.Photo.LensModel"), QObject::tr("Lens")},
        {QStringLiteral("Exif.Photo.LensSpecification"), QObject::tr("Lens specification")},
        {QStringLiteral("Exif.Photo.ExposureBiasValue"), QObject::tr("Exposure bias")},
        {QStringLiteral("Exif.GPSInfo.GPSLatitude"), QObject::tr("GPS latitude")},
        {QStringLiteral("Exif.GPSInfo.GPSLongitude"), QObject::tr("GPS longitude")},
        {QStringLiteral("Exif.GPSInfo.GPSAltitude"), QObject::tr("GPS altitude")},
    };
    return labels.value(key);
}

#ifdef BILTOO_HAVE_EXIV2
bool loadExiv2Metadata(QTreeWidget *tree, const QString &path)
{
    try {
        biltoo_exivlog::ensureExiv2LogHandler();
        biltoo_exivlog::Exiv2PathScope pathScope(path);
        auto image = Exiv2::ImageFactory::open(path.toStdString());
        if (!image.get()) {
            return false;
        }
        image->readMetadata();

        const Exiv2::ExifData &exif = image->exifData();
        const Exiv2::IptcData &iptc = image->iptcData();
        const Exiv2::XmpData &xmp = image->xmpData();

        if (exif.empty() && iptc.empty() && xmp.empty()) {
            return false;
        }

        // Prefer a short “Summary” of camera fields
        static const char *const kSummaryKeys[] = {
            "Exif.Image.Make",
            "Exif.Image.Model",
            "Exif.Photo.DateTimeOriginal",
            "Exif.Image.DateTime",
            "Exif.Photo.ExposureTime",
            "Exif.Photo.FNumber",
            "Exif.Photo.ISOSpeedRatings",
            "Exif.Photo.PhotographicSensitivity",
            "Exif.Photo.FocalLength",
            "Exif.Photo.LensModel",
            "Exif.Photo.Flash",
            "Exif.Photo.WhiteBalance",
            "Exif.GPSInfo.GPSLatitude",
            "Exif.GPSInfo.GPSLongitude",
        };

        QTreeWidgetItem *summary = nullptr;
        QSet<QString> seenSummary;
        for (const char *ckey : kSummaryKeys) {
            const std::string key(ckey);
            auto it = exif.findKey(Exiv2::ExifKey(key));
            if (it == exif.end()) {
                continue;
            }
            const QString qkey = QString::fromStdString(key);
            if (seenSummary.contains(qkey)) {
                continue;
            }
            seenSummary.insert(qkey);
            if (!summary) {
                summary = ensureGroup(tree, QObject::tr("Summary"));
            }
            QString label = friendlyExifLabel(qkey);
            if (label.isEmpty()) {
                label = qkey.section(QLatin1Char('.'), -1);
            }
            addChildRow(summary, label, QString::fromStdString(it->print()));
        }

        if (!exif.empty()) {
            QTreeWidgetItem *group = ensureGroup(tree, QObject::tr("Exif"));
            for (auto it = exif.begin(); it != exif.end(); ++it) {
                const QString key = QString::fromStdString(it->key());
                if (key.contains(QLatin1String("MakerNote"), Qt::CaseInsensitive)
                    || key.contains(QLatin1String("Thumbnail"), Qt::CaseInsensitive)
                    || key.endsWith(QLatin1String("Padding"))) {
                    continue;
                }
                if (it->size() > 1024 && (it->typeId() == Exiv2::undefined
                                          || it->typeId() == Exiv2::unsignedByte)) {
                    continue;
                }
                QString label = friendlyExifLabel(key);
                if (label.isEmpty()) {
                    label = key;
                } else {
                    label = QStringLiteral("%1").arg(label);
                    // Keep technical key in tooltip only via child
                }
                QString value;
                try {
                    value = QString::fromStdString(it->print());
                } catch (const Exiv2::Error &) {
                    value = QString::fromStdString(it->toString());
                }
                if (label != key) {
                    auto *item = new QTreeWidgetItem(group);
                    item->setText(0, label);
                    item->setText(1, value);
                    item->setToolTip(0, key);
                    item->setToolTip(1, value);
                } else {
                    addChildRow(group, label, value);
                }
            }
        }

        if (!iptc.empty()) {
            QTreeWidgetItem *group = ensureGroup(tree, QObject::tr("IPTC"));
            for (auto it = iptc.begin(); it != iptc.end(); ++it) {
                const QString key = QString::fromStdString(it->key());
                addChildRow(group, key, QString::fromStdString(it->print()));
            }
        }

        if (!xmp.empty()) {
            QTreeWidgetItem *group = ensureGroup(tree, QObject::tr("XMP"));
            for (auto it = xmp.begin(); it != xmp.end(); ++it) {
                const QString key = QString::fromStdString(it->key());
                if (key.contains(QLatin1String("Thumb"), Qt::CaseInsensitive)) {
                    continue;
                }
                try {
                    addChildRow(group, key, QString::fromStdString(it->print()));
                } catch (const Exiv2::Error &) {
                    addChildRow(group, key, QString::fromStdString(it->toString()));
                }
            }
        }

        return true;
    } catch (const Exiv2::Error &) {
        return false;
    } catch (const std::exception &) {
        return false;
    }
}
#endif

} // namespace



QString imageFormatName(QImage::Format fmt)
{
    switch (fmt) {
    case QImage::Format_Invalid: return QObject::tr("Invalid");
    case QImage::Format_Mono: return QObject::tr("Mono");
    case QImage::Format_MonoLSB: return QObject::tr("Mono (LSB)");
    case QImage::Format_Indexed8: return QObject::tr("Indexed 8-bit");
    case QImage::Format_RGB32: return QObject::tr("RGB32");
    case QImage::Format_ARGB32: return QObject::tr("ARGB32");
    case QImage::Format_ARGB32_Premultiplied: return QObject::tr("ARGB32 premultiplied");
    case QImage::Format_RGB16: return QObject::tr("RGB16");
    case QImage::Format_ARGB8565_Premultiplied: return QObject::tr("ARGB8565");
    case QImage::Format_RGB666: return QObject::tr("RGB666");
    case QImage::Format_ARGB6666_Premultiplied: return QObject::tr("ARGB6666");
    case QImage::Format_RGB555: return QObject::tr("RGB555");
    case QImage::Format_ARGB8555_Premultiplied: return QObject::tr("ARGB8555");
    case QImage::Format_RGB888: return QObject::tr("RGB888");
    case QImage::Format_RGB444: return QObject::tr("RGB444");
    case QImage::Format_ARGB4444_Premultiplied: return QObject::tr("ARGB4444");
    case QImage::Format_RGBX8888: return QObject::tr("RGBX8888");
    case QImage::Format_RGBA8888: return QObject::tr("RGBA8888");
    case QImage::Format_RGBA8888_Premultiplied: return QObject::tr("RGBA8888 premultiplied");
    case QImage::Format_BGR30: return QObject::tr("BGR30");
    case QImage::Format_A2BGR30_Premultiplied: return QObject::tr("A2BGR30");
    case QImage::Format_RGB30: return QObject::tr("RGB30");
    case QImage::Format_A2RGB30_Premultiplied: return QObject::tr("A2RGB30");
    case QImage::Format_Alpha8: return QObject::tr("Alpha8");
    case QImage::Format_Grayscale8: return QObject::tr("Grayscale 8-bit");
    case QImage::Format_Grayscale16: return QObject::tr("Grayscale 16-bit");
    case QImage::Format_RGBX64: return QObject::tr("RGBX64");
    case QImage::Format_RGBA64: return QObject::tr("RGBA64");
    case QImage::Format_RGBA64_Premultiplied: return QObject::tr("RGBA64 premultiplied");
    default: return QObject::tr("Format %1").arg(static_cast<int>(fmt));
    }
}


ImageHistogramWidget::ImageHistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    clear();
}

void ImageHistogramWidget::clear()
{
    m_luma = QVector<int>(256, 0);
    m_r = QVector<int>(256, 0);
    m_g = QVector<int>(256, 0);
    m_b = QVector<int>(256, 0);
    m_peak = 0;
    update();
}

void ImageHistogramWidget::setFromImage(const QImage &image)
{
    clear();
    if (image.isNull()) {
        return;
    }
    QImage src = image;
    if (src.width() > 384 || src.height() > 384) {
        src = src.scaled(384, 384, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    src = src.convertToFormat(QImage::Format_RGB32);

    const int w = src.width();
    const int h = src.height();
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = line[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const int yv = (299 * r + 587 * g + 114 * b) / 1000;
            ++m_r[r];
            ++m_g[g];
            ++m_b[b];
            ++m_luma[qBound(0, yv, 255)];
        }
    }
    m_peak = 1;
    for (int i = 0; i < 256; ++i) {
        m_peak = qMax(m_peak, m_luma[i]);
        m_peak = qMax(m_peak, m_r[i]);
        m_peak = qMax(m_peak, m_g[i]);
        m_peak = qMax(m_peak, m_b[i]);
    }
    update();
}

void ImageHistogramWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    if (m_peak <= 0) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, tr("No histogram"));
        return;
    }

    const int w = width();
    const int h = height();
    auto drawChannel = [&](const QVector<int> &bins, const QColor &color) {
        p.setPen(QPen(color, 1));
        for (int i = 0; i < 256; ++i) {
            const qreal x0 = (static_cast<qreal>(i) / 256.0) * w;
            const qreal barH = (static_cast<qreal>(bins[i]) / static_cast<qreal>(m_peak)) * (h - 2);
            p.drawLine(QPointF(x0, h - 1), QPointF(x0, h - 1 - barH));
        }
    };
    drawChannel(m_luma, QColor(220, 220, 220));
    drawChannel(m_r, QColor(220, 60, 60));
    drawChannel(m_g, QColor(60, 200, 80));
    drawChannel(m_b, QColor(60, 120, 220));
    p.setPen(QColor(60, 60, 60));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

ImagePaletteWidget::ImagePaletteWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ImagePaletteWidget::clear()
{
    m_colors.clear();
    setVisible(false);
    updateGeometry();
    update();
}

void ImagePaletteWidget::setColors(const QVector<QColor> &colors)
{
    m_colors = colors;
    setVisible(!m_colors.isEmpty());
    updateGeometry();
    update();
}

QSize ImagePaletteWidget::sizeHint() const
{
    if (m_colors.isEmpty()) {
        return QSize(120, 0);
    }
    const int rows = qMin(4, (m_colors.size() + 15) / 16);
    return QSize(200, rows * 14 + 4);
}

void ImagePaletteWidget::paintEvent(QPaintEvent *)
{
    if (m_colors.isEmpty()) {
        return;
    }
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    const int maxShow = qMin(m_colors.size(), 64);
    const int cols = 16;
    const int cell = qMax(8, (width() - 4) / cols);
    for (int i = 0; i < maxShow; ++i) {
        const int row = i / cols;
        const int col = i % cols;
        const QRect r(2 + col * cell, 2 + row * cell, cell - 1, cell - 1);
        p.fillRect(r, m_colors.at(i));
        p.setPen(QColor(0, 0, 0, 80));
        p.drawRect(r);
    }
}

MetadataPanel::MetadataPanel(QWidget *parent)
    : QWidget(parent)
{
    m_header = new QLabel(tr("No image"), this);
    m_header->setWordWrap(true);
    m_header->setStyleSheet(QStringLiteral("font-weight: bold;"));

    m_histogramLabel = new QLabel(tr("Histogram"), this);
    m_histogramLabel->setVisible(false);
    m_histogram = new ImageHistogramWidget(this);
    m_histogram->setVisible(false);

    m_paletteLabel = new QLabel(tr("Palette"), this);
    m_paletteLabel->setVisible(false);
    m_palette = new ImagePaletteWidget(this);
    m_palette->clear();

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Field"), tr("Value")});
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_header);
    layout->addWidget(m_histogramLabel);
    layout->addWidget(m_histogram);
    layout->addWidget(m_paletteLabel);
    layout->addWidget(m_palette);
    layout->addWidget(m_tree, 1);

    m_applyTimer = new QTimer(this);
    m_applyTimer->setSingleShot(true);
    m_applyTimer->setInterval(40);
    connect(m_applyTimer, &QTimer::timeout, this, &MetadataPanel::applyPendingPath);

    setMinimumWidth(240);
    setWhatsThis(tr("File and image metadata."));
}

void MetadataPanel::clear()
{
    if (m_applyTimer) {
        m_applyTimer->stop();
    }
    m_pendingPath.clear();
    m_pendingDecoded = QImage();
    m_header->setText(tr("No image"));
    m_tree->clear();
    if (m_histogram) {
        m_histogram->clear();
        m_histogram->setVisible(false);
    }
    if (m_histogramLabel) {
        m_histogramLabel->setVisible(false);
    }
    if (m_palette) {
        m_palette->clear();
    }
    if (m_paletteLabel) {
        m_paletteLabel->setVisible(false);
    }
}

void MetadataPanel::addRow(const QString &key, const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, key);
    item->setText(1, value);
    item->setToolTip(1, value);
}

void MetadataPanel::fillImageAnalysis(const QString &path, const QImage &decodedHint)
{
    QTreeWidgetItem *structGroup = ensureGroup(m_tree, tr("Image"));

    QImage image = decodedHint;
    if (image.isNull()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const int frameCount = reader.imageCount();
        if (frameCount > 1) {
            addChildRow(structGroup, tr("Frames / pages"), QString::number(frameCount));
        }
        image = reader.read();
        if (image.isNull()) {
            image = ImageLoader::load(path);
        }
    } else {
        // Hint path: still try frame count without forcing a full re-read when possible.
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (reader.canRead()) {
            const int frameCount = reader.imageCount();
            if (frameCount > 1) {
                addChildRow(structGroup, tr("Frames / pages"), QString::number(frameCount));
            }
        }
    }

    if (image.isNull()) {
        addChildRow(structGroup, tr("Pixels"), tr("(could not decode)"));
        return;
    }

    addChildRow(structGroup, tr("Pixel format"), imageFormatName(image.format()));
    addChildRow(structGroup, tr("Bit depth"),
                tr("%1 bits/pixel").arg(image.depth()));
    addChildRow(structGroup, tr("Alpha"),
                image.hasAlphaChannel() ? tr("Yes") : tr("No"));
    if (image.colorCount() > 0) {
        addChildRow(structGroup, tr("Colour count"),
                    QString::number(image.colorCount()));
    }

    // True layer stacks (PSD etc.) are not exposed by Qt; multi-frame is above.
    // Avoid a dead “Layers: unavailable” row — only report what we can know.

    m_histogram->setFromImage(image);
    m_histogram->setVisible(true);
    m_histogramLabel->setVisible(true);

    QVector<QColor> palette;
    const QList<QRgb> table = image.colorTable();
    if (!table.isEmpty()) {
        palette.reserve(table.size());
        for (QRgb c : table) {
            palette.append(QColor::fromRgba(c));
        }
        QTreeWidgetItem *palGroup = ensureGroup(m_tree, tr("Palette"));
        addChildRow(palGroup, tr("Entries"), QString::number(table.size()));
        const int show = qMin(16, table.size());
        for (int i = 0; i < show; ++i) {
            const QColor col = palette.at(i);
            addChildRow(palGroup, tr("#%1").arg(i),
                        col.name(QColor::HexArgb));
        }
        if (table.size() > show) {
            addChildRow(palGroup, tr("…"),
                        tr("%1 more").arg(table.size() - show));
        }
        m_palette->setColors(palette);
        m_paletteLabel->setVisible(true);
    } else {
        m_palette->clear();
        m_paletteLabel->setVisible(false);
    }
}

void MetadataPanel::setImagePath(const QString &path, const QImage &decodedHint)
{
    if (path.isEmpty()) {
        clear();
        return;
    }
    // Debounce: rapid Gallery selection must not stack synchronous decodes.
    m_pendingPath = path;
    m_pendingDecoded = decodedHint;
    if (m_applyTimer) {
        m_applyTimer->start();
    } else {
        applyPendingPath();
    }
}

void MetadataPanel::applyPendingPath()
{
    const QString path = m_pendingPath;
    const QImage decodedHint = m_pendingDecoded;
    m_pendingPath.clear();
    m_pendingDecoded = QImage();

    if (path.isEmpty()) {
        clear();
        return;
    }

    m_tree->clear();
    if (m_histogram) {
        m_histogram->clear();
        m_histogram->setVisible(false);
    }
    if (m_histogramLabel) {
        m_histogramLabel->setVisible(false);
    }
    if (m_palette) {
        m_palette->clear();
    }
    if (m_paletteLabel) {
        m_paletteLabel->setVisible(false);
    }

    const bool isArchive = ArchivePath::isArchiveRef(path);
    const QString displayName = ArchivePath::displayName(path);
    m_header->setText(displayName.isEmpty() ? path : displayName);
    m_header->setToolTip(path);

    QTreeWidgetItem *fileGroup = ensureGroup(m_tree, tr("File"));
    addChildRow(fileGroup, tr("Path"), path);

    if (isArchive) {
        const ArchivePath::Ref ref = ArchivePath::parse(path);
        if (ref.valid) {
            addChildRow(fileGroup, tr("Archive"), ref.archivePath);
            addChildRow(fileGroup, tr("Member"), ref.memberPath);
            const QFileInfo archiveInfo(ref.archivePath);
            if (archiveInfo.exists()) {
                addChildRow(fileGroup, tr("Archive size"),
                            QLocale().formattedDataSize(archiveInfo.size()));
                addChildRow(fileGroup, tr("Archive modified"),
                            QLocale().toString(archiveInfo.lastModified(),
                                               QLocale::ShortFormat));
            }
        }
    } else {
        const QFileInfo info(path);
        if (info.exists()) {
            addChildRow(fileGroup, tr("Size"),
                        QLocale().formattedDataSize(info.size()));
            addChildRow(fileGroup, tr("Modified"),
                        QLocale().toString(info.lastModified(), QLocale::ShortFormat));
        }
    }

    // Prefer already-decoded pixels for dimensions; otherwise try QImageReader
    // (cheap size query when the plugin supports it) before a full load.
    if (!decodedHint.isNull()) {
        addChildRow(fileGroup, tr("Dimensions"),
                    tr("%1 × %2").arg(decodedHint.width()).arg(decodedHint.height()));
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (reader.canRead()) {
        if (decodedHint.isNull()) {
            const QSize size = reader.size();
            if (size.isValid()) {
                addChildRow(fileGroup, tr("Dimensions"),
                            tr("%1 × %2").arg(size.width()).arg(size.height()));
            }
        }
        addChildRow(fileGroup, tr("Format"),
                    QString::fromLatin1(reader.format()).toUpper());
    } else if (decodedHint.isNull()) {
        // Last resort only when we have neither hint nor plugin size.
        const QImage decoded = ImageLoader::load(path);
        if (!decoded.isNull()) {
            addChildRow(fileGroup, tr("Dimensions"),
                        tr("%1 × %2").arg(decoded.width()).arg(decoded.height()));
            addChildRow(fileGroup, tr("Format"), tr("fallback loader"));
            // Reuse for analysis below.
            fillImageAnalysis(path, decoded);
#ifdef BILTOO_HAVE_EXIV2
            if (loadExiv2Metadata(m_tree, path)) {
                return;
            }
#endif
            return;
        }
        addChildRow(fileGroup, tr("Error"), reader.errorString());
    }

    // Structure, histogram, palette — reuse hint when present.
    fillImageAnalysis(path, decodedHint);

#ifdef BILTOO_HAVE_EXIV2
    if (loadExiv2Metadata(m_tree, path)) {
        return;
    }
#endif

    if (reader.canRead()) {
        const QStringList keys = reader.textKeys();
        if (keys.isEmpty()) {
            addChildRow(fileGroup, tr("Metadata"),
#ifdef BILTOO_HAVE_EXIV2
                        tr("(no Exif/IPTC/XMP found)")
#else
                        tr("(none from image plugin)")
#endif
            );
        } else {
            QTreeWidgetItem *group = ensureGroup(m_tree, tr("Plugin metadata"));
            QStringList sorted = keys;
            sorted.sort(Qt::CaseInsensitive);
            for (const QString &key : sorted) {
                addChildRow(group, key, reader.text(key));
            }
        }
    }
}
