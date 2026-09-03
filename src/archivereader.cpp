// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "archivereader.h"
#include "archivepath.h"
#include "imageloader.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#ifdef QIMGVIEW_HAVE_ARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

namespace ArchiveReader {

bool isAvailable()
{
#ifdef QIMGVIEW_HAVE_ARCHIVE
    return true;
#else
    return false;
#endif
}

#ifdef QIMGVIEW_HAVE_ARCHIVE

namespace {

struct ArchiveCloser {
    struct archive *a = nullptr;
    ~ArchiveCloser()
    {
        if (a) {
            archive_read_close(a);
            archive_read_free(a);
        }
    }
};

QString normalizeMemberName(const QString &raw)
{
    QString name = raw;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1String("./"))) {
        name = name.mid(2);
    }
    while (name.startsWith(QLatin1Char('/'))) {
        name = name.mid(1);
    }
    return name;
}

QString entryPathString(struct archive_entry *entry)
{
    if (!entry) {
        return {};
    }
    const char *utf8 = archive_entry_pathname_utf8(entry);
    if (utf8 && utf8[0] != '\0') {
        return QString::fromUtf8(utf8);
    }
    const char *name = archive_entry_pathname(entry);
    if (!name) {
        return {};
    }
    return QString::fromUtf8(name);
}

struct archive *openArchive(const QString &archivePath)
{
    struct archive *a = archive_read_new();
    if (!a) {
        qWarning("ArchiveReader: archive_read_new failed");
        return nullptr;
    }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    const QByteArray utf8 = archivePath.toUtf8();
    int rc = archive_read_open_filename(a, utf8.constData(), 10240);
    if (rc != ARCHIVE_OK) {
        const QByteArray local = QFile::encodeName(archivePath);
        if (local != utf8) {
            archive_read_free(a);
            a = archive_read_new();
            if (!a) {
                return nullptr;
            }
            archive_read_support_filter_all(a);
            archive_read_support_format_all(a);
            rc = archive_read_open_filename(a, local.constData(), 10240);
        }
    }
    if (rc != ARCHIVE_OK) {
        qWarning("ArchiveReader: cannot open %s: %s",
                 qPrintable(archivePath),
                 archive_error_string(a) ? archive_error_string(a) : "unknown error");
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

QByteArray readEntryBytes(struct archive *a, la_int64_t sz)
{
    QByteArray data;
    constexpr la_int64_t kMax = la_int64_t(512) * 1024 * 1024;
    if (sz > 0 && sz < kMax) {
        data.resize(int(sz));
        qint64 total = 0;
        while (total < sz) {
            const la_ssize_t n = archive_read_data(a, data.data() + total,
                                                   size_t(sz - total));
            if (n < 0) {
                qWarning("ArchiveReader: read error: %s",
                         archive_error_string(a) ? archive_error_string(a) : "unknown");
                data.clear();
                return data;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }
        data.resize(int(total));
        return data;
    }
    constexpr int kChunk = 256 * 1024;
    QByteArray chunk;
    chunk.resize(kChunk);
    for (;;) {
        const la_ssize_t n = archive_read_data(a, chunk.data(), size_t(kChunk));
        if (n < 0) {
            qWarning("ArchiveReader: stream read error: %s",
                     archive_error_string(a) ? archive_error_string(a) : "unknown");
            data.clear();
            break;
        }
        if (n == 0) {
            break;
        }
        data.append(chunk.constData(), int(n));
        if (data.size() > int(kMax)) {
            qWarning("ArchiveReader: member exceeds size limit");
            data.clear();
            break;
        }
    }
    return data;
}

bool entryIsImage(const QString &name)
{
    if (name.isEmpty() || name.endsWith(QLatin1Char('/'))) {
        return false;
    }
    if (name.contains(QLatin1String("__MACOSX/"))) {
        return false;
    }
    const int slash = name.lastIndexOf(QLatin1Char('/'));
    const QString base = (slash >= 0) ? name.mid(slash + 1) : name;
    if (base.startsWith(QLatin1Char('.'))) {
        return false;
    }
    return ImageLoader::isImageFile(base);
}

int nextHeader(struct archive *a, struct archive_entry **entry)
{
    for (;;) {
        const int rc = archive_read_next_header(a, entry);
        if (rc == ARCHIVE_RETRY) {
            continue;
        }
        return rc;
    }
}

} // namespace

QStringList listImageMembers(const QString &archivePath)
{
    QStringList out;
    if (archivePath.isEmpty() || !QFileInfo::exists(archivePath)) {
        qWarning("ArchiveReader: list: missing file %s", qPrintable(archivePath));
        return out;
    }
    ArchiveCloser guard;
    guard.a = openArchive(archivePath);
    if (!guard.a) {
        return out;
    }
    struct archive_entry *entry = nullptr;
    for (;;) {
        const int rc = nextHeader(guard.a, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            qWarning("ArchiveReader: list header error in %s: %s",
                     qPrintable(archivePath),
                     archive_error_string(guard.a) ? archive_error_string(guard.a) : "unknown");
            break;
        }
        const QString name = entryPathString(entry);
        if (entryIsImage(name)) {
            const QString member = normalizeMemberName(name);
            if (!member.isEmpty()) {
                out.append(member);
            }
        }
        archive_read_data_skip(guard.a);
    }
    out.sort();
    return out;
}

QByteArray readMember(const QString &archivePath, const QString &memberPath)
{
    QByteArray data;
    if (archivePath.isEmpty() || memberPath.isEmpty()) {
        return data;
    }
    if (!QFileInfo::exists(archivePath)) {
        qWarning("ArchiveReader: read: missing archive %s", qPrintable(archivePath));
        return data;
    }
    const QString want = normalizeMemberName(memberPath);

    ArchiveCloser guard;
    guard.a = openArchive(archivePath);
    if (!guard.a) {
        return data;
    }

    struct archive_entry *entry = nullptr;
    for (;;) {
        const int rc = nextHeader(guard.a, &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            qWarning("ArchiveReader: read header error in %s: %s",
                     qPrintable(archivePath),
                     archive_error_string(guard.a) ? archive_error_string(guard.a) : "unknown");
            break;
        }
        const QString name = normalizeMemberName(entryPathString(entry));
        if (name == want) {
            data = readEntryBytes(guard.a, archive_entry_size(entry));
            break;
        }
        archive_read_data_skip(guard.a);
    }

    if (data.isEmpty()) {
        qWarning("ArchiveReader: member not found or empty: \"%s\" in %s",
                 qPrintable(want), qPrintable(archivePath));
    }
    return data;
}

#else // !QIMGVIEW_HAVE_ARCHIVE

QStringList listImageMembers(const QString & /*archivePath*/)
{
    return {};
}

QByteArray readMember(const QString & /*archivePath*/, const QString & /*memberPath*/)
{
    return {};
}

#endif

QStringList expandArchiveToImageRefs(const QString &archivePath)
{
    QStringList refs;
    if (!isAvailable()) {
        qWarning("ArchiveReader: libarchive was not enabled at build time "
                 "(QIMGVIEW_HAVE_ARCHIVE); cannot open %s",
                 qPrintable(archivePath));
        return refs;
    }
    if (!ArchivePath::isArchiveFile(archivePath)) {
        return refs;
    }
    const QString abs = QFileInfo(archivePath).absoluteFilePath();
    const QStringList members = listImageMembers(abs);
    if (members.isEmpty()) {
        qWarning("ArchiveReader: no image members in %s", qPrintable(abs));
        return refs;
    }
    refs.reserve(members.size());
    for (const QString &m : members) {
        const QString ref = ArchivePath::makeRef(abs, m);
        if (!ref.isEmpty()) {
            refs.append(ref);
        }
    }
    return refs;
}

} // namespace ArchiveReader
