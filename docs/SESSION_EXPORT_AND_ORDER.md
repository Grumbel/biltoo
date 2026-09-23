# Session export and session order UI

Product notes (2026-09-23).

## Status

**v1 implemented (biltoo-2320+):** File → Export Images…
(directory / .cbz / multi-page PDF), bake via SessionAppearance,
File menu page exports mode-gated to Workspace.
Selection: filmstrip indices, else Gallery/Workspace `selectedSessionIndices`.
Progress dialog + cancel (2325). Export dialog UX (2340). See [RELEASE_0.2.0.md](RELEASE_0.2.0.md).

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

**Implemented (filmstrip v1, biltoo-2327):**

- Drag rows on the filmstrip; insertion **line** shows drop slot; drop calls
  `SessionDocument::replaceAll` via `MainWindow::reorderSessionRows` (undoable).
- Gallery / Workspace item order refresh after reorder; focus by SessionImageId.
- External path drag mime still present for canvas drops; strip-internal drop
  uses `application/x-biltoo-session-rows`.

**Keyboard / menu (2328):** Alt+←/→ (or ↑/↓ on vertical strip); context menu Move Left/Right/Start/End.

**Dialog (2330):** Gallery → Reorder Session… / Edit → Reorder Session… —
modal list with drag + Move Up/Down/Start/End; OK pushes the same undoable
`SessionReorderCommand` as filmstrip reorder.

**Done (2339–2343):** Gallery-canvas drag reorder + drag ghost (128px, multi badge).

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

