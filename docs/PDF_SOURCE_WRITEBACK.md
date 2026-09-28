<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# PDF source write-back (deferred)

Status: **Not in scope for the current annotation / export work.**  
Recorded 2026-09-28 so a later version can pick this up without rediscovering the constraints.

Related: [ANNOTATION_OVERLAY.md](ANNOTATION_OVERLAY.md), [SESSION_EXPORT_AND_ORDER.md](SESSION_EXPORT_AND_ORDER.md),
[CONTENT_COORDINATES.md](CONTENT_COORDINATES.md).

---

## 1. What we do today

| Path | Mechanism | Result |
|------|-----------|--------|
| **Export Page as PDF** | Qt `QPrinter` / PDF paint device | New PDF that is a **rasterized (or vector-drawn) snapshot** of the view |
| **Session export** | `QPdfWriter` / similar for multi-page | Same idea: **paint** content into a new file |
| **Annotations** | Session/project JSON, drawn by `AnnotationPainter` | **Non-destructive** overlay; not written into the source PDF |

`QPrinter` does **not** create PDF `/Annot` dictionaries. Anything that looks “annotated” in the export is just **pixels or paths on the page**, not editable comments in Acrobat/Okular.

Thumtoo’s MuPDF integration is **read-oriented** (rasterize, page size, structured text). There is no public write path for annotations or structural edits yet.

---

## 2. What “real” PDF write-back would mean

Biltoo already keeps a **source URL / path** for each page (including `file.pdf//page:N`-style refs). Session edits (crop appearance, annotations, removals, appends of pages in the session list, etc.) are largely **non-destructive in the app model**: they sit on top of identity (`SessionImageId` + path), not by rewriting the file on disk as you work.

A later version could treat the **source PDF as the durable document** and apply biltoo’s non-destructive ops as **PDF-level changes** when the user explicitly saves/exports, for example:

- **Annotations** → PDF `/Annot` (`Highlight`, `Ink`, `Square`, `Circle`, `Line`, `Text`/sticky, …) with page-space → PDF user-space mapping
- **Crop / appearance** → crop boxes, optional page content transform, or soft-mask — product choice
- **Page removals / reorder / appends** → page-tree edits on a copy of the source PDF (not a flat re-render of the canvas)
- **Colour grade** → almost certainly still raster or optional form XObject; hard as pure PDF structure

That is a different pipeline from “print the view to PDF.”

---

## 3. Why it is deferred

- Needs a **writable PDF stack** (MuPDF write APIs in thumtoo, or another library) with tests and failure modes (encrypted PDFs, broken files, incremental update vs full save).
- Coordinate and box semantics (media vs crop vs bleed, `pageYUp`, OCR page space) must match [OCR_COORDINATES.md](OCR_COORDINATES.md) / content pipeline.
- Product decisions: overwrite vs “Save annotated copy…”, multi-file sessions, non-PDF pages in the same session, merge with existing `/Annot`s.
- Debugging PDF structure is **explicitly out of scope** for the current annotation feature pass; raster/Qt export and project JSON are enough for “share what I see” and “reload my marks in biltoo.”

---

## 4. Guidance for a future implementer

1. Do **not** bolt `/Annot` onto `QPrinter` export — keep raster export as the “visual snapshot” path.
2. Add thumtoo (or biltoo) APIs: open PDF → mutate page → save **copy**; keep source read-only until the user confirms.
3. Map only annotation kinds that have clear PDF analogues first (highlight quads, ink, rect/ellipse, line, sticky text).
4. Use stored `pageBounds` / `sourceKey` and live page size to validate geometry before write.
5. Leave session/project JSON as the biltoo-native source of truth until write-back is solid.

---

## 5. Explicit non-goals (for now)

- Debugging or implementing PDF structural write-back in the current tip.
- Replacing `exportPdf()` behaviour without a separate, opt-in “Save into PDF…” action.
