// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PACKORDEROVERLAY_H
#define PACKORDEROVERLAY_H

#include "session/packorderview.h"
#include "session/sessiondocument.h"
#include "session/sessionpathorder.h"

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Owner of Gallery pack-order state on ImageView (Tier 4 residual).
 *
 * ImageView stores a PackOrderOverlay (`m_pathOrderOverlay`) instead of a bare
 * SessionPathOrder. Multiplicity, mode-leave clear, stash, and ad-hoc place
 * still need Explicit mode (possibly empty) so pack does not fall through to
 * SessionDocument membership alone.
 *
 * ## Modes
 *
 * - **FollowDocument** — resolve() yields the document membership (or empty
 *   when no document is bound). Used when pack order is known to align with
 *   session membership (post-loadFiles, no LoadAdd extras). Reachable via
 *   pathOrderSetOrder → tryCollapseToFollowDocument when order aligns.
 * - **Explicit** — resolve() yields the held SessionPathOrder. The held order
 *   may be empty: pathOrderClear / mode-leave must suppress pack even while
 *   SessionDocument still has membership (see PATH_ORDER.md and the dual-model
 *   characterization test `modeLeave_clearBook_documentPackWouldRegenerateIncorrectly`).
 *
 * ImageView host mutators: public pathOrderClear / pathOrderSetOrder write
 * Explicit (setOrder may collapse when aligned). Private pathOrderAppendRow
 * also writes Explicit (place/add only). pathOrderClear stays Explicit empty
 * (must not follow membership). Collapse is storage-only — not required for
 * correctness.
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

    /**
     * Append one explicit row. If currently FollowDocument, promote to Explicit
     * seeded from @p seedDoc (when non-null) so existing membership is not
     * dropped before the append (LoadAdd / ad-hoc place after collapse).
     */
    void appendExplicitRow(const QString &path,
                           SessionImageId id = kInvalidSessionImageId,
                           const SessionDocument *seedDoc = nullptr)
    {
        if (m_mode != Mode::Explicit) {
            m_mode = Mode::Explicit;
            if (seedDoc) {
                m_order.setOrder(seedDoc->paths(), seedDoc->ids());
            } else {
                m_order.clear();
            }
        }
        m_order.appendRow(path, id);
    }

    /**
     * If Explicit order aligns with @p doc membership, switch to FollowDocument
     * (drop the held copy). No-op when misaligned, empty-suppress vs non-empty
     * doc, or @p doc is null. Storage savings only — not required for correctness.
     * @return true if collapsed.
     */
    bool tryCollapseToFollowDocument(const SessionDocument *doc)
    {
        if (m_mode != Mode::Explicit || !doc) {
            return false;
        }
        if (!PackOrderView::fromBook(m_order).alignsWithDocument(*doc)) {
            return false;
        }
        followDocument();
        return true;
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
