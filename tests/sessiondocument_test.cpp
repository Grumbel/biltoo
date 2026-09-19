// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Characterization for SessionDocument (Phase 6 Tier 4 prerequisite).
 *
 * Locks path/id alignment, alloc never-reuse, and duplicate-path identity —
 * the contract Appearance-into-SessionDocument must preserve.
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
    void appearance_on_document();
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
    SessionDocument doc;
    doc.replaceAll({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")},
                   {1, 1}); // forced duplicate (legacy recovery path)
    QVERIFY(!doc.validateUniqueIds("dup ids"));
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

void SessionDocumentTest::appearance_on_document()
{
    SessionDocument doc;
    doc.append(QStringLiteral("/a.jpg"));
    const SessionImageId id = doc.idAt(0);
    WorkspaceItemState st;
    st.hasCrop = true;
    st.cropRect = QRect(2, 3, 40, 50);
    doc.appearance().set(id, st);
    QVERIFY(doc.appearance().contains(id));
    QCOMPARE(doc.appearance().get(id)->cropRect, QRect(2, 3, 40, 50));
    // Tier 4b: clear() drops path list and appearance together.
    doc.clear();
    QVERIFY(doc.isEmpty());
    QVERIFY(!doc.appearance().contains(id));
}

QTEST_MAIN(SessionDocumentTest)
#include "sessiondocument_test.moc"
