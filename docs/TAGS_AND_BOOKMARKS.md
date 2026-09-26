<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tags and bookmarks — scope brainstorm

Status: **ideas only**. biltoo does not implement a user tags/bookmarks system
yet. Closest existing features:

| Existing | What it is | Not the same as |
|----------|------------|-----------------|
| **Recent Sessions** | Auto list of path lists (reopen a session) | User-curated, named, permanent |
| **Recent Projects** | Auto list of `.biltoo` project files | User bookmarks into a book |
| **Document TOC** (`TocPanel`) | Outline from the file (PDF/EPUB/…) | User marks |
| **Attention points** | Per-image interest points (detect/edit) | Named bookmarks / tags |
| **Text selection / search** | Ephemeral or find hits | Saved places |
| **Annotations** (brainstorm) | Drawn ink on the page | See [FEATURE_BRAINSTORM.md](FEATURE_BRAINSTORM.md) §6 |

This note decides **scope axes** before UI or storage.

Related: [IDENTITY.md](../IDENTITY.md) (`SessionImageId`), [FEATURE_BRAINSTORM.md](FEATURE_BRAINSTORM.md)
(annotations, package format), [TEXT_OVERLAY.md](TEXT_OVERLAY.md).

---

## 1. Vocabulary (keep two words)

Avoid one overloaded “bookmark.”

| Term | Meaning (proposal) |
|------|-------------------|
| **Bookmark** | A **place** you can jump to: session + target (+ optional view). Named optional. |
| **Tag** | A **label** on a target (file, page, region). Many-to-many; used to filter/search. |

A bookmark may carry tags; a tag is not inherently a jump target (unless the UI
lists “all places tagged X”).

**Star / favourite** can be a boolean tag or a special bookmark list — pick one
story and stick to it (prefer: star = tag `★` or a dedicated flag on a bookmark).

---

## 2. Target granularity (what can be marked?)

From coarse to fine:

| Level | Target | Jump behaviour | Notes |
|-------|--------|----------------|-------|
| **A. Path / document** | Filesystem path or archive root | Open session containing it / focus first page | Weak for multi-page; duplicates paths |
| **B. Session row** | `SessionImageId` (preferred) or list index | Open project/session, select that id, Image mode | Aligns with IDENTITY / filmstrip |
| **C. Page in document** | Path + page index (PDF/DjVu/EPUB page-ref) | Open at that page | Needed when one file is many pages |
| **D. Point on page** | B or C + image-space point | Open + pan/zoom so point is visible | Like attention, but named/listable |
| **E. Rectangle on page** | B or C + image-space rect | Open + fit rect (Zoom-tool style) | Quote, figure, panel |
| **F. Text region** | B or C + region index / text anchor | Open + select/highlight region | Ties to text layer / TTS |

**Recommendation for v1**

- Support **B + C** solidly (session image / page).
- Allow optional **D or E** on the same record (`kind: page | point | rect`).
- **F** when a stable text layer exists; store region index + text snippet for
  display when OCR is rebuilt.

Do **not** key durable bookmarks only by path string when the session can
duplicate the same path (IDENTITY).

---

## 3. Scope of the collection (where do marks live?)

| Scope | Lifetime | Good for |
|-------|----------|----------|
| **In-project** | Saved in `.biltoo` project | “My marks in this book/workspace” |
| **Application library** | QSettings / user data dir, across sessions | “Places I care about in any book” |
| **Both** | Project holds local; library can pin “global” copies | Power users |

**Recent Sessions** stays automatic and capped. Bookmarks are **explicit**,
named or unnamed, user-deleted.

**Recommendation:**  

- **Project-scoped** bookmarks as the default (travel with the work).  
- Optional **Library** menu (global) for cross-project pins — same data shape,
  different store. Mirror Recent Projects vs in-project paths.

---

## 4. Tags vs bookmarks vs annotations

```
Tag ────────── labels / filter  (no geometry required)
Bookmark ───── navigable place (geometry optional)
Annotation ─── visible ink/vectors on the page (always geometry + style)
Attention ──── tool/chrome points (may feed “suggest bookmark”)
```

| Question | Proposal |
|----------|----------|
| Is a rect bookmark an annotation? | **No** by default — bookmark is navigation metadata; annotation is drawing. A command “Create annotation from bookmark” is fine later. |
| Can an annotation be bookmarked? | Yes: bookmark target = annotation id once annotations exist. |
| Tags on annotations? | Yes later; v1 tags on bookmark targets (page/image) only. |
| Attention → bookmark? | “Bookmark attention points” batch action. |

Mixing everything into one “mark” type with a `type` enum is possible in storage
but confuses menus. Prefer **separate lists** with shared target addressing.

---

## 5. Data sketch (not a schema freeze)

```text
Bookmark {
  id              // stable uuid
  title           // user string; default from page label / text snippet
  created / modified
  sessionHint     // optional last project path or session name
  target {
    sessionImageId?   // preferred when in-project
    path?             // fallback / global library
    pageRef?          // document page when path is multi-page
    kind: page | point | rect | text_region
    point? | rect?    // content / image coordinates
    regionIndex?      // text layer
    textSnippet?      // for UI when layer changes
  }
  tags[]            // strings or tag ids
  view? {           // optional restore
    scale, center   // or "fit-rect"
  }
}
```

Tags as a flat string list is enough for v1; a Tag table (color, icon) can wait.

---

## 6. UI surfaces

### 6.1 Dedicated **Bookmarks** menu (like Recent, but user-owned)

```
Bookmarks
  ├── Bookmark this page          (B/C)
  ├── Bookmark selection…       (E or F when applicable)
  ├── ────────────
  ├── Chapter 3 – figure        → jump
  ├── …
  ├── ────────────
  ├── Manage Bookmarks…
  └── (optional) Library…
```

Not mixed into Recent Sessions. Recent stays automatic; Bookmarks stays curated.

### 6.2 Side panel (optional)

- List/filter by tag, search titles.
- Same pattern as TOC panel but **user** data.
- Filmstrip badges (dot/star) for bookmarked session rows.

### 6.3 Context menus

- Filmstrip / gallery tile: Bookmark · Tag…
- Image view: Bookmark this page · Bookmark visible rect (from Zoom tool or selection)
- Text region: Bookmark this region

### 6.4 Tags UI

- Lightweight: completer on a line edit; filter chips in Manage dialog.
- Avoid a full taxonomy app in v1.

### 6.5 Tools interaction

- Select/Zoom already define rects — “Bookmark rect” reuses geometry.
- Pan does not create marks.
- Do not require a new tool for v1 bookmarks.

---

## 7. Behaviours worth deciding early

| Behaviour | Options | Lean |
|-----------|---------|------|
| Jump when target missing | Warn + remove / keep orphan | Keep orphan, mark broken |
| Duplicate bookmarks | Allow / unique per target | Allow; Manage can merge |
| Order | Manual sort + created | Manual order in project |
| Sync view on jump | Restore scale/center or only page | Page + optional fit-rect |
| Export | Inside project; library JSON | Project first |
| Privacy | Local only | No cloud |

---

## 8. Phased scope

### Phase 1 — Page bookmarks (project)

- Bookmark current session image / page (`SessionImageId` + pageRef if any).
- Bookmarks menu + Manage dialog (rename, delete, reorder).
- Jump opens the right row in Image mode.
- No tags required.

### Phase 2 — Tags + filter

- String tags on bookmarks; filter in Manage / panel.
- Filmstrip indicator for bookmarked rows.

### Phase 3 — Geometry

- Point / rect bookmarks; jump fits or centers.
- Text-region bookmarks when layer present.

### Phase 4 — Global library

- Application-wide bookmark store; menu section or Library window.
- Resolve path → open or prompt if file moved.

### Later

- Tie-in to annotations; smart suggestions (attention, search hits);
  package format embeds project bookmarks ([FEATURE_BRAINSTORM.md](FEATURE_BRAINSTORM.md) §1).

---

## 9. Out of scope for the first cut

- Replacing Recent Sessions/Projects
- Social/shared tags
- Full PDF “outline editor” (TOC remains document-defined)
- Treating every annotation as a bookmark automatically

---

## 10. Open questions

1. Default bookmark title: filename, page number, TOC entry nearest, text snippet?
2. Should Workspace multi-select “Bookmark all selected” create many bookmarks or one group?
3. Global library keying when the same file appears under two paths (hash?).
4. Are tags only on bookmarks, or also on bare session images without a bookmark record?

---

## See also

- Recent Sessions / Projects: shell history menus (automatic, capped)
- TOC: `TocPanel` + thumtoo outline
- Identity: [IDENTITY.md](../IDENTITY.md)
- Annotations / package: [FEATURE_BRAINSTORM.md](FEATURE_BRAINSTORM.md)
