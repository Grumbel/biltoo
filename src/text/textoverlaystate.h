// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTOVERLAYSTATE_H
#define TEXTOVERLAYSTATE_H

#include <QVector>

/**
 * Presentation flags and interaction indices for page text overlays.
 *
 * Data-only (no Qt widgets): suitable for scripting / future plugin hosts.
 * The ImageView TextLayerController owns the live copy; the Text panel and
 * paint path both read this without owning region geometry.
 *
 * Inspired by buffer-local overlay properties (Emacs): presentation is a
 * thin layer over the region list, not a parallel object graph.
 */
struct TextOverlayState {
    /** Outline every region bbox (debug / layout). */
    bool showOutlines = false;
    /** Draw region UTF-8 glyphs inside each bbox (OCR/native text). */
    bool showGlyphs = false;
    /** Region under pointer (−1 = none). Panel and page share this. */
    int hoverRegion = -1;
    /** Stable region indices in reading order (empty = no selection). */
    QVector<int> selected;

    bool hasHover() const { return hoverRegion >= 0; }
    bool hasSelection() const { return !selected.isEmpty(); }

    bool setHover(int idx)
    {
        if (hoverRegion == idx) {
            return false;
        }
        hoverRegion = idx;
        return true;
    }

    bool setShowOutlines(bool on)
    {
        if (showOutlines == on) {
            return false;
        }
        showOutlines = on;
        return true;
    }

    bool setShowGlyphs(bool on)
    {
        if (showGlyphs == on) {
            return false;
        }
        showGlyphs = on;
        return true;
    }

    bool setSelected(const QVector<int> &ids)
    {
        if (selected == ids) {
            return false;
        }
        selected = ids;
        return true;
    }

    void clearInteraction()
    {
        hoverRegion = -1;
        selected.clear();
    }
};

#endif
