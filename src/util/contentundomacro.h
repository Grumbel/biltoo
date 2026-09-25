// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CONTENTUNDOMACRO_H
#define CONTENTUNDOMACRO_H

#include <QString>
#include <QUndoStack>

/**
 * Groups N content undos under one QUndoStack macro when targetCount > 1.
 * Single-target apply pushes a normal command (no beginMacro).
 * Used by orient, colour grade, crop panel batch, and reset appearance.
 */
class ContentUndoMacro
{
public:
    explicit ContentUndoMacro(QUndoStack *s, const QString &text, int targetCount)
        : m_stack(s && targetCount > 1 ? s : nullptr)
    {
        if (m_stack) {
            m_stack->beginMacro(text);
        }
    }
    ~ContentUndoMacro()
    {
        if (m_stack) {
            m_stack->endMacro();
        }
    }
    ContentUndoMacro(const ContentUndoMacro &) = delete;
    ContentUndoMacro &operator=(const ContentUndoMacro &) = delete;

private:
    QUndoStack *m_stack = nullptr;
};

#endif
