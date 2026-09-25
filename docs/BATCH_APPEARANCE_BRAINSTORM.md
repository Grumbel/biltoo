<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Batch appearance brainstorm — multi-page crop, autocrop, and friends

**Status:** brainstorm only. Not a roadmap or API contract.  
**Related:** [DOMAIN.md](../DOMAIN.md), [IDENTITY.md](../IDENTITY.md), [CONTENT-VARIANT.md](../CONTENT-VARIANT.md),
[GLOSSARY.md](../GLOSSARY.md), crop notes in session appearance / ContentXform.

---

## 1. The user story

You open a **scanned book** (or a PDF of one). Most pages share the same problem:
large margins, slight skew, content block roughly in the same place. You want to:

1. **Crop out the margins** once, in a way that applies to **many pages**.
2. **Skip** title page / back cover (or treat them differently).
3. Use **different crops for left vs right** pages (binding gutter, asymmetric scan).
4. Optionally **see all pages stacked** as a sum/median image so the “true” content
   region is obvious despite mis-registration.
5. **Tweak** any page afterwards with the normal single-image crop UI.

The same *batch apply* pattern is useful beyond crop: **flip, 90° rotate, colour
adjust, attention points** — with very different difficulty.

---

## 2. What biltoo already guarantees (constraints, not obstacles)

| Fact | Consequence for batch ops |
|------|---------------------------|
| Appearance is per **SessionImageId**, not path | Batch writes *N* independent appearance blobs; duplicates of the same file stay independent unless the user selects both. |
| Crop lives in **content / post-orient** space (`cropRect` + `cropSourceSize` + `cropRotation`) | A “shared” crop is only meaningful among pages with **comparable geometry** (same logical size, or normalised coordinates). |
| Non-destructive session | Batch crop never rewrites source files; undo should ideally reverse the whole batch or per-id. |
| Gallery / Facing already know **order** | Even/odd or verso/recto can be derived from session index (with explicit cover exceptions). |
| Single-item crop UI exists | Batch should **produce the same appearance fields** the manual UI edits — no parallel crop system. |

Batch is **multi-target write of existing appearance**, plus optional **assist
(preview / detect)** — not a new identity model.

---

## 3. Difficulty ladder

### 3.1 Easy — true multi-apply (same parameters)

Operations that are **parameter-identical** on every target:

| Op | Why easy |
|----|----------|
| **Flip H / V** | Boolean on appearance; no geometry dependency between pages. |
| **Content rotate ±90°** | Discrete; must run the same ContentXform mapping on each id’s crop if crop exists. |
| **Colour / contrast adjust** | If stored as a small param block per id, copy params to selection. |
| **Reset appearance** | Clear the same fields on each id. |

**UI:** selection (session multi-select, Gallery selection, or “pages 3–N”) +
Apply. Progress optional. Undo = restore previous appearance map for those ids.

**Pitfall:** rotate already has crop-rect mapping rules; batch must call the
*same* helpers as single-page rotate, not assign raw rects.

### 3.2 Medium — shared crop in a normalised frame

Same **crop recipe** applied to many pages:

| Approach | Idea |
|----------|------|
| **Absolute pixels** | Only works if every page has the **same logical size** (many phone-scan PDFs do; mixed sizes don’t). |
| **Normalised 0–1 rect** | `crop` as fractions of width/height; apply × each page’s logical size. Survives modest size differences. |
| **Content-box + margin** | Store “keep inset from each edge” or “keep central box”; still normalised. |

**Left/right (verso/recto):**

- Two recipes: `cropEven`, `cropOdd` (or `cropVerso`, `cropRecto`).
- Index rule: after optional **cover offset** (skip first page, or first *and*
  last), page `i` maps to even/odd in the *body* sequence — not always raw
  `index % 2` if the PDF includes front matter.
- **Facing** layout’s notion of pairs can inform defaults (“left page = even
  body index”) but the user must be able to swap.

**Exclusions:**

- Explicit set: “do not modify these SessionImageIds” (covers, plates, colour
  inserts).
- Or roles: `CoverFront`, `CoverBack`, `Body`, `Plate` — assigned by index
  heuristics + user override.

**Manual tweak after:** each id still has a normal cropRect; opening crop mode
on one page edits only that id. Batch is a **bulk initialiser**, not a live
linked constraint (unless we add an optional “linked crop group” later — see
§7).

### 3.3 Hard — autocrop and registration

Scanned pages **do not line up**. A single normalised rect will clip content on
some pages and leave margin on others if you only use page 5 as the template.

Layers of assistance (product can stop at any layer):

| Layer | What it does | Cost / risk |
|-------|----------------|-------------|
| **A. Template from one page** | User crops page *k* well; batch copies normalised rect (and mirror for opposite side). | Easy; fails on drift. |
| **B. Stack preview** | Build a composite of many pages so the user *sees* the union/intersection of content (§4). User sets crop on the composite; apply normalised. | Medium compute; huge UX win. |
| **C. Per-page detect** | Autodetect content bbox (or margins) per page; optional median size clamp. | Hard; needs robust detection; tunable aggressiveness. |
| **D. Registration + common crop** | Estimate shift/rotation per page to a reference; warp or offset crop. | Hardest; full scan-pipeline territory. |

Biltoo should not pretend to be ScanTailor on day one. **A + B + exclusions +
L/R recipes + manual tweak** already covers a large fraction of “crop this
book’s margins” without solving global registration.

---

## 4. Stack / sum preview (“onion skin” of pages)

**Goal:** one interactive image that represents “where content usually is.”

### 4.1 Compositing modes (conceptual)

| Mode | Behaviour | Good for |
|------|-----------|----------|
| **Mean / sum** | Average intensities | Soft “cloud” of content; bright where many pages agree. |
| **Median** | Per-pixel median | Rejects outliers (thumb, bookmark, ink blot on one page). |
| **Max / min** | Extreme | Rough union or intersection of dark text (depends on polarity). |
| **Variance** | Highlight disagreement | Shows mis-registration and outliers. |
| **Aligned stack** | Optional shift each page by estimated offset before composite | Cleaner box when scans drift; needs a cheap align step. |

Practical notes:

- Work at a **fixed max edge** (e.g. soft/LQIP or ≤1024) — this is a UI assist,
  not print proof.
- **Same logical aspect** helps; pad or letterbox mismatched pages explicitly.
- **Polarity:** scans may be grey text on white; inversion or “inkness” helps
  max/min modes.
- **Memory:** don’t hold full-res N pages; stream-accumulate or use a reservoir
  of downscaled buffers.
- **Selection:** composite only the **batch target set** (body pages), not
  covers, so the stack isn’t polluted.

### 4.2 Interaction

1. User selects targets (or “body pages”).
2. Opens **Batch crop** → stack preview builds asynchronously (progress).
3. User draws / adjusts **one crop** (and optionally a second for the other
   parity) in the **same crop chrome** as single-image mode, but the underlay is
   the composite.
4. Apply writes normalised (or absolute) rects to each id; opposite parity gets
   mirrored recipe if configured.
5. User reviews in Gallery (Facing helps); opens outliers for manual crop.

The composite is **not** stored as session appearance; it is a tool buffer.

---

## 5. Autocrop (if pursued)

Keep the host honest: autocrop is **proposal generation**, then the same apply
path as manual batch.

Possible signals (implementation detail for a later design):

- Margin detection from downscaled luminance / edge density.
- Largest high-contrast blob vs page border.
- Library assistance only if it fits the stack (OpenCV etc. is a dependency
  policy decision).

**Safety defaults:**

- Never apply autocrop to excluded covers by default.
- Clamp minimum content size (refuse to crop to a sliver).
- Prefer **inward** margin trim with a user “aggressiveness” slider.
- Always allow **preview before commit** (ghost rects on filmstrip or a
  before/after strip).

Skew correction (deskew) is a separate op from crop; combining them multiplies
failure modes. Sequence, don’t merge: optional deskew pass → then crop batch.

---

## 6. Selection model

Batch needs a clear **target set** of SessionImageIds:

| Source | Use |
|--------|-----|
| Gallery multi-select | Visual; good for ad-hoc sets. |
| Session range (“from–to”) | Book body: pages 2…N−1. |
| Parity filter | All even / all odd within range. |
| Role tags | Covers excluded; body only. |
| “All with empty crop” | Only uncropped pages. |
| “All matching size W×H” | Homogeneous scan batches. |

**Apply scope UI** should show **count + exclusions** before commit
(“Apply to 248 pages (skipped 2 covers)”).

---

## 7. Linked vs independent after apply

**Default (recommended):** after batch apply, each id owns an **independent**
cropRect. Editing one page does not update others. Matches today’s model and
undo simplicity.

**Optional later — crop group:**

- Group id on appearance; “edit linked” updates the shared recipe and
  re-instantiates per page (normalised).
- Dangerous if pages drift; better as explicit “re-apply template from this
  page to group.”

---

## 8. Colour, attention, and other multi-target ops

| Feature | Multi-apply story |
|---------|-------------------|
| **Flip / content rot** | §3.1 — ship first if any batch menu exists. |
| **Colour / contrast** | Same params → all targets; preview on one page then apply. Stack preview less critical. |
| **Attention points** | If points are in normalised content space, batch can *copy* a set of points, or *clear* them. Auto-detect “interesting points” on all pages is a different product (and easy to get wrong). Prefer copy/clear + per-page edit. |
| **Crop** | §3.2–3.3 — the headline feature; needs selection, L/R, exclusions, stack preview. |
| **Deskew** | Per-page angle estimate + content rotate/shear policy; harder than flip; keep separate from crop. |

**Unified shell concept:** a **Batch appearance** dialog or mode with:

- Target set builder  
- Operation tabs: Orient · Colour · Crop · (Attention)  
- Shared progress + undo bag  

Not four disconnected features that each invent selection UI.

---

## 9. Undo and performance

- **One undo step** for “batch crop 248 pages” (store previous appearance
  snapshots for those ids only).
- Apply on a **worker** where possible; appearance commit on GUI thread in
  chunks so Gallery can refresh.
- Don’t force full-res tile rebuild for every page at apply time; crop is
  appearance — display pipeline already respects content rect as samples
  upgrade.

---

## 10. Interaction with Facing / book layouts

Facing layout is a **presentation** of pairs; batch crop is **data** on ids.
Still:

- Preview apply result in **Facing** to judge gutter balance.
- Default “left/right recipe” can follow Facing’s verso/recto convention.
- Stack preview might show **two composites** (even stack / odd stack) side by
  side for gutter-aware crops.

---

## 11. What not to do

- Path-keyed batch maps (“all `/book/page*.jpg`”) — breaks duplicates and
  archive identity; always **SessionImageId**.
- Silent autocrop on open — too aggressive; explicit tool only.
- A second crop format that the single-page UI doesn’t understand.
- Full ScanTailor pipelines in-process before A+B exist.
- Assuming index 0 is always “front cover” without user confirmation.

---

## 12. Suggested slice order (if this ever leaves the napkin)

1. **Multi-select + multi-apply** flip / content rotate / reset (proves target
   set + undo).  
2. **Batch crop from template page** (normalised rect, optional mirror for
   parity, range + exclude ends).  
3. **Stack preview** (mean/median at low res) as the underlay for defining that
   template.  
4. **Even/odd recipes** and cover roles as first-class in the dialog.  
5. **Autocrop proposals** per page, still user-confirmed.  
6. Colour multi-apply; attention copy/clear.  
7. Only then: registration / deskew sophistication.

---

## 13. One-sentence summary

**Batch appearance is bulk writing of per-SessionImageId content params—
easy for orient and colour, medium for shared or mirrored crops with
exclusions, and hard for autocrop—best unlocked by a low-res stack preview and
the existing single-page crop UI as the final authority on every page.**

---

*End of brainstorm.*
