<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Annotation cutouts — future mode (brainstorm)

Status: **idea only** — not implemented. Captured so the annotated-pages list
work does not grow into a second layout system without a design note.

Related: [ANNOTATION_OVERLAY.md](ANNOTATION_OVERLAY.md),
[DOMAIN.md](../DOMAIN.md) (Image / Gallery / Workspace),
[MODE_OWNERSHIP.md](MODE_OWNERSHIP.md).

## Problem

The annotation panel can list **pages that already have marks** and jump to
them. That still shows each mark **in context** on the full page.

Users often want the inverse: see the **annotated regions by themselves**,
like cutting clippings out of a newspaper — each highlight, sticky, or ink
stroke as a small card, without the surrounding page chrome.

## Desired experience (sketch)

- A grid or packed board of **cutouts**: each is a tight crop around one
  annotation object (or a merged cluster of overlapping objects on the same
  page).
- Optional caption: page label, tool kind, colour chip, object count.
- Click a cutout → jump to the source page and select that object (same
  identity as `SessionImageId` + object id).
- Filter by kind (highlighter / pen / sticky / shape) and by page.

## Why not the current panel

The Annotations dock is **tool chrome** (colour, width, visibility) plus a
simple **navigation list**. Embedding a cutout board there would mix two
jobs and fight the dock’s narrow width.

## Likely home: Workspace (or a fourth mode)

| Option | Fit | Notes |
|--------|-----|--------|
| **Workspace** | Strong | Free arrangement already exists; cutouts could be lightweight items with a link back to the source page object. |
| **Gallery** | Weak | Gallery is session overview / pack order, not scrapbook. |
| **Image mode overlay** | Weak | Full-page reading stays primary; cutouts are review, not reading. |
| **New “Clippings” mode** | Possible | Only if Workspace semantics (pose, export) are a poor match. Prefer extending Workspace first. |

## Data and rendering

- Source of truth remains `AnnotationSession` (page-space objects).
- Cutout bounds = object bounds in page space (pad a few page units), mapped
  through the same content coordinates as paint/export.
- Raster: either a **lazy crop** from the page display surface or a dedicated
  soft sample; avoid baking into the source file.
- Cluster rule (optional later): union of overlapping highlighters on one page
  → one card.

## Out of scope for v1 of this idea

- PDF write-back of cutouts as a new document
- OCR of cutout images
- Sharing / cloud sync of the board

## Next steps when picked up

1. Prototype cutout bounds + one card in Workspace from a single sticky.
2. Decide whether cutouts are **session-only chrome** or durable in `.biltoo`.
3. Keep the Annotations panel list as the lightweight “jump to page” UI.
