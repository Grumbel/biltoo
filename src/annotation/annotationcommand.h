// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONCOMMAND_H
#define ANNOTATIONCOMMAND_H

#include "annotation/annotationtypes.h"

#include <QUndoCommand>

class ImageView;

/** Add or remove one annotation object (undo/redo). */
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

#endif // ANNOTATIONCOMMAND_H
