// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONCOMMAND_H
#define ANNOTATIONCOMMAND_H

#include "annotation/annotationtypes.h"

#include <QUndoCommand>
#include <QVector>

class ImageView;

/** Add one annotation object (undo/redo). */
class AnnotationAddCommand : public QUndoCommand
{
public:
    AnnotationAddCommand(ImageView *view, SessionImageId sid, const Annotation::Object &obj,
                         const QRectF &pageBounds, bool pageYUp, const QString &text);

    void undo() override;
    void redo() override;

private:
    ImageView *m_view = nullptr;
    SessionImageId m_sid = kInvalidSessionImageId;
    Annotation::Object m_obj;
    QRectF m_pageBounds;
    bool m_pageYUp = false;
    bool m_applied = false;
};

/** Remove one or more annotation objects (eraser / clear / select-delete). */
class AnnotationRemoveCommand : public QUndoCommand
{
public:
    AnnotationRemoveCommand(ImageView *view, SessionImageId sid,
                            const QVector<Annotation::Object> &removed,
                            const QRectF &pageBounds, bool pageYUp, const QString &text);

    void undo() override;
    void redo() override;

private:
    ImageView *m_view = nullptr;
    SessionImageId m_sid = kInvalidSessionImageId;
    QVector<Annotation::Object> m_removed;
    QRectF m_pageBounds;
    bool m_pageYUp = false;
    bool m_applied = false;
};

/** In-place replace of one object (edit sticky text, future property edits). */
class AnnotationReplaceCommand : public QUndoCommand
{
public:
    AnnotationReplaceCommand(ImageView *view, SessionImageId sid,
                             const Annotation::Object &before, const Annotation::Object &after,
                             const QString &text);

    void undo() override;
    void redo() override;

private:
    ImageView *m_view = nullptr;
    SessionImageId m_sid = kInvalidSessionImageId;
    Annotation::Object m_before;
    Annotation::Object m_after;
    bool m_applied = false;
};

#endif // ANNOTATIONCOMMAND_H
