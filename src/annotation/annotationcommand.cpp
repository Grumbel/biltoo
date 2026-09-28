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

AnnotationRemoveCommand::AnnotationRemoveCommand(ImageView *view, SessionImageId sid,
                                                 const QVector<Annotation::Object> &removed,
                                                 const QRectF &pageBounds, bool pageYUp,
                                                 const QString &text)
    : QUndoCommand(text)
    , m_view(view)
    , m_sid(sid)
    , m_removed(removed)
    , m_pageBounds(pageBounds)
    , m_pageYUp(pageYUp)
{
}

void AnnotationRemoveCommand::undo()
{
    if (!m_view || !m_applied) {
        return;
    }
    for (const Annotation::Object &o : m_removed) {
        if (!m_view->hostAnnot().session().findObject(m_sid, o.id, nullptr)) {
            m_view->hostAnnot().session().addObject(m_sid, o, m_pageBounds, m_pageYUp);
        }
    }
    m_applied = false;
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AnnotationRemoveCommand::redo()
{
    if (!m_view) {
        return;
    }
    for (const Annotation::Object &o : m_removed) {
        m_view->hostAnnot().session().removeObject(m_sid, o.id);
    }
    m_applied = true;
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

AnnotationReplaceCommand::AnnotationReplaceCommand(ImageView *view, SessionImageId sid,
                                                   const Annotation::Object &before,
                                                   const Annotation::Object &after,
                                                   const QString &text)
    : QUndoCommand(text)
    , m_view(view)
    , m_sid(sid)
    , m_before(before)
    , m_after(after)
{
}

void AnnotationReplaceCommand::undo()
{
    if (!m_view || !m_applied) {
        return;
    }
    m_view->hostAnnot().session().updateObject(m_sid, m_before);
    m_applied = false;
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AnnotationReplaceCommand::redo()
{
    if (!m_view) {
        return;
    }
    if (!m_view->hostAnnot().session().updateObject(m_sid, m_after)) {
        // Object missing (e.g. erased under us) — re-add after.
        Annotation::Page *pg = m_view->hostAnnot().session().page(m_sid);
        const QRectF bounds = pg ? pg->pageBounds : QRectF();
        const bool yUp = pg ? pg->pageYUp : false;
        m_view->hostAnnot().session().addObject(m_sid, m_after, bounds, yUp);
    }
    m_applied = true;
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


AnnotationMoveCommand::AnnotationMoveCommand(ImageView *view, SessionImageId sid,
                                             const QVector<Annotation::Object> &before,
                                             const QVector<Annotation::Object> &after,
                                             const QString &text)
    : QUndoCommand(text)
    , m_view(view)
    , m_sid(sid)
    , m_before(before)
    , m_after(after)
{
}

void AnnotationMoveCommand::apply(const QVector<Annotation::Object> &objs)
{
    if (!m_view) {
        return;
    }
    for (const Annotation::Object &o : objs) {
        if (!m_view->hostAnnot().session().updateObject(m_sid, o)) {
            Annotation::Page *pg = m_view->hostAnnot().session().page(m_sid);
            const QRectF bounds = pg ? pg->pageBounds : QRectF();
            const bool yUp = pg ? pg->pageYUp : false;
            m_view->hostAnnot().session().addObject(m_sid, o, bounds, yUp);
        }
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void AnnotationMoveCommand::undo()
{
    if (!m_view || !m_applied) {
        return;
    }
    apply(m_before);
    m_applied = false;
}

void AnnotationMoveCommand::redo()
{
    if (!m_view) {
        return;
    }
    apply(m_after);
    m_applied = true;
}
