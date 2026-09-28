<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# View, focus, and selection language

**Status:** design / cleanup plan. Not fully implemented.  
**Related:** [DOMAIN.md](../DOMAIN.md), [SPREAD.md](SPREAD.md), [TEXT_OVERLAY.md](TEXT_OVERLAY.md),
[MODE_OWNERSHIP.md](MODE_OWNERSHIP.md), [ACTIVITY.md](ACTIVITY.md).

This note names the different “which thing am I dealing with?” concepts in
biltoo, how they collide today, and a clearer model for users and code.

---

## 1. Why this exists

Users (and agents) currently face overlapping words:

| Word used in UI / code | Often means… |
|------------------------|--------------|
| Current / cursor | Session row the status bar and filmstrip “are on” |
| Selection | Filmstrip multi-select, Gallery tiles, Workspace items, *or* text regions |
| Highlight | TTS speech, search hits, hover, *or* selected text |
| Focus | Keyboard focus of a widget, *or* the page under consideration |
| View | Image / Gallery / Workspace mode, *or* zoom/pan camera, *or* Double View |

The filmstrip is the sharpest example: **Qt list selection** and **session
cursor** are forced into one visual language (palette Highlight), so “pages I
am looking at” and “pages I picked for an operation” look the same.

Goal: **one concept → one name → one colour family → one interaction story.**

---

## 2. Inventory (as the code actually behaves)

### 2.1 Session membership (world data)

- Ordered list of `SessionImageId` + paths ([IDENTITY.md](../IDENTITY.md)).
- Not a UI selection; the universe of pages the session owns.

### 2.2 Session cursor (navigation focus)

- **What:** One preferred session row: `m_currentIndex` / `SessionIdentity`.
- **Drives:** status filename, classic Image underlay identity, filmstrip
  “current row”, slideshow advance, many “do it to this page” shortcuts.
- **Changes with:** Prev/Next, filmstrip click (Image), Gallery arrow keys,
  opening a tile, spread anchor updates.
- **Not:** a multi-set. At most one cursor (plus Dual compare’s secondary pane,
  which is a second cursor on the same world).

### 2.3 Spread membership (reading surface)

- **What:** Ordered `SpreadState.members` — pages on the Image surface together
  ([SPREAD.md](SPREAD.md)).
- **Relation to cursor:** Cursor/anchor is usually the first member or the
  filmstrip’s current row; membership can be FixedN window or explicit
  Selection set.
- **Visual today:** Multiple underlays; status range `12–13 / 200`. Filmstrip
  does **not** specially mark non-cursor members.

### 2.4 Session selection (page set for commands)

- **What:** Multi-set of session ids for batch ops: remove, duplicate, View
  Selection, Apply colour/crop to selection, Open in new window.
- **Surfaces:**
  - Filmstrip: `QListWidget` selection (`selectedSessionIds`).
  - Gallery: scene item selection / multi-select.
  - Workspace: selected `ImageItem`s.
- **Modifiers:** Ctrl = toggle, Shift = range (list / pack order). Image-mode
  filmstrip often forces single-select navigation instead of multi-select.
- **Confusion:** In Image mode, clicking a thumb often **moves the cursor**
  and collapses selection to one row — same gesture as “select for batch”.

### 2.5 Text selection (content set)

- **What:** `TextSelection` / `selectedRegions` — region indices (+ multi-page
  bag) for Copy text, Speak selection, panel highlight.
- **Modifiers:** Shift+drag rubber-band (and Select tool drag); not the same as
  session Shift-range.
- **Visual today:** Blue fill/outline on regions (`QColor(40, 140, 255, …)`).

### 2.6 Speech highlight (playback activity)

- **What:** Regions currently being spoken (progress bar on active region).
- **Visual today:** Green (`QColor(40, 200, 100, …)`); Gallery can ring
  member paths.
- **Nature:** Transient **activity**, not a selection the user edits.

### 2.7 Search hits

- **What:** Match ranges for in-page / session search.
- **Visual:** Distinct overlay (not the same as text selection). Should stay
  out of the selection colour family.

### 2.8 Hover / affordances

- Link hover tip, edge zones (prev/next/Up), tool cursors (Pan hand, etc.).
- Ephemeral; must not use selection blue or speech green as the primary fill.

### 2.9 View camera (viewpoint)

- Zoom/pan matrix on the canvas (fit / fill / free zoom). Orthogonal to *which
  pages* are members; confuses users when “Fill” frames one page of a spread
  (fixed in 2743.x for multi-underlay).

### 2.10 Tools and modifiers (canvas)

| Input | Typical meaning today |
|-------|------------------------|
| Tool Select | Rubber-band text or Workspace pick |
| Tool Pan | Drag viewport |
| Tool Zoom | Region zoom |
| Shift | Text rubber-band *or* session range select (context-dependent) |
| Ctrl | Session multi-toggle *or* reserved |
| Alt | Content transform / move pages (Workspace; crop-related paths) |

Same keys mean different worlds depending on mode — learnable if **mode chrome
states the rule**, painful if not.

---

## 3. Proposed terminology (user-facing + code)

Prefer these names in UI strings, docs, and new symbols:

| Term | Meaning | Avoid calling it |
|------|---------|------------------|
| **Session** | The ordered document of pages | “list”, “album” alone |
| **Cursor** | The one page navigation is “on” | “selection”, “current selection” |
| **Page selection** | Multi-set of pages for commands | “selection” alone when text exists |
| **Spread** / **reading set** | Pages on the Image surface together | “double view selection” |
| **Text selection** | Selected OCR/text regions | “highlight” |
| **Speech mark** | TTS playback position | “selection”, “highlight” alone |
| **Search marks** | Search hit overlays | “selection” |
| **Camera** | Zoom/pan of the canvas | “view” when mode is meant |
| **Mode** | Image / Gallery / Workspace | “view” |

**Cursor vs page selection (law):**

- Cursor answers: *Where am I reading / navigating?*
- Page selection answers: *What set will the next command affect?*
- They may coincide (single-select Image) but must remain **separable** in
  Gallery/Workspace and on the filmstrip.

**Spread vs page selection:**

- Spread is **presentation** (what is drawn on the surface).
- Page selection is **command target**.
- “View Selection” *copies* page selection into spread membership; after that,
  navigating the spread may slide membership without clearing page selection
  (policy choice — see §6).

---

## 4. Unified colour language

Semantic colours, independent of light/dark theme (theme maps these roles to
concrete QColor / palette roles):

| Role | Hue family | Used for | Not for |
|------|------------|----------|---------|
| **Select** | **Blue** | Page selection chrome, text selection fill, “will be affected by command” | Cursor, speech, search |
| **Activity** | **Green** | Speech mark, optional in-progress decode/activity rings | Selection, cursor |
| **View** | **Yellow / amber** | Session cursor on filmstrip, spread-member marks, “you are here”, camera-related HUD chips | Selection fills |
| **Search** | **Violet / magenta** (optional fourth) | Search hits only | Everything else |
| **Hover** | Neutral alpha of Select or View | Mouse-over only | Persistent state |

### 4.1 Filmstrip (concrete)

| State | Visual |
|-------|--------|
| Cursor only | **Amber** left edge or frame (View role); not full-cell blue |
| Page selection | **Blue** selection wash or outline (Select role) |
| Cursor ∩ selected | Amber edge **plus** blue wash |
| Spread member (not cursor) | Small amber mark / double-edge, no blue |
| Speech on that page | Optional green dot (Activity), never replaces cursor/select |

Today both cursor and selection use `QPalette::Highlight` → collapse to one
blue. Cleanup must split **Qt item selection** (page selection) from **cursor
decoration** (custom paint role).

### 4.2 Canvas text

| State | Visual |
|-------|--------|
| Text selection | Blue fill (keep; already matches Select) |
| Speech mark | Green fill / progress (keep; already matches Activity) |
| Search hits | Violet outline or underline |
| Region hover | Thin neutral or light blue outline, not solid selection |

### 4.3 Gallery / Workspace

- Selected tiles: Select blue.
- “Opened as cursor when returning to Image”: View amber indicator on that
  cell if still visible in a pack (optional).
- Speech rings on Gallery (spread TTS): Activity green.

---

## 5. Keyboard model (who moves what)

### 5.1 Principle

**Arrow keys move the cursor** (and the reading surface when a spread is
active).  
**Shift/Ctrl+click (and explicit Select-all) edit page selection.**  
**Text selection is pointer-primary** (drag); keyboard extends only when a
text caret exists (future) — do not overload arrows for text while reading.

### 5.2 By mode

| Mode | ←/→ or spatial arrows | Shift+click / Ctrl+click | Alt |
|------|----------------------|---------------------------|-----|
| **Image** | Cursor ±1 or spread stride | Filmstrip: page selection (enable multi-select) | Reserved for content tools |
| **Gallery** | Cursor among tiles (spatial) | Page selection on tiles | — |
| **Workspace** | Optional nudge cursor among selected; prefer tool | Page/object selection | Move/transform pages |

### 5.3 Spread

- With FixedN/Selection spread: **Next/Prev move the reading set** (cursor
  follows anchor), not “extend page selection”.
- Extending page selection while reading uses filmstrip/Gallery modifiers,
  not the same keys as Next page.

### 5.4 Ambiguity to ban in UI copy

- Do not say “Select next page” for Next; say **“Next page”** or **“Next spread”**.
- Do not say “Selection” in status for the cursor; say **“Page 12 of 200”** or
  **“Pages 12–13 of 200”**.

---

## 6. Interaction matrix (cleanup targets)

| Situation | Desired behaviour |
|-----------|-------------------|
| Filmstrip, Image mode, single click | Move **cursor** (and spread window if FixedN); do not require blue selection to mean “current” |
| Filmstrip, Ctrl/Shift click | Edit **page selection**; cursor may stay put or move to the clicked row (pick one rule and document it — recommendation: **cursor moves to clicked row**, selection updates independently) |
| Gallery open tile | Set cursor + enter Image; page selection optional |
| View Selection | Page selection → spread membership; enable HUD Up; Next slides reading window |
| Copy / Speak | Prefer **text selection** if non-empty; else command is page-level |
| TTS | Only Activity green; does not clear text selection |
| Zoom Fit/Fill | Camera frames **spread bounds** when multi-underlay |

---

## 7. Implementation plan (next step — not this commit’s code)

Phased so each step is shippable:

### P0 — Names and docs only

- Adopt terminology in new UI strings and status tips.
- Keep this document authoritative.

### P1 — Colour roles API

- Central `ChromeColor` / `SessionChromeRoles` (Select / Activity / View /
  Search) used by filmstrip, text paint, Gallery rings.
- Map from theme (light/dark) once.

### P2 — Filmstrip cursor ≠ selection

- Custom paint: cursor = View amber; selection = Select blue.
- Image mode: allow multi-select on filmstrip without losing cursor chrome.
- Status line uses “Page(s) …” not “selected”.

### P3 — Keyboard and modifiers

- Table in §5 enforced: arrows → cursor/spread only.
- Tooltips on filmstrip and Gallery state Shift/Ctrl rules.
- Alt remains Workspace/content transform only.

### P4 — Spread membership chrome

- Filmstrip marks all spread members (View role).
- Optional Gallery amber “in spread” for FixedN (low priority).

### P5 — Text / speech / search separation audit

- Confirm no shared brushes between the three.
- Copy text / Speak labels already distinguish text vs page.

---

## 8. Decisions (accepted 2026-09-28)

1. **Cursor on Ctrl+click:** **move cursor** to the clicked page so status and
   Image stay aligned with the last clicked thumb.
2. **Page selection when entering Image from Gallery:** **keep** multi-select
   for return; cursor = opened page.
3. **Selection policy spread + filmstrip click:** **keep Selection membership**
   until Double View is toggled (do not snap to FixedN on click).
4. **Search colour:** **violet** (fourth role), distinct from speech green.

### P2 progress

- Filmstrip: Select blue wash vs View amber cursor edge.
- Filmstrip: **camera viewport** rectangle on the cursor thumb (normalised
  content coords), updated from Image camera on status refresh.
- `ChromeColors` helper (`src/shell/chromecolors.h`).

---

## 9. One-sentence summary

**Cursor is where you are (yellow); page/text selection is what you target
(blue); speech is what the system is doing (green); camera is how you look;
spread is which pages share the reading surface.**

