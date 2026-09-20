// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PACKORDEROVERLAY_H
#define PACKORDEROVERLAY_H

#include "packorderview.h"
#include "sessiondocument.h"
#include "sessionpathorder.h"

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Future owner of Gallery pack-order state (Tier 4 residual).
 *
 * Today ImageView keeps a permanent SessionPathOrder (`m_pathOrderBook`). That
 * book cannot be deleted until multiplicity, mode-leave clear, stash, and
 * ad-hoc place are expressible without a second full ordered copy of the
 * session. PackOrderOverlay is the design vehicle for that replacement.
 *
 * ## Modes
 *
 * - **FollowDocument** — resolve() yields the document membership (or empty
 *   when no document is bound). Used when pack order is known to align with
 *   session membership (post-loadFiles, no LoadAdd extras).
 * - **Explicit** — resolve() yields the held SessionPathOrder. The held order
 *   may be empty: pathOrderClear / mode-leave must suppress pack even while
 *   SessionDocument still has membership (see PATH_ORDER.md and the dual-model
 *   characterization test `modeLeave_clearBook_documentPackWouldRegenerateIncorrectly`).
 *
 * Writes that today target the view book (clear / setOrder / appendRow) map
 * onto Explicit mode. When the explicit order again matches the document,
 * a future migration step may optionally collapse back to FollowDocument;
 * that collapse is *not* required for correctness and is not done here.
 *
 * This type is pure data + resolve. ImageView still stores m_pathOrderBook;
 * runtime adoption is a later tip after the ImageView characterization harness
 * is green.
 *
 * See docs/PATH_ORDER.md § PackOrderOverlay and REFACTOR.md Tier 4 residual.
 */
class PackOrderOverlay
{
public:
    enum class Mode {
        FollowDocument,
        Explicit,
    };

    Mode mode() const { return m_mode; }
    bool isExplicit() const { return m_mode == Mode::Explicit; }

    /** Explicit mode and the held order is empty (pack must stay blank). */
    bool isExplicitEmpty() const
    {
        return m_mode == Mode::Explicit && m_order.isEmpty();
    }

    /** Resolve pack order under the current mode. */
    PackOrderView resolve(const SessionDocument *doc) const
    {
        if (m_mode == Mode::FollowDocument) {
            if (doc) {
                return PackOrderView::fromDocument(*doc);
            }
            return PackOrderView();
        }
        return PackOrderView::fromBook(m_order);
    }

    /** Switch to FollowDocument; drops any held explicit order. */
    void followDocument()
    {
        m_mode = Mode::FollowDocument;
        m_order.clear();
    }

    /**
     * Explicit + empty. Models pathOrderClear / mode-leave: pack is blank
     * even when the document still lists open files.
     */
    void clearExplicit()
    {
        m_mode = Mode::Explicit;
        m_order.clear();
    }

    void setExplicit(const QStringList &paths, const QVector<SessionImageId> &ids)
    {
        m_mode = Mode::Explicit;
        m_order.setOrder(paths, ids);
    }

    void setExplicit(const SessionPathOrder &order)
    {
        m_mode = Mode::Explicit;
        m_order = order;
    }

    void setExplicit(const PackOrderView &view)
    {
        setExplicit(view.paths(), view.ids());
    }

    void appendExplicitRow(const QString &path,
                           SessionImageId id = kInvalidSessionImageId)
    {
        if (m_mode != Mode::Explicit) {
            // Promote from FollowDocument: caller is adding a row that may
            // break alignment (LoadAdd / ad-hoc). Start from empty explicit
            // and append; callers that need document rows first must seed
            // via setExplicit(fromDocument) themselves.
            m_mode = Mode::Explicit;
            m_order.clear();
        }
        m_order.appendRow(path, id);
    }

    /** Held order; only meaningful when isExplicit(). */
    const SessionPathOrder &explicitOrder() const { return m_order; }

    int explicitSize() const { return m_order.size(); }

    int countPathOccurrences(const QString &path, const SessionDocument *doc) const
    {
        return resolve(doc).countPathOccurrences(path);
    }

private:
    Mode m_mode = Mode::FollowDocument;
    SessionPathOrder m_order;
};

#endif // PACKORDEROVERLAY_H
