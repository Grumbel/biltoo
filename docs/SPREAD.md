<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Spread — multi-page reading surface (design)

**Status:** P0–P4 complete (binding, LTR/RTL/Vertical, fixed-N, settings persistence).  
**Related:** [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md), [TEXT_OVERLAY.md](TEXT_OVERLAY.md),
[TEXT_TO_SPEECH.md](TEXT_TO_SPEECH.md), [DOMAIN.md](../DOMAIN.md) (world vs viewpoint),
`DualImageShell` (compare — **not** spread).

---

## 1. Problem

Image mode is a **single-page** underlay. Readers often need **facing pages** (2)
or, rarely, **N pages** on one surface (foldouts, plates). Requirements:

1. Arbitrary N (1 = today’s Image; 2 = common; N>2 supported without a fork).
2. Text selection, copy, and speak work **across pages** without path hacks.
3. Prev/Next semantics change deliberately (documented), not as an accident.
4. Fits session identity (`SessionImageId`), ItemWorld, and world vs viewpoint.

Non-goals for this design:

- Pixel-stitching pages into one raster.
- Merging with **DualImageShell** (independent compare nav).
- Turning Image into Workspace (free-form poses stay Workspace).

---

## 2. Three surfaces that must not be confused

| Surface | Role | Nav | Text |
|---------|------|-----|------|
| **Image (single)** | One session row full-frame | ±1 row | One `PageTextLayer` |
| **Spread (this doc)** | Ordered N rows, one camera | Stride over membership | Unified model over members |
| **Dual compare** | Two ImageViews, shared world | Per-pane | Per-pane (not one selection) |
| **Workspace** | Free-form multi-item | N/A | Per-item / future |

**Double view** UI and **View Selection** write **Spread** state. They do not
enable DualImageShell.

---

## 3. Data model

### 3.1 `SpreadState` (world / session)

Owned by session-facing world state (not by GalleryController, not by a
transient MainWindow bool). Survives Gallery ↔ Image leave/enter.

```text
SpreadState {
  members:  Vec<SessionImageId>   // ordered reading order (LTR default)
  anchor:   SessionImageId        // filmstrip / status / “current” page
  policy:   MembershipPolicy
  stride:   SpreadStride          // how Next/Prev moves the window
}
```

**Invariants**

- `members` is empty or a subsequence of session rows in **session order**
  (View Selection sorts by session index, not click order).
- `anchor ∈ members` when `members` non-empty; otherwise anchor is unset and
  Image uses the normal current session row.
- N = `members.len()`. N=0 means “no spread” (classic single-page Image driven
  only by current index). N=1 is allowed (explicit single from a 1-item
  selection).

Paths are **derived** from session rows when binding items; never stored as the
primary key of membership.

### 3.2 Membership policies

```text
MembershipPolicy =
  | Off                    // members empty; single-page Image
  | FixedN { n }           // window of n pages around anchor (binding rules)
  | Selection              // members = ordered gallery/session selection
  | Explicit               // members set programmatically (tests, scripts)
```

**Binding hints** (for `FixedN` with n=2), applied when *building* membership,
not inside the layout kernel:

| Hint | Behaviour |
|------|-----------|
| `CoverAlone` | Page 1 alone; then pairs (2–3), (4–5), … |
| `StrictPairs` | (1–2), (3–4), …; last may be 1 page |
| `AnchorCentre` | Prefer pair containing anchor; odd/even by index |

v1 ships StrictPairs + CoverAlone + Selection; AnchorCentre remains available in the enum.

### 3.3 Stride (nav)

```text
SpreadStride =
  | BySpread    // Next moves window by N (or binding step)
  | ByPage      // Next shifts membership by 1 session row
```

Status chrome should show the range, e.g. `12–13 / 200`, not only the anchor
filename.

---

## 4. Presentation

### 4.1 Items

- **One `ImageItem` per member** (same as today: one path/sid, one pipeline).
- Do **not** composite into a single `QImage`.
- Image mode live set = spread members when `SpreadState` is active; otherwise
  the single current underlay.

### 4.2 Layout kernel

Pure function (testable without Qt session):

```text
layoutSpread(
  pageSizes: [(sid, sizeInContentUnits)],
  options: { gutter, heightMatch, direction }
) -> { slots: [(sid, rectInSpreadFrame)], unionRect }
```

- Default direction: horizontal LTR; RTL later via option.
- `heightMatch`: scale each page so heights equal min or median height; width
  follows aspect.
- Gutter: fixed device-independent px or fraction of min side.
- Camera **fit** uses `unionRect` as the sole content rect for zoom/pan chrome.

Slot placement is applied as item transforms/positions on the scene; each item
keeps its own ContentXform (crop/orient) in page space.

**Pose mapping:** `ImageItem` content is centred on `pos` (`setOffset(-w/2,-h/2)`).
`applySpreadLayout` must set `pos` to each slot’s **centre** (not top-left) and
uniform scale so the item fills the height-matched (or width-matched vertical)
slot. Using top-left made unequal pages look shifted and stacked oddly.

### 4.3 Hit-testing

Scene point → item under cursor → map into page/source space with existing
ContentXform helpers. No global “fake page coordinates” that invent Y across
page boundaries.

---

## 5. Text

### 5.1 Identity (non-negotiable)

Selection entries remain:

```text
TextSelRef { sessionId, regionIndex, textSnapshot? }
```

as in `TextSelection` today. **Never** renumber regions into a global 0…K-1
index space that collapses pages.

### 5.2 Spread text facade

Logical component (name flexible: `SpreadTextCoordinator`):

| Concern | Rule |
|---------|------|
| Load | Ensure/cached text layer per member sid (budgeted; visible first) |
| Paint | For each member item: map region bboxes through that item only |
| Rubber-band | Band in scene/spread frame; classify hits per item; append refs with correct sid |
| Panel | Rows = reading-order flatten of members’ regions; multi-select → `TextSelection` |
| Copy / speak | `TextSelection.joinedText()` in reading order across sids |

`TextLayerController` may stay per-view but must **not** assume a single
`layerPath` equals the whole selection. Either:

- one controller with a map `sid → PageTextLayer`, or  
- a thin coordinator owning N layer installs and one selection bag.

### 5.3 Speak / highlight

- Speak plan spans carry `(sid, regionIndex, start, end)`.
- Green region highlight only on items present in the current spread.
- Gallery TTS page ring: highlight **all** members while speaking a spread, or
  only the span’s current sid — prefer **active span’s sid** for clarity;
  optional dim ring on other members.

---

## 6. State machine

### 6.1 Enter spread

| Trigger | Membership |
|---------|------------|
| Toolbar **Double view** ON | `FixedN{2}` from current anchor + binding hint |
| Gallery **View Selection** | `Selection`: selected rows in session order; open Image |
| Explicit API / test | `Explicit` |

Then:

1. Write `SpreadState`.
2. Enter or stay in Image mode.
3. Bind/ensure items for all members; run layout; fit union.
4. Refresh text facade for members.

### 6.2 Leave spread

| Trigger | Result |
|---------|--------|
| Double view OFF | `members` cleared; Image shows **anchor** (or session current) alone |
| Open a single Gallery tile (normal open) | Spread off; that row alone |
| Enter Workspace | Spread state **retained in world** but not shown; Workspace ignores it until return to Image (or clear on Workspace enter — product choice; prefer retain) |
| Enter Gallery | Spread retained; Gallery selection independent |

### 6.3 Navigation while spread active

```text
Next/Prev:
  BySpread → rebuild members from policy at new anchor
  ByPage   → shift window start by ±1, clamp to session
Home/End → first/last valid window
```

Filmstrip click: set anchor to clicked row; rebuild membership from policy
(Selection policy may collapse to FixedN around click, or keep selection —
prefer **rebuild from FixedN** if Double view was the source; **keep Selection**
until user toggles Double view).

### 6.4 Mode ownership interaction

- Spread does **not** stash Gallery packs differently; Image leave still follows
  [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md).
- Spreading must **not** remove rows from `SessionDocument`.
- Forbidden: stealing Workspace stash pointers to fill spread slots.

---

## 7. UI contract

### 7.1 Toolbar / menu — Double view

- Toggle; checked when `policy` is `FixedN` with n=2 (or n>1 active from toggle).
- Tooltip: facing pages (spread), not “compare”.
- **View → Double view** submenu: toggle, binding (strict pairs / cover alone),
  direction (LTR / RTL / vertical), fixed-N (2 / 3 / 4), and dual compare.
- Toolbar still exposes the Double view action for quick toggle.

### 7.2 Gallery — View Selection

- Context menu / View menu when selection non-empty.
- 1 selected → Image single (spread off or N=1).
- 2+ selected → Image with `Selection` membership.
- Order = session order of selected ids.

### 7.3 Status / a11y

- Indicate spread range and stride mode.
- Prev/Next status tips change when spread active (“Next spread” vs “Next page”).

---

## 8. Minimal API surface (implementation guide)

Names illustrative; keep them out of random MainWindow lambdas.

```text
// World
struct SpreadState { ... };
class SpreadBook {  // or fields on SessionDocument / SessionShell
  SpreadState state() const;
  void clear();
  void setFixedN(int n, SessionImageId anchor, BindingHint);
  void setFromSelection(QVector<SessionImageId> ordered);
  bool advance(int direction); // applies stride; returns false at end
};

// Viewpoint (Image)
class SpreadPresenter {
  void apply(const SpreadState&, ImageView&);
  QRectF unionContentRect() const;
};

// Text
class SpreadTextCoordinator {
  void setMembers(const QVector<SessionImageId>&);
  void ensureLayers();
  SpeakPlan buildSpeakPlan(...) const;
  // selection bag remains TextSelection at session/world level
};
```

**DisplayPipeline:** no API change required beyond concurrent loads for N paths
(budget already exists for Gallery). Cap N (e.g. 8) to protect memory.

---

## 9. Phased delivery

| Phase | Ship | Done when |
|-------|------|-----------|
| **P0** | `SpreadState` + presenter layout N=2; nav stride; Double view toggle | **Done:** state/book/layout, Double view, nav stride, multi-underlay install, text paint deferred. Annotation paint/hit uses the same multi-`liveItems` path as Gallery (not primary-only). |
| **P1** | Gallery View Selection; status range | **Done:** View Selection + Gallery menu; status range; event-driven sync; FixedN filmstrip; leave/dual mutual exclusion; N≤8. |
| **P2** | Text coordinator + cross-page rubber-band/copy | **Done:** layers, rubber-band, paint, copy, panel flatten, multi search, secondary glyphs. |
| **P3** | Speak plan + highlights over members | **Done:** SpeakSpan.sessionId; full-spread plan; Image highlight; Gallery multi-member ring (active strong, others dim). |
| **P4** | N>2, binding hints, RTL, heightMatch prefs | Foldout / polish |

Do not ship P0 with “text only on primary” if P2 is near; a half-broken
selection teaches the wrong model. Acceptable interim: text overlay **disabled**
until P2, with a status note.

---

## 10. Design tests

1. **Identity:** Copy from a rubber-band spanning both pages yields left-page
   text then right-page text in reading order; refs keep distinct sids.
2. **Nav:** From pages (2–3), Next under `BySpread` + `StrictPairs` shows (4–5),
   not (3–4), unless stride is `ByPage`.
3. **Compare:** DualImageShell still independent; enabling Dual does not set
   `SpreadState`.
4. **Workspace:** Enter Workspace with spread on does not destroy session rows
   or require spread layout on the free canvas.
5. **Reload:** Hard reload on anchor does not drop the other member from
   membership (only re-decode paths).
6. **TTS:** Speak spread; Gallery ring tracks active span sid; leaving Image to
   Gallery does not clear world speak path until Stop.

---

## 11. Open choices (decide at P0/P1)

1. Retain `SpreadState` across Workspace enter, or clear? **Recommendation: retain.**
2. Filmstrip click under Selection policy: keep selection or snap to FixedN?
   **Recommendation: snap only if Double view owns policy; keep selection for View Selection until toggled.**
3. Max N: **8** for v1 hard cap.
4. Vertical spreads (manga): direction flag in layout options; not a second feature.

---

## 12. Summary

Spread is a **session-owned ordered membership** projected by Image mode as
**N items + one camera**. Text and speech use **`(SessionImageId, regionIndex)`**
end to end. Dual compare and Workspace stay separate products. UI only edits
membership; layout and text are deterministic functions of that state.
