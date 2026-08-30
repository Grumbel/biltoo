// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadatapanel.h"
#include "imageloader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#ifdef QIMGVIEW_HAVE_EXIV2
#  include <exiv2/exiv2.hpp>
#  include <string>
#endif

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

#ifdef QIMGVIEW_HAVE_EXIV2
bool loadExiv2Metadata(QTreeWidget *tree, const QString &path)
{
    try {
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

MetadataPanel::MetadataPanel(QWidget *parent)
    : QWidget(parent)
{
    m_header = new QLabel(tr("No image"), this);
    m_header->setWordWrap(true);
    m_header->setStyleSheet(QStringLiteral("font-weight: bold;"));

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
    layout->addWidget(m_tree, 1);

    setMinimumWidth(240);
#ifdef QIMGVIEW_HAVE_EXIV2
    setWhatsThis(tr("Shows file information and Exif/IPTC/XMP metadata via Exiv2."));
#else
    setWhatsThis(tr("Shows file information and metadata reported by the image plugin. "
                    "Build with libexiv2 for full Exif/IPTC/XMP support."));
#endif
}

void MetadataPanel::clear()
{
    m_header->setText(tr("No image"));
    m_tree->clear();
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

void MetadataPanel::setImagePath(const QString &path)
{
    m_tree->clear();

    if (path.isEmpty()) {
        clear();
        return;
    }

    const QFileInfo info(path);
    m_header->setText(info.fileName());
    m_header->setToolTip(path);

    QTreeWidgetItem *fileGroup = ensureGroup(m_tree, tr("File"));
    addChildRow(fileGroup, tr("Path"), info.absoluteFilePath());
    addChildRow(fileGroup, tr("Size"),
                QLocale().formattedDataSize(info.size()));
    addChildRow(fileGroup, tr("Modified"),
                QLocale().toString(info.lastModified(), QLocale::ShortFormat));

    QImageReader reader(path);
    reader.setAutoTransform(true);

    if (reader.canRead()) {
        const QSize size = reader.size();
        if (size.isValid()) {
            addChildRow(fileGroup, tr("Dimensions"),
                        tr("%1 × %2").arg(size.width()).arg(size.height()));
        }
        addChildRow(fileGroup, tr("Format"),
                    QString::fromLatin1(reader.format()).toUpper());
    } else {
        const QImage decoded = ImageLoader::load(path);
        if (!decoded.isNull()) {
            addChildRow(fileGroup, tr("Dimensions"),
                        tr("%1 × %2").arg(decoded.width()).arg(decoded.height()));
            addChildRow(fileGroup, tr("Format"),
                        tr("fallback loader"));
        } else {
            addChildRow(fileGroup, tr("Error"), reader.errorString());
        }
    }

#ifdef QIMGVIEW_HAVE_EXIV2
    if (loadExiv2Metadata(m_tree, path)) {
        return;
    }
#endif

    // Qt plugin text keys (limited; used when Exiv2 is absent or finds nothing)
    if (reader.canRead()) {
        const QStringList keys = reader.textKeys();
        if (keys.isEmpty()) {
            addChildRow(fileGroup, tr("Metadata"),
#ifdef QIMGVIEW_HAVE_EXIV2
                        tr("(no Exif/IPTC/XMP found)")
#else
                        tr("(none reported by Qt image plugin; build with libexiv2 for full Exif)")
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
