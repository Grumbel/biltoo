<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Plain text and Markdown support (deferred design)

Status: **Not implemented — waiting on MuPDF ≥ 1.28 in the stack.** Recorded 2026-09-28.

Related: thumtoo `PathKind` / `EPUB.md`, biltoo session expand.

---

## Hope: “MuPDF like ePub”

**Not a drop-in.** ePub is a first-class MuPDF document type (spine of XHTML).
thumtoo already has `PathKind::Epub`, `epub_page_uri`, rasterize, layout CSS.

MuPDF’s `fz_open_document` path does **not** treat arbitrary `.txt` / `.md` the
same way. Typical MuPDF document kinds are PDF, XPS, EPUB, CBZ, FB2 (and
HTML/SVG in some builds) — not “open this markdown file as pages.”

So enabling `.md` / `.txt` is **new thumtoo work**, not a biltoo file-filter
tweak.

---

## Practical options (lightest first)

| Approach | Effort | UX |
|----------|--------|-----|
| **A. Synthetic HTML → MuPDF HTML/EPUB path** | Medium (thumtoo) | Wrap text/md as HTML (or one-shot mini-EPUB), reflow with existing ePub layout knobs | 
| **B. QTextDocument pages in biltoo** | Medium (biltoo-only) | Render text to page-sized images/items; no thumtoo | 
| **C. External convert (pandoc → PDF/EPUB)** | Low code, high ops | User or helper produces a real document | 

**Recommendation:** if we want one stack, **A in thumtoo** (classify `.txt` /
`.md`, convert to HTML with sensible CSS, open via the same document/page URI
machinery as ePub). Markdown needs a small parser or a constrained subset
(headers, lists, code fences) unless we vendor a library.

**Not “easy”** relative to ePub: new `PathKind`, expand URIs, cache keys,
encoding (UTF-8), and reflow/layout tests.

---

## Non-goals for a first cut

- Full CommonMark / GFM fidelity  
- Live editing of `.md` in biltoo  
- PDF write-back of text notes (see [PDF_SOURCE_WRITEBACK.md](PDF_SOURCE_WRITEBACK.md))


---

## MuPDF 1.28 Markdown (wait)

MuPDF **1.28.0-rc1** (2026-06-19) adds Markdown document support. That is likely
the right long-term path (open `.md` like other MuPDF docs, similar to ePub).

NixOS / our flake currently ship **MuPDF 1.27.2**, which does not include that.
**Do not** implement a parallel `.md` pipeline in biltoo/thumtoo until the
packaged MuPDF is ≥ 1.28 (or we deliberately vendor a newer MuPDF).

`.txt` may still need a separate decision (synthetic HTML vs future MuPDF).
