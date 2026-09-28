<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Session vs PDF/ZIP container

Status: **Open design tension** — no chosen fix yet.  
Recorded so later work can build on a shared problem statement.

Related: [IDENTITY.md](../IDENTITY.md), [PATH_ORDER.md](PATH_ORDER.md),
[PDF_SOURCE_WRITEBACK.md](PDF_SOURCE_WRITEBACK.md), [ANNOTATION_OVERLAY.md](ANNOTATION_OVERLAY.md),
thumtoo locator / appearance model.

---

## 1. The conflict in one paragraph

A **Session** is an ordered bag of **pages** (`SessionImageId` + path). Those pages
may come from many PDFs, ZIPs, or loose images. Product operations (reorder,
drop a page, annotate, crop) feel natural at the **session** level. Persistence
and “the document I opened,” however, often live at the **container** level
(the PDF file, the archive, the folder). Path/locator-keyed durable state
(orient, flip, annotations) strengthens the *file* story; session membership
and project JSON strengthen the *collection* story. Neither story fully owns
“I removed pages 3–5 from this PDF” or “keep this multi-PDF reading order as
the document.”

The origin is not *gone* (it sits in the path / locator / `file.pdf//page:N`
style ref), but it is **flattened into a list of leaves**. Container structure
(page tree, archive member list, bookmarks, TOC) is not a first-class object
in biltoo’s session model.

---

## 2. What each layer actually owns today

| Concern | Owner today | Key |
|---------|-------------|-----|
| Decode / raster | thumtoo + path | Path / locator / page ref |
| Orient, flip, grade (durable XDG) | locator appearance DB | Locator id (file-ish) |
| Annotations (durable XDG) | same DB, `locator_annotations` | Locator id |
| Crop (bound) | ItemWorld / appearance | **SessionImageId** |
| Membership, order, remove/reorder | SessionDocument + pack order | **SessionImageId** + paths∥ids |
| Full “reload my work” | `.biltoo` project | Session-shaped JSON |
| PDF page tree / ZIP member list | **Not modeled** | — |
| Write back into source PDF | Deferred ([PDF_SOURCE_WRITEBACK.md](PDF_SOURCE_WRITEBACK.md)) | — |

So: **session is the product surface**; **locator is the durable file surface**;
**container structure is mostly implicit in path strings**.

---

## 3. Where the model strains

1. **Multi-origin sessions**  
   Open book A (PDF), append chapter from book B, drop two pages, reorder.  
   Session order is clear. “Save back into the PDF” is not — there is no single
   container, and dropped pages are only *absent from the session*, not deleted
   from A’s page tree.

2. **Same file, two slots**  
   IDENTITY requires independent appearance per `SessionImageId`. Path-keyed
   durable annotations/orient *share* across slots. That is consistent with
   “file-level memory” and inconsistent with “two independent readings of the
   same page.”

3. **Gallery-first open of a PDF**  
   User mental model: “this grid *is* the PDF.” Implementation: N session
   images that happen to share a parent path prefix / page refs. Removing a
   tile ≠ removing a PDF page from disk.

4. **ZIP / comic / folder**  
   Same pattern: archive is a container; session is an expanded member list.
   Reorder in session does not reorder the ZIP central directory.

5. **Annotations + write-back**  
   Durable marks are path-keyed (good for “open this file again”). Project
   marks are session-keyed. PDF `/Annot` write-back needs a **page index inside
   a document**, which is container-relative, not session-list-relative once
   the session has been shuffled or multi-sourced.

6. **sourceKey / pageBounds**  
   Annotation pages can record a soft identity of the source at write time.
   That helps detect remap/size change; it does not reify the container.

---

## 4. Design directions (ideas only — not a plan)

None of these are endorsed; they are options to reduce the tension when a
product choice is clear.

### A. Keep the dual model, name it loudly

- **Session** = working set / reading order / project.  
- **Container** = file on disk; durable XDG is *file* memory.  
- UI copy: “Remove from session” vs never “Delete page from PDF” until
  write-back exists.  
- Cheap; does not add structure; reduces false expectations.

### B. Optional container spine

Introduce a first-class **Container** (or **Document**) object:

- Identity: locator of the PDF/ZIP (or folder root).  
- Children: ordered page refs (page index / member path).  
- Session becomes a **view or playlist** over one or more containers
  (subset, interleave, annotations as overlays).

Reorder/remove *inside a single-container session* can mean either playlist
edit or (with write-back) page-tree edit. Multi-container sessions stay
playlists.

Heavier model; clarifies origin; needs UI for “playlist vs document.”

### C. Session-only truth + explicit “link to file”

- Everything durable that is *editorial* (marks, crop, order) lives only in
  project / session id.  
- Path XDG keeps only pure file hints (orient) or is dropped for annotations.  
- Opening a bare PDF never restores session order or dropped pages — only
  file-level hints.

Simplifies mental model for power users who always save projects; weakens
“open file, marks are still there” unless projects are automatic.

### D. Derived sessions from containers

- Opening a PDF *always* creates a container-backed session with a stable
  mapping `SessionImageId ↔ (container, pageIndex)`.  
- Append from another PDF attaches a second container.  
- Persistence stores **container ops** (drop page index 4 of container X,
  reorder within X) plus **playlist ops** (interleave).  

Recoverable origin; serialization is more than a flat path list.

### E. Soft grouping without full spine

- Tag consecutive session rows that share a container locator as a **group**
  in the UI (filmstrip brackets, Gallery section headers).  
- No new persistence; helps users see origin; reorder across groups is
  clearly a playlist edit.

### F. Write-back as the resolver

- Do not model page-tree deletes until “Save into PDF…” exists.  
- Until then, session remove = playlist only; document on disk unchanged.  
- Aligns with [PDF_SOURCE_WRITEBACK.md](PDF_SOURCE_WRITEBACK.md).  
- Annotations stay biltoo-native until `/Annot` mapping is real.

---

## 5. Questions that should drive a choice

1. Is the primary artifact a **session/project** or a **PDF/ZIP file**?  
   (Different answers for “research binder” vs “annotate this paper.”)

2. When the user removes a page in Gallery, do they mean **leave the working
   set** or **edit the file**?

3. Should two slots of the same page ever hold **different** durable marks?

4. Must multi-PDF sessions remain first-class, or is single-container the
   common case we optimize?

5. Is automatic XDG restore without a project a hard requirement for
   annotations, or only for orient/flip?

---

## 6. Suggested non-goals (until a direction is picked)

- Silently treating session order as PDF page-tree order.  
- Path-keyed durable state that pretends to **delete** PDF page-tree entries
  (hide/filter flags are a different, reversible layer — see §9).  
- Write-back that flattens a multi-origin session into one PDF without an
  explicit merge/export wizard.

---

## 7. Practical near-term hygiene (compatible with any direction)

- Prefer UI strings that say **session** / **working set** for remove/reorder.  
- Keep locator refs and `file.pdf//page:N` intact on every session row (origin
  breadcrumb).  
- Document that durable annotations are **file-level**, like orient
  ([ANNOTATION_OVERLAY.md](ANNOTATION_OVERLAY.md)).  
- Project file remains the only place that stores full membership + order +
  per-`SessionImageId` appearance together.  
- If container spine (B/D) is ever built, durable annotations may need a key
  of `(locator, pageIndex)` rather than locator alone for multi-page files —
  page refs already encode index in the path string today; verify that remains
  stable under renames and incremental PDF saves.

---

## 8. Summary

The unresolved conflict is not missing path data; it is **missing a product
object for “the PDF/ZIP as a structured document”** while the UI is built
around **“an ordered list of pages.”** Locator-backed durable state and
session-backed project state pull in opposite directions. A later design can
pick explicit playlist semantics (A/C/F), introduce a container spine (B/D),
or only group in the UI (E) — but should avoid implying page-tree edits until
write-back and keying rules are chosen.

---

## 9. Emerging direction: container view-state + explicit mix sessions

User sketch (2026-09-28) — refine, do not implement yet.

### 9.1 Proposal

| Operation | Binding | Persistence sketch |
|-----------|---------|----------------------|
| **Hide** (“remove from view”) | Per page, by **location** (locator + page ref) | Durable flag on container page; reversible |
| **Reorder** within one PDF/ZIP | Per **container** (not per session slot) | Ordered list of page refs under that locator |
| **Mix** pages from different documents | Explicit **session / project** | Only place multi-origin order lives |
| **Show original** | Per container (or global) | One-click toggle: apply vs ignore local view-state; **keep** stored modifications |

Mental model:

- Opening a single PDF/ZIP loads **container + its view-state** (hide + order +
  orient/marks as today).
- Hiding a page is not a page-tree delete and not “gone forever.”
- Interleaving two books requires an explicit session (playlist over containers).
- Modified document should be **obvious**; user can flip to virgin layout/content
  presentation without wiping the stored edits.

This sits between options **A** (honest dual model) and **B/D** (container
spine): container gets durable **view-state**, session stays the mix layer.

### 9.2 What this gets right

- Aligns “I don’t want to see page 3 of *this* PDF next time” with path/locator
  durability (same family as orient/annotations).
- Avoids forcing every PDF open through a saved project.
- Makes multi-document order an explicit product object (session), which matches
  IDENTITY’s “ordered list of images.”
- “Show original” preserves work (overlay off / filter off), unlike clear/reset.

### 9.3 Things easy to overlook

1. **Page identity stability**  
   Order and hide lists must key on stable page refs (`page:N`, archive member
   path), not session list index. PDF incremental save, linearize, or “save as”
   can renumber or rewrite objects; ZIP rename of members breaks member paths.
   Need a defined failure mode (drop orphan hides, or match by content hash).

2. **Hide vs session remove**  
   Two verbs:
   - *Hide in container* → durable, applies whenever that container is opened
     alone.
   - *Remove from session* → playlist only; page still exists when the PDF is
     opened fresh (unless also hidden).  
   UI must not use one word for both.

3. **Show original — what does it cover?**  
   Stack of layers, each toggleable or one master switch:
   - container order (custom vs file order)
   - hide flags (show hidden pages again)
   - orient / flip / grade
   - crop
   - annotations  
   “Original” might mean *layout only* (order+hide) or *everything visual*.
   Prefer a master switch that **ignores** view-state without deleting it, plus
   optional per-layer controls later.

4. **Dirty / modified indicator**  
   Needs a precise definition: any non-default order, any hide, any appearance,
   any annotation? Indicator per container in Gallery vs per session? Opening a
   mixed session: which document is “modified”?

5. **Default open path**  
   Single PDF open → apply container view-state automatically?  
   Then “show original” is essential.  
   If default is virgin and view-state is opt-in, discoverability suffers.

6. **Duplicates in one container**  
   Rare for PDF page trees; possible if session appends the same page twice.
   Container order is a permutation of unique page refs; session can still
   duplicate slots with independent crop (IDENTITY). Hide-by-location would
   hide *all* slots of that page in a container-native view — maybe correct.

7. **Mixed session + container order**  
   When session interleaves book A and B, whose order wins inside A’s runs?
   Rule of thumb: **session order is absolute** for mixed sessions; container
   order applies only when the open set is a single container (or when
   expanding a container into a session for the first time).

8. **Empty visible set**  
   User hides all pages → open shows nothing. Need “show original” or “unhide
   all” recovery, not a blank brick.

9. **Export / write-back**  
   Hide is still not a PDF page-tree delete. Export “visible pages only” vs
   “full file” must be explicit. Write-back remains deferred.

10. **Annotations on hidden pages**  
    Still stored; still path-keyed. Hidden pages should not paint in Gallery but
    marks remain when unhidden. Eraser/clear while hidden is a corner case.

11. **Filmstrip / slideshow / search**  
    Do they respect hide flags? Probably yes for container-native views; session
    mode follows session membership only.

12. **Concurrency / two windows**  
    Same locator view-state is global (XDG). Two sessions on one PDF share hide
    and order — surprising if one user expected session-local hides.

### 9.4 Suggested invariant (if this direction is taken)

```
container view-state  →  single-origin open (hide, order, file-level marks)
session / project     →  multi-origin membership and interleaving
show-original         →  presentation filter over stored view-state (non-destructive)
page-tree / ZIP rewrite → only via explicit write-back (still deferred)
```

### 9.5 Open product choices (still required)

- Exact scope of “show original” (layout vs all layers).  
- Whether container view-state applies on every bare PDF open.  
- Whether hide is allowed inside a mixed session or only in single-container UI.  
- Badge/copy for “modified document” vs “modified session.”

