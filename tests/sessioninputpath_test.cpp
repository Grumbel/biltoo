// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * SessionInputPath: classify CLI/drop strings without treating URLs as OS paths.
 */

#include "session/sessioninputpath.h"

#include <QFileInfo>
#include <QtTest/QtTest>

class SessionInputPathTest : public QObject
{
    Q_OBJECT
private slots:
    void empty_and_whitespace();
    void local_absolute_and_relative();
    void file_url_to_local();
    void http_and_https_archive_org();
    void https_not_absolutized_by_canonical();
    void broken_https_single_slash_not_local_junk();
    void url_path_leaf_from_url_not_filesystem();
    void remote_cache_path_stable_and_under_remote();
    void other_schemes();
    void windows_drive_lookalike_is_local();
};

void SessionInputPathTest::empty_and_whitespace()
{
    QCOMPARE(SessionInputPath::classify(QString()).kind, SessionInputPath::Kind::Empty);
    QCOMPARE(SessionInputPath::classify(QStringLiteral("   ")).kind,
             SessionInputPath::Kind::Empty);
    QVERIFY(SessionInputPath::canonicalForSession(QString()).isEmpty());
}

void SessionInputPathTest::local_absolute_and_relative()
{
    {
        const auto c = SessionInputPath::classify(QStringLiteral("/tmp/photo.jpg"));
        QCOMPARE(c.kind, SessionInputPath::Kind::LocalPath);
        QCOMPARE(c.localPath, QStringLiteral("/tmp/photo.jpg"));
        QVERIFY(c.url.isEmpty());
    }
    {
        const auto c = SessionInputPath::classify(QStringLiteral("subdir/a.png"));
        QCOMPARE(c.kind, SessionInputPath::Kind::LocalPath);
        QCOMPARE(c.localPath, QStringLiteral("subdir/a.png"));
    }
}

void SessionInputPathTest::file_url_to_local()
{
    const auto c = SessionInputPath::classify(QStringLiteral("file:///tmp/doc.pdf"));
    QCOMPARE(c.kind, SessionInputPath::Kind::FileUrl);
    QCOMPARE(c.localPath, QStringLiteral("/tmp/doc.pdf"));
    QVERIFY(c.url.isLocalFile());

    const QString canon = SessionInputPath::canonicalForSession(
        QStringLiteral("file:///tmp/doc.pdf"));
    QVERIFY(canon.endsWith(QStringLiteral("doc.pdf")));
    QVERIFY(!canon.startsWith(QStringLiteral("file:")));
}

void SessionInputPathTest::http_and_https_archive_org()
{
    const QString https = QStringLiteral(
        "https://archive.org/download/scientificamerican1867scie/"
        "scientificamerican1867scie.pdf");
    const QString http = QStringLiteral(
        "http://archive.org/download/scientificamerican1867scie/"
        "scientificamerican1867scie.pdf");

    for (const QString &s : {https, http}) {
        const auto c = SessionInputPath::classify(s);
        QCOMPARE(c.kind, SessionInputPath::Kind::HttpUrl);
        QVERIFY(c.localPath.isEmpty());
        QVERIFY(SessionInputPath::isHttpUrl(s));
        QCOMPARE(c.url.host(), QStringLiteral("archive.org"));
        QVERIFY(c.url.path().endsWith(QStringLiteral(".pdf")));
    }

    // Leading/trailing whitespace
    const auto padded = SessionInputPath::classify(QStringLiteral("  ") + https + QStringLiteral("\n"));
    QCOMPARE(padded.kind, SessionInputPath::Kind::HttpUrl);
}

void SessionInputPathTest::https_not_absolutized_by_canonical()
{
    const QString https = QStringLiteral(
        "https://archive.org/download/scientificamerican1867scie/"
        "scientificamerican1867scie.pdf");

    const QString canon = SessionInputPath::canonicalForSession(https);
    QCOMPARE(canon, https);

    // Contrast: QFileInfo path semantics corrupt the string (the original bug).
    const QString corrupted = QFileInfo(https).absoluteFilePath();
    QVERIFY(corrupted != https);
    QVERIFY(!SessionInputPath::isHttpUrl(corrupted));
}

void SessionInputPathTest::broken_https_single_slash_not_local_junk()
{
    // Historical corruption: "$PWD/https:/host/..." — must not classify as HttpUrl
    // and must not be "fixed" into a download.
    const QString junk = QStringLiteral(
        "/home/ingo/projects/biltoo/biltoo.git/https:/archive.org/download/x/x.pdf");
    const auto c = SessionInputPath::classify(junk);
    QCOMPARE(c.kind, SessionInputPath::Kind::LocalPath);
    QCOMPARE(c.localPath, junk);
    QVERIFY(!SessionInputPath::isHttpUrl(junk));
}

void SessionInputPathTest::url_path_leaf_from_url_not_filesystem()
{
    const QUrl url(QStringLiteral(
        "https://archive.org/download/scientificamerican1867scie/"
        "scientificamerican1867scie.pdf"));
    QCOMPARE(SessionInputPath::urlPathLeafName(url),
             QStringLiteral("scientificamerican1867scie.pdf"));

    const QUrl bare(QStringLiteral("https://example.com/"));
    QCOMPARE(SessionInputPath::urlPathLeafName(bare), QStringLiteral("download"));

    // Query must not require filesystem path APIs
    const QUrl withQuery(QStringLiteral("https://example.com/files/a.pdf?download=1"));
    QCOMPARE(SessionInputPath::urlPathLeafName(withQuery), QStringLiteral("a.pdf"));
}

void SessionInputPathTest::remote_cache_path_stable_and_under_remote()
{
    const QUrl url(QStringLiteral("https://archive.org/download/id/file.pdf"));
    const QString a = SessionInputPath::remoteCachePath(url, QStringLiteral("/tmp/biltoo-cache-test"));
    const QString b = SessionInputPath::remoteCachePath(url, QStringLiteral("/tmp/biltoo-cache-test"));
    QCOMPARE(a, b);
    QVERIFY(a.contains(QStringLiteral("/remote/")));
    QVERIFY(a.endsWith(QStringLiteral("_file.pdf")) || a.endsWith(QStringLiteral("file.pdf")));

    const QUrl other(QStringLiteral("https://archive.org/download/id/other.pdf"));
    const QString c = SessionInputPath::remoteCachePath(other, QStringLiteral("/tmp/biltoo-cache-test"));
    QVERIFY(a != c);
}

void SessionInputPathTest::other_schemes()
{
    const auto ftp = SessionInputPath::classify(QStringLiteral("ftp://example.com/a.pdf"));
    QCOMPARE(ftp.kind, SessionInputPath::Kind::OtherUrl);
    QVERIFY(ftp.localPath.isEmpty());

    const auto about = SessionInputPath::classify(QStringLiteral("about:blank"));
    QCOMPARE(about.kind, SessionInputPath::Kind::OtherUrl);
}

void SessionInputPathTest::windows_drive_lookalike_is_local()
{
    // Single-letter scheme must not be treated as a URL (Windows drive).
    const auto c = SessionInputPath::classify(QStringLiteral("C:/Users/me/photo.jpg"));
    QCOMPARE(c.kind, SessionInputPath::Kind::LocalPath);
    QCOMPARE(c.localPath, QStringLiteral("C:/Users/me/photo.jpg"));
}

QTEST_MAIN(SessionInputPathTest)
#include "sessioninputpath_test.moc"
