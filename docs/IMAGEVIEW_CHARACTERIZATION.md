<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ImageView characterization harness (Tier 4 prerequisite)

REFACTOR.md requires an offscreen `QTest` that drives `ImageView` through
**open → Gallery → crop → return → Image** and asserts appearance, logical
size, and framing before deleting `m_pathOrderBook`.

## Pure contracts already locked

| Test | Covers |
|------|--------|
| `sessiondocument` | path/id alignment, never-reuse, appearance ownership |
| `sessionappearance` | id-keyed crop, materialize, dual-path independence |
| `sessionpathorder` | multiplicity, setOrder trim, OOB |
| `pathorder-dual-model` | document vs book independence, LoadAdd, clear, gallery delete prune, aligned pack case |
| `contentxform` | layout size, crop map through rotate |
| `packorderview` | fromBook/fromDocument, alignsWithDocument, multiplicity |
| `session-gallery-crop-scenario` | narrative open→Gallery→crop→return pure side |

These do **not** replace the harness: they do not exercise decode, mode
transitions, framing, or Live canvas.

## Harness goals

1. Construct `ImageView` offscreen (`QApplication` + no show, or `QTest`).
2. Bind `SessionDocument` appearance + document (same as MainWindow).
3. Open two synthetic paths (temp PNGs of known size).
4. Enter Gallery; assert path-order book aligns with document.
5. Crop one session id; assert `appearance().get(id)` and layout size.
6. Return to Image on that id; assert crop still applied; sibling unchanged.
7. LoadAdd / paste multiplicity: book size > document size; pack count follows book.

## Suggested layout

```text
tests/imageview_characterization.cpp
  - links ImageView + minimal deps (or whole biltoo object list minus main)
  - QTEST_MAIN
  - fixtures: temp dir with 2× solid-colour PNGs
```

CMake: only enable when `Qt6Test` and a flag `BILTOO_IMAGEVIEW_CHARACTERIZATION`
are on — full link is expensive (near-full app).

## Assertions (checklist)

- [ ] After open: `doc.size() == 2`, unique ids
- [ ] After Gallery enter: `pathOrderSize() == doc.size()` when no LoadAdd
- [ ] After crop commit: `appearance().get(sid)->hasCrop`
- [ ] After return to Image: same crop; other id has no crop
- [ ] After `pathOrderClear`: book empty, doc unchanged
- [ ] After LoadAdd×3 same path: `pathOrderOccurrences == 3`, `doc.count == 1`

## Until the harness exists

Keep `m_pathOrderBook`. Identity queries continue to prefer `m_sessionDoc`
when bound (`firstSessionIdForPath`). See [PATH_ORDER.md](PATH_ORDER.md).
