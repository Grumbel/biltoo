# Source records, page profiles and the Status panel

**Normative.** How biltoo learns about a source (session path), decides how to
show it, and makes every such decision visible. Code: `src/tilelod/source_records.*`,
`page_profile_service.*`, `source_status.*`, `src/shell/statuspanel.*`.

## Why

Tile behaviour used to depend on facts hidden in classes and on guesses made
after failures: biltoo asked for a denser PDF scale, thumtoo refused, biltoo
stepped a per-page floor down and asked again. Nobody could see why a page
stopped at some zoom level. Now facts and decisions live in one table, as
plain data, and the Status panel shows them.

## Model

```text
SourceRecords (process-wide, GUI thread)      key = session path
  SourceRecord
    kind            image | PDF page | DjVu page | EPUB page
    profile_state   n/a | not requested | pending | known | failed
    profile         thumtoo::PdfPageProfile (what the page draws)
    document_file, page
    decisions[]     { what, value, why }  — one per `what`, latest value
```

- **Systems write, views read.** `PageProfileService` fills profiles;
  tile prepare writes decisions; the Status panel reads everything.
- **One notification.** When a profile lands, the service calls
  `TileLodRegistry::notify_path_views(path)`, i.e. the existing change hook of
  every view bound to the path. Views re-plan and read the record again. There
  are no other signals.
- **Forget on change.** `TileLodRegistry::invalidate(path)` forgets the record;
  `invalidateAll()` (session replace) clears the table. Late profile answers
  carry a request number and are dropped when it no longer matches.

## Page profiles (thumtoo)

`thumtoo::pdf_page_profile` runs the page's display list (contents,
annotations, widgets) through a profiling device and reports:

| kind | meaning | zoom |
|------|---------|------|
| empty | nothing visible | document floor |
| vector | paths / shadings / visible glyphs, no images | document floor (−4) |
| raster | images only (scans) | `finest_useful_scale` |
| mixed | images + visible vector content | document floor (−4) |

Invisible text (OCR layers) and a page-size first fill (paper background) do
not count as vector detail. `finest_useful_scale` is the coarsest scale whose
dpi reaches the sharpest image's dpi / 1.25 (300 dpi scan → −1, 600 dpi → −2).
Details: thumtoo TILES.md "PDF rendering".

The profile is built together with the page's display list, which the tile
cells reuse, so asking for it costs nothing extra.

## Decisions

`decide_zoom_floor(record, kDocumentLiveMinScale)` is the zoom policy:

| source | floor |
|--------|-------|
| image | 0 (file resolution) |
| PDF page, profile not known yet | 0, **provisional** |
| PDF page, profile failed | 0, with the error |
| PDF raster page | `max(−4, finest_useful_scale)` |
| PDF vector / mixed / empty page | −4 |
| DjVu / EPUB page | −4 (no profile yet) |

Recorded decisions (`what`):

- `zoom floor` — the policy above, with its reason.
- `gallery floor` — Gallery's density floor (cells much smaller than the
  content need no fine levels), with the on-screen size.

Any new heuristic that changes what a source shows must record a decision
here. A heuristic that cannot be seen in the Status panel is a bug.

## Status panel

Panels → Status shows the focused item (Image mode item, else the
selection): source, page analysis, decisions, the view's tile state, the shared
loader, and thumtoo's per-page and per-document render/decode counts (display
list builds, cells rendered/refused/failed, whole vs per-cell image decodes,
decodes shared between threads, document lock waits). Problems are coloured.
"Copy report" copies the same rows as text. `BILTOO_STATUS_REPORT=<file>`
keeps writing that text for scripted runs.

`build_status_sections(SourceStatus)` turns the data into rows; the panel only
renders them (unit-tested in `tests/tilelod_test.cpp`).
