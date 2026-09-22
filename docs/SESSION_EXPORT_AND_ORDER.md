# Session export and session order UI

Product notes (2026-09-23).

## Status

**v1 implemented (biltoo-2320+):** File → Export Images…
(directory / .cbz / multi-page PDF), bake via SessionAppearance,
File menu page exports mode-gated to Workspace.
Selection: filmstrip indices, else Gallery/Workspace `selectedSessionIndices`.
Progress dialog + cancel (2325). Reorder UI still open.

## 1. Session image export (Gallery / Image)

Distinct from **Workspace** page export (single PNG/PDF of the paper guide).

| | Workspace export (exists) | Session export (planned) |
|--|--|--|
| Unit | One composed **page** | One **file per session item** (or selection) |
| Bake | Scene layout on paper | Per-item content appearance: rotate, flip, crop (+ optional grade) |
| Output | Single PNG / single-page PDF | **Directory**, **.cbz**, or **multi-page PDF** |
| Sources | Untouched | **Never rewritten** — only new files |

Pipeline (read-only on disk): session order → decode full (or max long-edge) →
`SessionAppearance::applyContentToImage` → encode JPEG/PNG → write under export
root / zip (cbz) / PDF pages.

File menu:

- Rename existing sheet exports to **Export page as PNG/PDF…** (Workspace-primary).
- Add **Export images…** for session bake (Gallery / Image).
- Mode-gate: page guide, page PNG/PDF off outside Workspace; session export off when session empty.

Build order: menu gating → directory export → cbz → multi-page PDF → selection-only / edge cap / cancel.

## 2. Session reorder UI (related, separate)

**Need:** a clear UI to **reorder images in the session** (filmstrip / Gallery order).

Today order is mostly open/add/sort-driven. Session export, slideshow, and filmstrip
all follow session order — without reorder, users cannot define deliverable
sequence without re-opening files.

**Desired (sketch only):**

- Reorder **session membership** (`SessionDocument` paths + ids), not only the
  Gallery pack overlay (`docs/PATH_ORDER.md`).
- Affordances: drag-reorder in filmstrip and/or a simple ordered list dialog;
  move left/right (or up/down) for selection; optional “move to front/back”.
- After reorder: filmstrip, Gallery pack (when following document), slideshow,
  and future session export all see the same order.
- Do not confuse with Workspace z-order (raise/lower on the page).

When implementing, read `PATH_ORDER.md` so document vs `PackOrderOverlay` stay
consistent (FollowDocument vs Explicit).

## 3. File menu mode matrix (planned)

| Action | Gallery | Image | Workspace |
|--------|---------|-------|-----------|
| Print / preview | later / weak | current image | page |
| Page setup | — | — | yes |
| Print page guide | no | no | yes |
| Export page PNG/PDF | no | maybe single view | yes |
| Export images… (bake) | yes | yes | optional later |
| Export text | if doc pages | if doc page | weak |
| Workspace background | no | no | yes |

