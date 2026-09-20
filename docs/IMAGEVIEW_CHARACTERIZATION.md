<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ImageView characterization harness (Tier 4 prerequisite)

REFACTOR.md requires an offscreen `QTest` that drives `ImageView` through
**open → Gallery → crop → return → Image** and asserts appearance, logical
size, and framing before trusting FollowDocument / document-only pack.

## Pure contracts already locked

| Test | Covers |
|------|--------|
| `sessiondocument` | path/id alignment, never-reuse, appearance ownership |
| `sessionappearance` | id-keyed crop, materialize, dual-path independence |
| `sessionpathorder` | multiplicity, setOrder trim, OOB |
| `pathorder-dual-model` | document vs book independence, LoadAdd, clear, gallery delete prune, aligned pack case |
| `contentxform` | layout size, crop map through rotate |
| `packorderview` | fromBook/fromDocument, alignsWithDocument, multiplicity |
| `packorderoverlay` | FollowDocument / Explicit, collapse, seed-on-append |
| `session-gallery-crop-scenario` | narrative open→Gallery→crop→return pure side; ItemWorld Crop/ContentBake presence |
| `itemworld` | Stage 0–2 facade: dual-write, presence, clear |
| `imageview-characterization` | PNG fixtures + overlay host-mutator simulation + layoutSize |

These do **not** replace the full harness: they do not exercise decode, mode
transitions, framing, or Live canvas.

## Harness goals

1. Construct `ImageView` offscreen (`QApplication` + no show, or `QTest`).
2. Bind `SessionDocument` appearance + document (same as MainWindow).
3. Open two synthetic paths (temp PNGs of known size).
4. Enter Gallery; assert pack-order overlay aligns with document (post-collapse
   FollowDocument or Explicit aligned).
5. Crop one session id; assert `appearance().get(id)` and layout size.
6. Return to Image on that id; assert crop still applied; sibling unchanged.
7. LoadAdd / paste multiplicity: pack size > document size; pack count follows
   overlay explicit order.

## Suggested layout

```text
tests/imageview_characterization.cpp
  - pure: always on (session + PackOrderOverlay + ItemWorld + ContentXform)
  - full: links ${BILTOO_LIB_SOURCES} when BILTOO_IMAGEVIEW_CHARACTERIZATION=ON
  - QTEST_MAIN
  - fixtures: temp dir with 2× solid-colour PNGs
```

CMake pure target is always built with `Qt6Test` (default).

Full harness: `-DBILTOO_IMAGEVIEW_CHARACTERIZATION=ON` links **`biltoo_lib`**
(static library of `${BILTOO_LIB_SOURCES}` + the same PUBLIC deps as the app)
and defines `BILTOO_HAVE_IMAGEVIEW_HARNESS`. The open→Gallery→crop→return
**body** still needs to be written against that define.

## Assertions (checklist)

### Pure (green — imageview-characterization)

- [x] After open: `doc.size() == 2`, unique ids
- [x] After Gallery enter simulation: overlay resolves aligned with document
- [x] After crop commit: `itemWorld().hasCrop(sid)` + `ContentXform::layoutSize`
- [x] Sibling id has no crop
- [x] After `clearExplicit`: pack empty, doc unchanged
- [x] After LoadAdd×3 same path: pack occurrences == 3, `doc.count == 1`
- [x] Host setOrder collapses when aligned; clear stays Explicit empty
- [x] Append after collapse seeds document membership
- [x] Crop survives pathOrderClear (return-to-Image invariant)

### Full ImageView (still pending)

- [ ] Offscreen `ImageView` construct + bind document/appearance
- [ ] Decode / soft tiles for fixture PNGs
- [ ] `enterGallery` / mode transitions / framing
- [ ] Live canvas crop apply + return to Image mode

## Landed

- **1787:** pure scaffold + QSKIP for ImageView step
- **1788:** `BILTOO_LIB_SOURCES` for future full link
- **1885:** pure harness uses `PackOrderOverlay` (post-1883/1884); layoutSize;
  host-mutator simulation (collapse / clear / seed-append / crop survives clear)

## Until the full ImageView harness exists

Keep overlay **Explicit-only** for writes that must suppress pack; collapse is
allowed only when order aligns with the document. Identity queries continue to
prefer `m_sessionDoc` when bound (`firstSessionIdForPath`).
See [PATH_ORDER.md](PATH_ORDER.md).
