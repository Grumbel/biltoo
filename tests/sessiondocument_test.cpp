// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for SessionDocument (Phase 6 Tier 4 prerequisite).
 *
 * Locks path/id alignment, alloc never-reuse, and duplicate-path identity —
 * the contract Appearance-into-SessionDocument must preserve.
 * setPaths clears fat appearance (Open/Replace); replaceAll keeps it (sort).
 */

#include "sessiondocument.h"

#include <QtTest/QtTest>

class SessionDocumentTest : public QObject
{
    Q_OBJECT
private slots:
    void empty_initial();
    void setPaths_allocatesUniqueIds();
    void append_duplicatePath_distinctIds();
    void removeAt_doesNotReuseId();
    void replaceAll_preservesIds();
    void indexOfId_and_path();
    void validateUniqueIds_detectsDuplicate();
    void insert_middle_shiftsIds();
    void clear_empties();
    void clearPaths_keepsAppearance();
    void countPath_and_firstId();
    void indexOfPathPreferId_prefersBoundId();
    void appearance_on_document();
    // Stage 2 residual (2046): setPaths orphans prior ids; replaceAll keeps them
    void setPaths_clearsAppearance();
    void replaceAll_keepsAppearance();
    void removeAt_clearsAppearance();
};

void SessionDocumentTest::empty_initial()
{
    SessionDocument doc;
    QVERIFY(doc.isEmpty());
    QCOMPARE(doc.size(), 0);
}

void SessionDocumentTest::setPaths_allocatesUniqueIds()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg"), QStringLiteral("/c.jpg")});
    QCOMPARE(doc.size(), 3);
    QVERIFY(doc.validateUniqueIds("setPaths"));
    QCOMPARE(doc.pathAt(0), QStringLiteral("/a.jpg"));
    QCOMPARE(doc.pathAt(2), QStringLiteral("/c.jpg"));
    QVERIFY(doc.idAt(0) != doc.idAt(1));
    QVERIFY(doc.idAt(1) != doc.idAt(2));
}

void SessionDocumentTest::append_duplicatePath_distinctIds()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/dup.jpg"));
    doc.append(QStringLiteral("/dup.jpg"));
    QCOMPARE(doc.size(), 2);
    QCOMPARE(doc.pathAt(0), doc.pathAt(1));
    QVERIFY(doc.idAt(0) != doc.idAt(1));
    QVERIFY(doc.validateUniqueIds("dup path"));
}

void SessionDocumentTest::removeAt_doesNotReuseId()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    const SessionImageId removed = doc.idAt(0);
    doc.removeAt(0);
    doc.append(QStringLiteral("/c.jpg"));
    QCOMPARE(doc.size(), 2);
    QVERIFY(doc.idAt(0) != removed);
    QVERIFY(doc.idAt(1) != removed);
    QVERIFY(doc.validateUniqueIds("after remove"));
}

void SessionDocumentTest::replaceAll_preservesIds()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    const SessionImageId idA = doc.idAt(0);
    const SessionImageId idB = doc.idAt(1);
    // Swap order, keep ids
    doc.replaceAll({QStringLiteral("/b.jpg"), QStringLiteral("/a.jpg")},
                   {idB, idA});
    QCOMPARE(doc.pathAt(0), QStringLiteral("/b.jpg"));
    QCOMPARE(doc.idAt(0), idB);
    QCOMPARE(doc.idAt(1), idA);
    QVERIFY(doc.validateUniqueIds("replaceAll"));
}

void SessionDocumentTest::indexOfId_and_path()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg"), QStringLiteral("/a.jpg")});
    const SessionImageId id0 = doc.idAt(0);
    QCOMPARE(doc.indexOfId(id0), 0);
    QCOMPARE(doc.indexOfPath(QStringLiteral("/a.jpg")), 0);
    QCOMPARE(doc.lastIndexOfPath(QStringLiteral("/a.jpg")), 2);
}

void SessionDocumentTest::validateUniqueIds_detectsDuplicate()
{
    // replaceAll is the legacy recovery path: it must not leave duplicate ids
    // in the document. validateUniqueIds is defense-in-depth after mutations.
    SessionDocument doc;
    doc.replaceAll({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")},
                   {1, 1}); // would-be duplicate — repaired in place
    QVERIFY(doc.validateUniqueIds("after replaceAll repair"));
    QVERIFY(doc.idAt(0) != doc.idAt(1));
    QCOMPARE(doc.idAt(0), SessionImageId(1));
}

void SessionDocumentTest::insert_middle_shiftsIds()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/c.jpg")});
    const SessionImageId idA = doc.idAt(0);
    const SessionImageId idC = doc.idAt(1);
    doc.insert(1, QStringLiteral("/b.jpg"));
    QCOMPARE(doc.size(), 3);
    QCOMPARE(doc.idAt(0), idA);
    QCOMPARE(doc.idAt(2), idC);
    QVERIFY(doc.idAt(1) != idA);
    QVERIFY(doc.idAt(1) != idC);
    QVERIFY(doc.validateUniqueIds("insert"));
}

void SessionDocumentTest::clear_empties()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg")});
    doc.clear();
    QVERIFY(doc.isEmpty());
    QCOMPARE(doc.size(), 0);
}

void SessionDocumentTest::clearPaths_keepsAppearance()
{
    // clearPaths does not clear seed book (full wipe is clear()).
    SessionDocument doc;
    doc.append(QStringLiteral("/a.jpg"));
    const SessionImageId id = doc.idAt(0);
    doc.seedBook().markSeedAttempted(id);
    doc.clearPaths();
    QVERIFY(doc.isEmpty());
    QVERIFY(doc.seedBook().seedAttempted(id));
}

void SessionDocumentTest::countPath_and_firstId()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/a.jpg"));
    doc.append(QStringLiteral("/b.jpg"));
    doc.append(QStringLiteral("/a.jpg"));
    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/a.jpg")), 2);
    QCOMPARE(doc.countPathOccurrences(QStringLiteral("/b.jpg")), 1);
    QCOMPARE(doc.firstIdForPath(QStringLiteral("/a.jpg")), doc.idAt(0));
    QCOMPARE(doc.firstIdForPath(QStringLiteral("/missing.jpg")), kInvalidSessionImageId);
}

void SessionDocumentTest::indexOfPathPreferId_prefersBoundId()
{
    SessionDocument doc;
    // Two rows same path: first bound id wins over pure path index when we
    // resolve via firstIdForPath (duplicate-safe identity).
    doc.append(QStringLiteral("/dup.jpg"));
    doc.append(QStringLiteral("/dup.jpg"));
    const SessionImageId id0 = doc.idAt(0);
    QCOMPARE(doc.indexOfPathPreferId(QStringLiteral("/dup.jpg")), 0);
    QCOMPARE(doc.indexOfId(id0), 0);
    // Unbound path: pure path index.
    QCOMPARE(doc.indexOfPathPreferId(QStringLiteral("/missing.jpg")), -1);
    // Empty path.
    QCOMPARE(doc.indexOfPathPreferId(QString()), -1);
}


void SessionDocumentTest::appearance_on_document()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/a.jpg"));
    const SessionImageId id = doc.idAt(0);
    doc.seedBook().markSeedAttempted(id);
    QVERIFY(doc.seedBook().seedAttempted(id));
    // clear() drops path list and seed book together.
    doc.clear();
    QVERIFY(doc.isEmpty());
    QVERIFY(!doc.seedBook().seedAttempted(id));
}

void SessionDocumentTest::setPaths_clearsAppearance()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/old.jpg"));
    const SessionImageId oldId = doc.idAt(0);
    doc.seedBook().markSeedAttempted(oldId);
    QVERIFY(doc.seedBook().seedAttempted(oldId));

    doc.setPaths({QStringLiteral("/new.jpg")});
    QVERIFY(!doc.seedBook().seedAttempted(oldId));
    QVERIFY(!doc.seedBook().seedAttempted(doc.idAt(0)));
}

void SessionDocumentTest::replaceAll_keepsAppearance()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    const SessionImageId idA = doc.idAt(0);
    const SessionImageId idB = doc.idAt(1);
    doc.seedBook().markSeedAttempted(idA);

    // Swap order; same ids — seed flags must survive (replaceAll does not clear).
    doc.replaceAll({QStringLiteral("/b.jpg"), QStringLiteral("/a.jpg")}, {idB, idA});
    QCOMPARE(doc.idAt(0), idB);
    QCOMPARE(doc.idAt(1), idA);
    QVERIFY(doc.seedBook().seedAttempted(idA));
}

void SessionDocumentTest::removeAt_clearsAppearance()
{
    SessionDocument doc;
    doc.setPaths({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
    const SessionImageId idA = doc.idAt(0);
    const SessionImageId idB = doc.idAt(1);
    doc.seedBook().markSeedAttempted(idA);
    doc.seedBook().markSeedAttempted(idB);

    doc.removeAt(0);
    QVERIFY(!doc.seedBook().seedAttempted(idA));
    QVERIFY(doc.seedBook().seedAttempted(idB));
    QCOMPARE(doc.size(), 1);
    QCOMPARE(doc.idAt(0), idB);
}

QTEST_MAIN(SessionDocumentTest)
#include "sessiondocument_test.moc"
