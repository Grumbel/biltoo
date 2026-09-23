// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/sessionexport.h"

#include "biltoo_thread.h"
#include "imageloader.h"
#include "session/sessionappearance.h"
#include "host/pagepath.h"
#include "host/archivepath.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QMarginsF>
#include <QtEndian>

#include <cstring>
#include <atomic>

namespace SessionExport {
namespace {

QString sanitizeStem(const QString &stem)
{
    QString s = stem;
    for (QChar &c : s) {
        if (c.unicode() < 32 || QStringLiteral("/\\:*?\"<>|").contains(c)) {
            c = QLatin1Char('_');
        }
    }
    if (s.isEmpty()) {
        s = QStringLiteral("image");
    }
    return s;
}

QImage clampLongEdge(QImage img, int maxLongEdge)
{
    if (img.isNull() || maxLongEdge <= 0) {
        return img;
    }
    const int le = qMax(img.width(), img.height());
    if (le <= maxLongEdge) {
        return img;
    }
    return img.scaled(maxLongEdge, maxLongEdge, Qt::KeepAspectRatio,
                      Qt::SmoothTransformation);
}

bool saveImage(const QImage &img, const QString &path, Format fmt, int jpegQuality)
{
    if (img.isNull() || path.isEmpty()) {
        return false;
    }
    if (fmt == Format::Png) {
        return img.save(path, "PNG");
    }
    // JPEG encoder rejects some formats (e.g. mono, indexed).
    QImage out = img;
    if (out.format() != QImage::Format_RGB32
        && out.format() != QImage::Format_ARGB32
        && out.format() != QImage::Format_RGB888) {
        out = out.convertToFormat(QImage::Format_RGB32);
    }
    return out.save(path, "JPEG", qBound(1, jpegQuality, 100));
}

QString extension(Format fmt)
{
    return fmt == Format::Png ? QStringLiteral("png") : QStringLiteral("jpg");
}

/** Minimal ZIP (store only) for CBZ. */
bool writeStoreZip(const QString &zipPath,
                   const QVector<QPair<QString, QByteArray>> &entries)
{
    QFile out(zipPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    struct LocalRec {
        QString name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };
    QVector<LocalRec> locals;
    locals.reserve(entries.size());

    auto crc32 = [](const QByteArray &data) -> quint32 {
        static quint32 table[256];
        static bool init = false;
        if (!init) {
            for (quint32 i = 0; i < 256; ++i) {
                quint32 c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            init = true;
        }
        quint32 c = 0xffffffffu;
        for (unsigned char b : data) {
            c = table[(c ^ b) & 0xffu] ^ (c >> 8);
        }
        return c ^ 0xffffffffu;
    };

    for (const auto &e : entries) {
        const QByteArray nameUtf8 = e.first.toUtf8();
        LocalRec rec;
        rec.name = e.first;
        rec.crc = crc32(e.second);
        rec.size = static_cast<quint32>(e.second.size());
        rec.offset = static_cast<quint32>(out.pos());

        // local file header
        char hdr[30];
        std::memset(hdr, 0, sizeof(hdr));
        qToLittleEndian<quint32>(0x04034b50u, hdr + 0);
        qToLittleEndian<quint16>(20, hdr + 4);  // version needed
        qToLittleEndian<quint16>(0x0800, hdr + 6); // UTF-8
        qToLittleEndian<quint16>(0, hdr + 8);   // store
        qToLittleEndian<quint16>(0, hdr + 10);
        qToLittleEndian<quint16>(0, hdr + 12);
        qToLittleEndian<quint32>(rec.crc, hdr + 14);
        qToLittleEndian<quint32>(rec.size, hdr + 18);
        qToLittleEndian<quint32>(rec.size, hdr + 22);
        qToLittleEndian<quint16>(static_cast<quint16>(nameUtf8.size()), hdr + 26);
        qToLittleEndian<quint16>(0, hdr + 28);
        if (out.write(hdr, 30) != 30) {
            return false;
        }
        if (out.write(nameUtf8) != nameUtf8.size()) {
            return false;
        }
        if (!e.second.isEmpty() && out.write(e.second) != e.second.size()) {
            return false;
        }
        locals.append(rec);
    }

    const quint32 centralOffset = static_cast<quint32>(out.pos());
    for (const LocalRec &rec : locals) {
        const QByteArray nameUtf8 = rec.name.toUtf8();
        char ch[46];
        std::memset(ch, 0, sizeof(ch));
        qToLittleEndian<quint32>(0x02014b50u, ch + 0);
        qToLittleEndian<quint16>(20, ch + 4);
        qToLittleEndian<quint16>(20, ch + 6);
        qToLittleEndian<quint16>(0x0800, ch + 8);
        qToLittleEndian<quint16>(0, ch + 10);
        qToLittleEndian<quint16>(0, ch + 12);
        qToLittleEndian<quint16>(0, ch + 14);
        qToLittleEndian<quint32>(rec.crc, ch + 16);
        qToLittleEndian<quint32>(rec.size, ch + 20);
        qToLittleEndian<quint32>(rec.size, ch + 24);
        qToLittleEndian<quint16>(static_cast<quint16>(nameUtf8.size()), ch + 28);
        qToLittleEndian<quint16>(0, ch + 30);
        qToLittleEndian<quint16>(0, ch + 32);
        qToLittleEndian<quint16>(0, ch + 34);
        qToLittleEndian<quint16>(0, ch + 36);
        qToLittleEndian<quint32>(0, ch + 38);
        qToLittleEndian<quint32>(rec.offset, ch + 42);
        if (out.write(ch, 46) != 46) {
            return false;
        }
        if (out.write(nameUtf8) != nameUtf8.size()) {
            return false;
        }
    }
    const quint32 centralSize =
        static_cast<quint32>(out.pos()) - centralOffset;
    char end[22];
    std::memset(end, 0, sizeof(end));
    qToLittleEndian<quint32>(0x06054b50u, end + 0);
    qToLittleEndian<quint16>(0, end + 4);
    qToLittleEndian<quint16>(0, end + 6);
    qToLittleEndian<quint16>(static_cast<quint16>(locals.size()), end + 8);
    qToLittleEndian<quint16>(static_cast<quint16>(locals.size()), end + 10);
    qToLittleEndian<quint32>(centralSize, end + 12);
    qToLittleEndian<quint32>(centralOffset, end + 16);
    qToLittleEndian<quint16>(0, end + 20);
    if (out.write(end, 22) != 22) {
        return false;
    }
    out.close();
    return true;
}


} // namespace

QString fileStem(const QString &path)
{
    // Virtual session paths must not go through QFileInfo on the full string:
    // "//page:" / "//archive:" collapse under cleanPath and yield empty/wrong stems.
    if (PagePath::isPageRef(path)) {
        const PagePath::Ref ref = PagePath::parse(path);
        if (ref.valid) {
            return sanitizeStem(
                QStringLiteral("%1_p%2")
                    .arg(QFileInfo(ref.pdfPath).completeBaseName())
                    .arg(ref.page));
        }
    }
    if (PagePath::isPdfImageRef(path)) {
        const QString doc = PagePath::documentFilePath(path);
        const int n = PagePath::pdfImageNumber(path);
        return sanitizeStem(
            QStringLiteral("%1_img%2")
                .arg(QFileInfo(doc).completeBaseName())
                .arg(n));
    }
    if (ArchivePath::isArchiveRef(path)) {
        const ArchivePath::Ref ref = ArchivePath::parse(path);
        if (ref.valid) {
            return sanitizeStem(QFileInfo(ref.memberPath).completeBaseName());
        }
    }
    return sanitizeStem(QFileInfo(path).completeBaseName());
}

/**
 * True when writing @p outPath would replace the same on-disk file as @p sourcePath.
 * Virtual session paths (PDF page, archive member) never collide with export files.
 * Never treat empty canonical paths as equal (QFileInfo collapses // markers).
 */
bool wouldOverwriteSource(const QString &sourcePath, const QString &outPath)
{
    if (sourcePath.isEmpty() || outPath.isEmpty()) {
        return false;
    }
    if (PagePath::isPageRef(sourcePath) || PagePath::isPdfImageRef(sourcePath)
        || ArchivePath::isArchiveRef(sourcePath)) {
        return false;
    }
    const QFileInfo srcInfo(sourcePath);
    const QFileInfo outInfo(outPath);
    const QString srcCanon = srcInfo.canonicalFilePath();
    const QString outCanon = outInfo.canonicalFilePath();
    // Both resolve to an existing file.
    if (!srcCanon.isEmpty() && !outCanon.isEmpty()) {
        return srcCanon == outCanon;
    }
    // Destination may not exist yet: compare cleaned absolute paths only when
    // the source is a real local file.
    if (srcCanon.isEmpty() || !srcInfo.isFile()) {
        return false;
    }
    return srcCanon == QDir::cleanPath(outInfo.absoluteFilePath());
}

QImage bakeItem(const Item &item, int maxLongEdge)
{
    ASSERT_NOT_GUI_THREAD();
    if (item.path.isEmpty()) {
        return {};
    }
    // Full decode first so cropSourceSize / FullSource bake stays in native
    // pixel space; clamp long edge only after appearance bake.
    QImage raw = ImageLoader::loadThumbnail(item.path, /*maxEdge=*/0);
    if (raw.isNull()) {
        return {};
    }
    QImage baked = SessionAppearance::applyContentToImage(
        raw, item.appearance, SessionAppearance::PixelKind::FullSource);
    if (baked.isNull()) {
        baked = raw;
    }
    return clampLongEdge(baked, maxLongEdge);
}

Result exportItems(const QVector<Item> &items, const Options &opt,
                   ProgressFn progress, std::atomic<bool> *cancelFlag)
{
    ASSERT_NOT_GUI_THREAD();
    Result r;
    r.destPath = opt.destPath;
    if (items.isEmpty() || opt.destPath.isEmpty()) {
        r.errors << QStringLiteral("nothing to export");
        return r;
    }

    const int total = items.size();
    auto cancelled = [&]() -> bool {
        return cancelFlag && cancelFlag->load(std::memory_order_relaxed);
    };
    auto afterItem = [&](int completed) -> bool {
        if (cancelled()) {
            r.cancelled = true;
            return false;
        }
        if (progress && !progress(completed, total)) {
            r.cancelled = true;
            if (cancelFlag) {
                cancelFlag->store(true, std::memory_order_relaxed);
            }
            return false;
        }
        return !cancelled();
    };

    if (opt.container == Container::Directory) {
        QDir dir(opt.destPath);
        if (!dir.exists() && !QDir().mkpath(opt.destPath)) {
            r.errors << QStringLiteral("cannot create directory");
            return r;
        }
        const int width = qMax(3, QString::number(items.size()).size());
        for (int i = 0; i < items.size(); ++i) {
            if (cancelled()) {
                r.cancelled = true;
                break;
            }
            const Item &it = items.at(i);
            QImage img = bakeItem(it, opt.maxLongEdge);
            if (img.isNull()) {
                ++r.failed;
                r.errors << QStringLiteral("decode failed: %1").arg(it.path);
            } else {
                const QString name =
                    QStringLiteral("%1_%2.%3")
                        .arg(i + 1, width, 10, QLatin1Char('0'))
                        .arg(fileStem(it.path))
                        .arg(extension(opt.format));
                const QString outPath = dir.filePath(name);
                if (wouldOverwriteSource(it.path, outPath)) {
                    ++r.failed;
                    r.errors << QStringLiteral("refusing to overwrite source: %1")
                                    .arg(outPath);
                } else if (!saveImage(img, outPath, opt.format, opt.jpegQuality)) {
                    ++r.failed;
                    r.errors << QStringLiteral("write failed: %1").arg(outPath);
                } else {
                    ++r.written;
                }
            }
            if (!afterItem(i + 1)) {
                break;
            }
        }
        return r;
    }

    if (opt.container == Container::Cbz) {
        {
            const QFileInfo fi(opt.destPath);
            if (!fi.absolutePath().isEmpty()
                && !QDir().mkpath(fi.absolutePath())) {
                r.errors << QStringLiteral("cannot create parent directory");
                return r;
            }
        }
        QVector<QPair<QString, QByteArray>> entries;
        entries.reserve(items.size());
        const int width = qMax(3, QString::number(items.size()).size());
        for (int i = 0; i < items.size(); ++i) {
            if (cancelled()) {
                r.cancelled = true;
                break;
            }
            const Item &it = items.at(i);
            QImage img = bakeItem(it, opt.maxLongEdge);
            if (img.isNull()) {
                ++r.failed;
                r.errors << QStringLiteral("decode failed: %1").arg(it.path);
            } else {
                QByteArray bytes;
                QBuffer buf(&bytes);
                buf.open(QIODevice::WriteOnly);
                if (opt.format == Format::Png) {
                    img.save(&buf, "PNG");
                } else {
                    QImage out = img;
                    if (out.format() != QImage::Format_RGB32
                        && out.format() != QImage::Format_ARGB32
                        && out.format() != QImage::Format_RGB888) {
                        out = out.convertToFormat(QImage::Format_RGB32);
                    }
                    out.save(&buf, "JPEG", qBound(1, opt.jpegQuality, 100));
                }
                buf.close();
                if (bytes.isEmpty()) {
                    ++r.failed;
                    r.errors << QStringLiteral("encode failed: %1").arg(it.path);
                } else {
                    const QString name =
                        QStringLiteral("%1_%2.%3")
                            .arg(i + 1, width, 10, QLatin1Char('0'))
                            .arg(fileStem(it.path))
                            .arg(extension(opt.format));
                    entries.append(qMakePair(name, bytes));
                    ++r.written;
                }
            }
            if (!afterItem(i + 1)) {
                break;
            }
        }
        if (entries.isEmpty()) {
            r.errors << QStringLiteral("no pages encoded");
            return r;
        }
        if (QFile::exists(opt.destPath)) {
            QFile::remove(opt.destPath);
        }
        if (!writeStoreZip(opt.destPath, entries)) {
            r.errors << QStringLiteral("cbz write failed");
            r.written = 0;
            return r;
        }
        return r;
    }

    // PDF — one page per image, page size matches image aspect at 72 dpi base.
    {
        {
            const QFileInfo fi(opt.destPath);
            if (!fi.absolutePath().isEmpty()
                && !QDir().mkpath(fi.absolutePath())) {
                r.errors << QStringLiteral("cannot create parent directory");
                return r;
            }
        }
        QPdfWriter pdf(opt.destPath);
        pdf.setTitle(QStringLiteral("biltoo export"));
        pdf.setCreator(QStringLiteral("biltoo"));
        // Margins 0 so the image fills the page.
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        bool first = true;
        QPainter painter;
        for (int i = 0; i < items.size(); ++i) {
            if (cancelled()) {
                r.cancelled = true;
                break;
            }
            const Item &it = items.at(i);
            QImage img = bakeItem(it, opt.maxLongEdge);
            if (img.isNull()) {
                ++r.failed;
                r.errors << QStringLiteral("decode failed: %1").arg(it.path);
            } else {
                // Page size in points (1/72"); pixel size as points ≈ 72 dpi 1:1.
                const QPageSize pageSize(
                    QSizeF(img.width(), img.height()), QPageSize::Point);
                // setPageSize must precede begin / newPage for that page.
                pdf.setPageSize(pageSize);
                if (first) {
                    if (!painter.begin(&pdf)) {
                        r.errors << QStringLiteral("pdf begin failed");
                        return r;
                    }
                    first = false;
                } else {
                    if (!pdf.newPage()) {
                        r.errors << QStringLiteral("pdf newPage failed");
                        break;
                    }
                }
                const QRectF pageRect(0, 0, pdf.width(), pdf.height());
                painter.drawImage(pageRect, img);
                ++r.written;
            }
            if (!afterItem(i + 1)) {
                break;
            }
        }
        if (painter.isActive()) {
            painter.end();
        }
        if (r.written == 0 && r.errors.isEmpty()) {
            r.errors << QStringLiteral("no pages written");
        }
        return r;
    }
}

} // namespace SessionExport
