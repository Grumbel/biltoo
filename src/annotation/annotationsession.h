// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANNOTATIONSESSION_H
#define ANNOTATIONSESSION_H

#include "annotation/annotationtypes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

/**
 * In-memory annotation store keyed by SessionImageId.
 *
 * Entity model (data-driven, not OO inheritance):
 *   SessionImageId → Page (bounds + ordered Object list)
 * Objects are plain structs; tools/painters switch on Kind.
 * Same identity role as ItemWorld sparse tables for appearance.
 */
class AnnotationSession
{
public:
    bool isVisible() const { return m_visible; }
    void setVisible(bool on) { m_visible = on; }

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

    /** In-place replace by id (keeps list order / z). Returns false if missing. */
    bool updateObject(SessionImageId sid, const Annotation::Object &obj)
    {
        auto it = m_pages.find(sid);
        if (it == m_pages.end()) {
            return false;
        }
        auto &objs = it.value().objects;
        for (int i = 0; i < objs.size(); ++i) {
            if (objs.at(i).id == obj.id) {
                objs[i] = obj;
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
    void clearAll()
    {
        m_pages.clear();
        m_nextId = 0;
    }

    quint64 nextId() { return ++m_nextId; }

    /**
     * Envelope or legacy array. Always writes formatVersion ≥ 2.
     * Visible flag is session UI state, not persisted (optional later).
     */
    QJsonObject toJsonObject() const
    {
        QJsonObject root;
        root.insert(QStringLiteral("formatVersion"), Annotation::kFormatVersion);
        QJsonArray pages;
        for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
            if (!it.value().objects.isEmpty()) {
                pages.append(Annotation::pageToJson(it.value()));
            }
        }
        root.insert(QStringLiteral("pages"), pages);
        return root;
    }

    /** Project file still stores a JSON value (object preferred, array legacy). */
    QJsonValue toJsonValue() const { return toJsonObject(); }

    /** @deprecated Prefer toJsonValue / toJsonObject. Legacy array-only export. */
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

    void fromJsonValue(const QJsonValue &v)
    {
        m_pages.clear();
        m_nextId = 0;
        QJsonArray pages;
        if (v.isObject()) {
            const QJsonObject root = v.toObject();
            pages = root.value(QStringLiteral("pages")).toArray();
            if (pages.isEmpty() && root.contains(QStringLiteral("sid"))) {
                // Single page object mistaken for envelope — tolerate.
                pages.append(root);
            }
        } else if (v.isArray()) {
            pages = v.toArray();
        }
        for (const QJsonValue &pv : pages) {
            Annotation::Page pg = Annotation::pageFromJson(pv.toObject());
            for (const Annotation::Object &o : pg.objects) {
                if (o.id > m_nextId) {
                    m_nextId = o.id;
                }
            }
            if (pg.sid != kInvalidSessionImageId && !pg.objects.isEmpty()) {
                m_pages.insert(pg.sid, pg);
            }
        }
    }

    void fromJson(const QJsonArray &a) { fromJsonValue(a); }

    const QHash<SessionImageId, Annotation::Page> &pages() const { return m_pages; }

private:
    QHash<SessionImageId, Annotation::Page> m_pages;
    quint64 m_nextId = 0;
    bool m_visible = true;
};

#endif // ANNOTATIONSESSION_H
