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
| `sessiondocument` | path/id alignment, never-reuse, seed-book lifecycle |
| `sessionappearance` | materialize helpers + seed book; content via ItemWorld |
| `sessionpathorder` | multiplicity, setOrder trim, OOB |
| `pathorder-dual-model` | document vs book independence, LoadAdd, clear, gallery delete prune, aligned pack case |
| `contentxform` | layout size, crop map through rotate |
| `packorderview` | fromBook/fromDocument, alignsWithDocument, multiplicity |
| `packorderoverlay` | FollowDocument / Explicit, collapse, seed-on-append |
| `session-gallery-crop-scenario` | narrative open→Gallery→crop→return pure side; ItemWorld Crop/ContentBake/Placement/Color/Attention presence |
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
5. Crop one session id; assert `itemWorld().hasCrop(id)` and layout size.
6. Return to Image on that id; assert crop still applied; sibling unchanged.
7. LoadAdd / paste multiplicity: pack size > document size; pack count follows
   overlay explicit order.

## Suggested layout

```text
tests/imageview_characterization.cpp
  - pure cases: session + PackOrderOverlay + ItemWorld + ContentXform
  - full cases: offscreen ImageView (default CMake; BILTOO_HAVE_IMAGEVIEW_HARNESS)
  - QTEST_MAIN
  - fixtures: temp dir with 2× solid-colour PNGs
```

Default CMake links **`biltoo_lib`** and defines `BILTOO_HAVE_IMAGEVIEW_HARNESS`.
Escape hatch: `-DBILTOO_IMAGEVIEW_CHARACTERIZATION=OFF` builds the pure scaffold
only (ImageView slots QSKIP). `imageView_openGalleryCropReturn` constructs an
offscreen `ImageView`, binds document/appearance, enters Gallery, sets workspace
paths, commits a crop via ItemWorld, clears pack order, and asserts LoadAdd
multiplicity — without waiting on decode.

## Assertions (checklist)

### Pure (always run)

Session / ItemWorld / PackOrderOverlay cases (no ImageView). See AGENT-ENV.md.


- [x] After open: `doc.size() == 2`, unique ids
- [x] After Gallery enter simulation: overlay resolves aligned with document
- [x] After crop commit: `itemWorld().hasCrop(sid)` + `ContentXform::layoutSize`
- [x] Sibling id has no crop
- [x] After `clearExplicit`: pack empty, doc unchanged
- [x] After LoadAdd×3 same path: pack occurrences == 3, `doc.count == 1`
- [x] Host setOrder collapses when aligned; clear stays Explicit empty
- [x] Append after collapse seeds document membership
- [x] Crop survives pathOrderClear (return-to-Image invariant)
- [x] Placement survives pathOrderClear; sibling id has no placement
- [x] ContentBake survives pathOrderClear; sibling clean
- [x] Color grade survives pathOrderClear; sibling clean
- [x] Attention points survive pathOrderClear; sibling clean
- [x] Applied ContentXform runtime table survives pathOrderClear; not durable; clearAppearance drops it
- [x] Live colour lag runtime table survives pathOrderClear; not durable Color; clearAppearance drops it
- [x] ViewFraming defaults, fit/fill transitions, sticky pan norms + preserved scale (pure)
- [x] ViewTransform uniformFitScale / padded / fitRectCentered (pure)

### Full ImageView (`BILTOO_HAVE_IMAGEVIEW_HARNESS`)

- [x] Offscreen `ImageView` construct + bind document/appearance
- [x] `enterGallery` + `setWorkspacePaths` pack order aligns
- [x] Crop via ItemWorld; sibling clean; layoutSize
- [x] Placement / ContentBake / Color / Attention on focus; siblings clean
- [x] Applied ContentXform + liveColorLag on focus; siblings clean; survive pathOrderClear
- [x] Soft install (fixture PNG → SoftPreview) without async decode wait; sibling clean
- [x] ViewFraming defaults on offscreen ImageView construct
- [x] prepareImageModeCanvas resets view scale to 1 + Fit; fitItem yields positive view scale
- [x] captureStickyPanAnchor preserves view scale / pan; restoreStickyPanAnchor safe after fit
- [x] Soft install + fit/capture/restore handoff across two session ids (no async Image load)
- [x] Image-mode sync LoadReplace (installImageModeReplaceItem) focus→other; sticky capture/restore; ItemWorld survives
- [x] `pathOrderClear` leaves doc + all id-keyed components; LoadAdd multiplicity
- [ ] Full async decode / PreferCache ladder (optional; DisplaySurface decide pure tests cover policy)

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
