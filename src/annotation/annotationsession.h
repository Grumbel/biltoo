// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONSESSION_H
#define ANNOTATIONSESSION_H

#include "annotation/annotationtypes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

/** In-memory annotation store keyed by SessionImageId. */
class AnnotationSession
{
public:
    bool isVisible() const { return m_visible; }
    void setVisible(bool on) { m_visible = on; }

    bool isEmpty() const { return m_pages.isEmpty(); }

    Annotation::Page *page(SessionImageId sid)
    {
        auto it = m_pages.find(sid);
        return it == m_pages.end() ? nullptr : &it.value();
    }
    const Annotation::Page *page(SessionImageId sid) const
    {
        auto it = m_pages.constFind(sid);
        return it == m_pages.cend() ? nullptr : &it.value();
    }

    Annotation::Page &ensurePage(SessionImageId sid, const QRectF &pageBounds, bool pageYUp)
    {
        auto it = m_pages.find(sid);
        if (it == m_pages.end()) {
            Annotation::Page pg;
            pg.sid = sid;
            pg.pageBounds = pageBounds;
            pg.pageYUp = pageYUp;
            it = m_pages.insert(sid, pg);
        }
        return it.value();
    }

    void addObject(SessionImageId sid, const Annotation::Object &obj,
                   const QRectF &pageBounds, bool pageYUp)
    {
        Annotation::Page &pg = ensurePage(sid, pageBounds, pageYUp);
        if (!pg.pageBounds.isValid() && pageBounds.isValid()) {
            pg.pageBounds = pageBounds;
            pg.pageYUp = pageYUp;
        }
        pg.objects.append(obj);
    }

    bool removeObject(SessionImageId sid, quint64 id)
    {
        auto it = m_pages.find(sid);
        if (it == m_pages.end()) {
            return false;
        }
        auto &objs = it.value().objects;
        for (int i = 0; i < objs.size(); ++i) {
            if (objs.at(i).id == id) {
                objs.removeAt(i);
                if (objs.isEmpty()) {
                    m_pages.erase(it);
                }
                return true;
            }
        }
        return false;
    }

    bool findObject(SessionImageId sid, quint64 id, Annotation::Object *out) const
    {
        const Annotation::Page *pg = page(sid);
        if (!pg) {
            return false;
        }
        for (const Annotation::Object &o : pg->objects) {
            if (o.id == id) {
                if (out) {
                    *out = o;
                }
                return true;
            }
        }
        return false;
    }

    void clearPage(SessionImageId sid) { m_pages.remove(sid); }
    void clearAll() { m_pages.clear(); }

    quint64 nextId() { return ++m_nextId; }

    QJsonArray toJson() const
    {
        QJsonArray a;
        for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
            if (!it.value().objects.isEmpty()) {
                a.append(Annotation::pageToJson(it.value()));
            }
        }
        return a;
    }

    void fromJson(const QJsonArray &a)
    {
        m_pages.clear();
        m_nextId = 0;
        for (const QJsonValue &v : a) {
            Annotation::Page pg = Annotation::pageFromJson(v.toObject());
            for (const Annotation::Object &o : pg.objects) {
                if (o.id > m_nextId) {
                    m_nextId = o.id;
                }
            }
            if (pg.sid != kInvalidSessionImageId) {
                m_pages.insert(pg.sid, pg);
            }
        }
    }

    const QHash<SessionImageId, Annotation::Page> &pages() const { return m_pages; }

private:
    QHash<SessionImageId, Annotation::Page> m_pages;
    quint64 m_nextId = 0;
    bool m_visible = true;
};

#endif // ANNOTATIONSESSION_H
