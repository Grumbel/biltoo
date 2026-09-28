<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Bookshelf / library (design)

Status: **Phase 1 in tree** (menu pin list). Recorded 2026-09-28.

Related: Recent Sessions menu, project files (`.biltoo`), [TAGS_AND_BOOKMARKS.md](TAGS_AND_BOOKMARKS.md).

---

## Goal

Keep **user-chosen** sessions/documents available permanently inside the app —
not only the automatic Recent list (time-ordered, capped, easy to lose).

---

## Phase 1 — Menu only (in tree)

Menu bar **Bookshelf**: Add Current Session; click entry to open; **Delete** (or Backspace) on a highlighted entry removes the pin. Persistence: QSettings array `bookshelf` (path lists).


Mirror **Recent Sessions**:

| Action | Behaviour |
|--------|-----------|
| **Add to Bookshelf** | Pin current session root (folder, PDF/EPUB path, or project) with a display title |
| **Bookshelf** submenu | Ordered list of pins; activate → open that path/project like Recent |
| **Remove from Bookshelf** | On the submenu item or context on the entry |

**Storage:** QSettings or a small JSON under the app data dir (path + title +
optional last page / project path). User-controlled membership, not MRU eviction.

**Why start here**

- Reuses open/session plumbing already used by Recent  
- No new mode or layout  
- Validates what people want to pin (file vs folder vs project) before UI investment  

---

## Phase 2 — Optional richer list

Dedicated dock or dialog: search, rename, folders/tags, drag reorder. Still
list-centric, not a cover grid.

---

## Phase 3 — Graphical “level above Gallery” (later)

A **cover shelf** mode: one tile per book/session, open → current Gallery/Image
for that book. Needs:

- Cover art policy (first page, embedded EPUB cover, user image)  
- Mode ownership vs Gallery/Workspace ([MODE_OWNERSHIP.md](MODE_OWNERSHIP.md))  
- Scaling to hundreds of entries without loading every document  

**Do not start here** until Phase 1 data model and open paths are solid.

---

## Recommendation

Ship **Phase 1 (menu)** first. Treat the graphical shelf as a separate product
slice, not the MVP.
