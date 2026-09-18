// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COLORADJUSTCOMMIT_H
#define COLORADJUSTCOMMIT_H

#include "imageview_types.h"

#include <QString>

/**
 * Pending durable colour-grade commit target after slider idle debounce.
 * QTimer stays on ImageView; this bag only holds sid + path.
 */
struct ColorAdjustCommit {
    SessionImageId sid = kInvalidSessionImageId;
    QString path;

    void schedule(SessionImageId s, const QString &p)
    {
        sid = s;
        path = p;
    }

    /** Copy target and clear; returns false when nothing was pending. */
    bool take(SessionImageId *outSid, QString *outPath)
    {
        if (sid == kInvalidSessionImageId && path.isEmpty()) {
            return false;
        }
        if (outSid) {
            *outSid = sid;
        }
        if (outPath) {
            *outPath = path;
        }
        sid = kInvalidSessionImageId;
        path.clear();
        return true;
    }

    void clear()
    {
        sid = kInvalidSessionImageId;
        path.clear();
    }

    bool pending() const
    {
        return sid != kInvalidSessionImageId || !path.isEmpty();
    }
};

#endif // COLORADJUSTCOMMIT_H
