// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ContentUndoMacro — groups N>1 undos; no-op for single/empty.
 */

#include "util/contentundomacro.h"

#include <QUndoCommand>
#include <QUndoStack>
#include <QtTest/QtTest>

namespace {

class CountingCommand : public QUndoCommand
{
public:
    explicit CountingCommand(int *counter, QUndoCommand *parent = nullptr)
        : QUndoCommand(parent)
        , m_counter(counter)
    {
    }
    void redo() override
    {
        if (m_counter) {
            ++(*m_counter);
        }
    }
    void undo() override
    {
        if (m_counter) {
            --(*m_counter);
        }
    }
private:
    int *m_counter = nullptr;
};

} // namespace

class ContentUndoMacroTest : public QObject
{
    Q_OBJECT
private slots:
    void singleTarget_noMacro();
    void multiTarget_groupsUndo();
    void nullStack_safe();
};

void ContentUndoMacroTest::singleTarget_noMacro()
{
    QUndoStack stack;
    int n = 0;
    {
        ContentUndoMacro macro(&stack, QStringLiteral("one"), 1);
        stack.push(new CountingCommand(&n));
    }
    QCOMPARE(stack.count(), 1);
    QCOMPARE(n, 1);
    stack.undo();
    QCOMPARE(n, 0);
}

void ContentUndoMacroTest::multiTarget_groupsUndo()
{
    QUndoStack stack;
    int n = 0;
    {
        ContentUndoMacro macro(&stack, QStringLiteral("batch"), 3);
        stack.push(new CountingCommand(&n));
        stack.push(new CountingCommand(&n));
        stack.push(new CountingCommand(&n));
    }
    // One macro entry on the stack.
    QCOMPARE(stack.count(), 1);
    QCOMPARE(n, 3);
    stack.undo();
    QCOMPARE(n, 0);
    stack.redo();
    QCOMPARE(n, 3);
}

void ContentUndoMacroTest::nullStack_safe()
{
    ContentUndoMacro macro(nullptr, QStringLiteral("x"), 5);
    // Destructor must not crash.
}

QTEST_MAIN(ContentUndoMacroTest)
#include "contentundomacro_test.moc"
