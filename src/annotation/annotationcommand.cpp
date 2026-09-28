// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "annotation/annotationcommand.h"

#include "imageview.h"

AnnotationAddCommand::AnnotationAddCommand(ImageView *view, SessionImageId sid,
                                           const Annotation::Object &obj,
                                           const QRectF &pageBounds, bool pageYUp,
                                           const QString &text)
    : QUndoCommand(text)
    , m_view(view)
    , m_sid(sid)
    , m_obj(obj)
    , m_pageBounds(pageBounds)
    , m_pageYUp(pageYUp)
{
}

void AnnotationAddCommand::undo()
{
    if (!m_view || !m_applied) {
        return;
    }
    m_view->hostAnnot().session().removeObject(m_sid, m_obj.id);
    m_applied = false;
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AnnotationAddCommand::redo()
{
    if (!m_view) {
        return;
    }
    if (!m_view->hostAnnot().session().findObject(m_sid, m_obj.id, nullptr)) {
        m_view->hostAnnot().session().addObject(m_sid, m_obj, m_pageBounds, m_pageYUp);
    }
    m_applied = true;
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}
