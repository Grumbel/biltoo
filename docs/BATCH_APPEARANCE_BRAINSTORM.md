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

## 12. Agreed direction (2026-09-25)

Build order, refined from the difficulty ladder:

1. **Multi-apply orient / reset + undo bag** — flip H/V, content ±90°, reset
   appearance on a target set. Proves selection, batch commit, and one undo
   step for N ids.  
   **Shipped:** `transformTargets()` already multi-applies orient. `ContentUndoMacro`
   groups N content undos for flip/rotate when |targets|>1. Reset snapshots
   before/after and pushes ContentCommands under the same macro. Limited to
   **live** selected ImageItems (virtual Gallery slots need materialise/select).
2. **Colour multi-apply** — same param block as [AdjustmentsPanel](../src/shell/adjustmentspanel.h)
   (`ColorAdjustments`), apply to targets; panel already has live preview on
   *one* image.  
   **Shipped:** Adjustments **Apply to selection** → `applyColorAdjustmentsToTargets`
   + undo macro; sliders still edit primary only.
3. **Crop panel** (dock, same family as colour) — see §14. Hard-coded / manual
   values + autocrop-with-threshold + post-margins + reset crops; batch apply
   to the target set.  
   **Shipped (v1):** View → Show Crop Panel. **BatchTargets** Current/Selection/Range/Even/Odd for crop+colour+orient. Manual margins / Autocrop+threshold /
   extra margins; Apply/Reset current & selection; `CropRecipeUtil` +
   `CropController::applyCropRecipeTo*`. Live preview and normalised fields still open.  
4. **Later** — template-from-page UX (§15), stack/sum preview, even/odd
   recipes, cover roles, registration/deskew.

Stack preview and “perfect book pipeline” stay **after** a useful panel exists.

---

## 13. One-sentence summary

**Batch appearance is bulk writing of per-SessionImageId content params;
ship orient/reset and colour multi-apply first, then a colour-like crop panel
(manual values, autocrop+threshold, extra margins, reset), and only later
template/stack assists for hard scanned books.**

---

## 14. Crop panel as intermediate (like AdjustmentsPanel)

A dock **Crop panel** is a better next product step than a full batch-crop
wizard. Mirror the colour panel’s habits: controls in a side panel, effect
described as data, **Apply to selection / session range** as an explicit
action (not every slider tick writing 200 pages).

### 14.1 Controls (v1)

| Control | Role |
|---------|------|
| **Mode** | Manual values · Autocrop · (later: From template) |
| **Manual rect** | Raw L/T/R/B or x/y/w/h in **pixels of current logical size**, and/or normalised 0–1 fields. Editing one representation updates the other when size is known. |
| **Autocrop** | Run content-bbox detect on each target (or on current page only for preview). **Threshold** / aggressiveness slider. |
| **Extra margin** | Top / bottom / left / right **added after** autocrop (or inset from manual edges) — padding back outward so text isn’t tight. Units: px or % of page. |
| **Reset crop** | Clear crop fields on the target set (or current only). |
| **Target set** | Current page · Gallery selection · Index range · (later: parity / exclude ends). |
| **Apply** | Commit to all targets in the set (one undo bag). |
| **Preview** | Optional: show proposed rect on the **current** Image/Gallery focus only, before Apply. |

Fully automatic = Autocrop mode + threshold + optional extra margins + Apply
to range. Fully manual = type rects + Apply. Both share the same commit path
into existing `cropRect` / `cropSourceSize` / `cropRotation` fields.

### 14.2 What the panel does *not* need in v1

- Stack/sum underlay  
- Linked live groups (editing page 5 updates 6–200 continuously)  
- Deskew  
- Dependency on a chosen template page  

Those layer on without replacing the panel.

### 14.3 Apply semantics

- **While dragging sliders:** update preview on the *current* session image
  only (same as colour), or update nothing until Apply — pick one and stick
  to it; colour panel emits `adjustmentsChanged` live, so crop panel can
  either match that for current-id only, or stay Apply-gated for batch safety.
- **Recommendation:** live update **current id only**; **Apply to targets**
  is a separate button for N>1. Prevents accidental bulk writes while tuning
  threshold.

---

## 15. Template page — UI options (later)

Template = “use **this** page’s crop (normalised) as the recipe for others.”
Uncertainty is mostly **how the user designates the template**, not the data
model.

| UI pattern | How it works | Pros | Cons |
|------------|--------------|------|------|
| **A. “Use current as template”** | User crops page *k* with normal crop UI or panel manual mode; panel button **Use as template** stores normalised recipe; **Apply to targets** paints that recipe. | Minimal new chrome; reuses existing crop. | Easy to forget which page is template. |
| **B. Template slot in panel** | Small thumbnail + “Set from current” / “Clear”; Apply always uses the slot if set. | Visible state. | Slightly more UI. |
| **C. Right-click tile** | Gallery context: **Set as crop template** then **Apply template to selection**. | Fast for power users. | Discoverability. |
| **D. Wizard** | Step 1 pick template, step 2 targets, step 3 confirm. | Guided. | Heavy for a dock-first product. |

**Recommendation:** **A + B** — panel holds an optional template recipe
(normalised rect + “from session id …”); primary path is crop current → Use as
template → set target range → Apply. Gallery context menu (C) as shortcut.
Avoid a separate wizard until the panel is proven.

**Mirror for verso/recto:** once template exists, a checkbox **Mirror
horizontally for opposite parity** covers many book scans without a second
manual crop. Still later than v1 panel.

**Stack preview** can eventually *replace* the template underlay (composite
instead of one page) without changing Apply.

---

## 16. Undo bag (orient first)

For multi-apply orient/reset (slice 1):

- Before commit, snapshot `ContentAppearance` (or the fields touched) for each
  target SessionImageId.  
- Single QUndoCommand: “Batch orient (N pages)” / “Batch reset appearance (N)”.  
- Redo re-applies the new values; undo restores snapshots.  

Same bag type should serve colour and crop Apply later so the shell does not
grow three undo designs.

---

*End of brainstorm.*
