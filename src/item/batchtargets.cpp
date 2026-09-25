// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "item/batchtargets.h"

#include "imageview.h"
#include "imageitem.h"
#include "session/sessiondocument.h"

#include <QSet>
#include <QtGlobal>

namespace BatchTargets {

namespace {

void appendUnique(QList<BatchAppearanceTarget> &out,
                  QSet<SessionImageId> &seenIds,
                  QSet<ImageItem *> &seenItems,
                  SessionImageId sid,
                  const QString &path,
                  ImageItem *live,
                  int sessionIndex)
{
    if (live) {
        if (seenItems.contains(live)) {
            return;
        }
        seenItems.insert(live);
    }
    if (sid != kInvalidSessionImageId) {
        if (seenIds.contains(sid)) {
            // Prefer keeping an entry that already has a live pointer.
            for (BatchAppearanceTarget &t : out) {
                if (t.sessionId == sid && !t.live && live) {
                    t.live = live;
                    if (t.path.isEmpty()) {
                        t.path = path;
                    }
                    if (t.sessionIndex < 0) {
                        t.sessionIndex = sessionIndex;
                    }
                    return;
                }
                if (t.sessionId == sid) {
                    return;
                }
            }
        }
        seenIds.insert(sid);
    } else if (!live) {
        return;
    }

    BatchAppearanceTarget t;
    t.sessionId = sid;
    t.path = path;
    t.live = live;
    t.sessionIndex = sessionIndex;
    out.append(t);
}

} // namespace

QList<BatchAppearanceTarget> resolve(ImageView *view,
                                     Mode mode,
                                     const QList<SessionImageId> &filmstripIds,
                                     int rangeFrom,
                                     int rangeTo)
{
    QList<BatchAppearanceTarget> out;
    if (!view) {
        return out;
    }
    SessionDocument *doc = view->sessionDocument();
    QSet<SessionImageId> seenIds;
    QSet<ImageItem *> seenItems;

    auto addLive = [&](ImageItem *item) {
        if (!item) {
            return;
        }
        const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : view->hostResolveContentEditSessionId(item);
        const int idx = (doc && sid != kInvalidSessionImageId)
            ? doc->indexOfId(sid)
            : view->sessionListIndex(item);
        appendUnique(out, seenIds, seenItems, sid, item->path(), item, idx);
    };

    if (mode == Mode::Current) {
        ImageItem *item = view->targetItem();
        if (!item && view->isImageMode() && !view->liveItems().isEmpty()) {
            item = view->liveItems().first();
        }
        addLive(item);
        return out;
    }

    if (mode == Mode::IndexRange || mode == Mode::EvenIndices
        || mode == Mode::OddIndices) {
        if (!doc || doc->isEmpty()) {
            return out;
        }
        for (int i : sessionIndices(mode, doc->size(), rangeFrom, rangeTo)) {
            const SessionImageId sid = doc->idAt(i);
            const QString path = doc->pathAt(i);
            ImageItem *live = (sid != kInvalidSessionImageId)
                ? view->findItemBySessionId(sid)
                : nullptr;
            appendUnique(out, seenIds, seenItems, sid, path, live, i);
        }
        return out;
    }

    // Selection: live scene selection first.
    for (ImageItem *item : view->transformTargets()) {
        addLive(item);
    }
    // Filmstrip multi-select (covers Gallery virtual slots not on canvas).
    QList<SessionImageId> stripIds = filmstripIds;
    if (stripIds.isEmpty()) {
        stripIds = view->filmstripSelectedSessionIds();
    } else {
        for (SessionImageId sid : view->filmstripSelectedSessionIds()) {
            if (!stripIds.contains(sid)) {
                stripIds.append(sid);
            }
        }
    }
    if (doc) {
        for (SessionImageId sid : stripIds) {
            if (sid == kInvalidSessionImageId) {
                continue;
            }
            const int idx = doc->indexOfId(sid);
            const QString path = idx >= 0 ? doc->pathAt(idx) : QString();
            ImageItem *live = view->findItemBySessionId(sid);
            appendUnique(out, seenIds, seenItems, sid, path, live, idx);
        }
    }
    // Empty selection → current page.
    if (out.isEmpty()) {
        ImageItem *item = view->targetItem();
        if (!item && view->isImageMode() && !view->liveItems().isEmpty()) {
            item = view->liveItems().first();
        }
        addLive(item);
    }
    return out;
}

} // namespace BatchTargets
