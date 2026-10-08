# Session notes 2026-10-08: tile corruption, --no-cache, thumtoo subtree

Handoff for whoever (human or agent) picks this up. Everything below is
committed on master except where marked **open**.

## 1. `--no-cache` flag (done, commit cedd9a01)

Bypasses all on-disk caches: thumtoo `Client::open_memory` (index/bulk/user as
SQLite `:memory:`), in-memory appearance DB (flip/crop/annotations lost at
exit), temp dir for HTTP downloads. Qt settings (`biltoo.conf`) still apply.
Flag is scanned from raw argv in `main()` before `ThumtooCache::init()`
(`ThumtooCache::setEphemeral`) and also registered with the parser for `--help`.
Documented in `docs/ENVIRONMENT.md`.

**Open:** not verified at runtime that nothing is written to disk. Check with
empty `XDG_CACHE_HOME` / `XDG_DATA_HOME` / `XDG_STATE_HOME` and
`biltoo --no-cache <dir>`; those directories should stay empty.

## 2. PDF tiles scrambled at scale >= 0 (done, thumtoo e9e23b0, in subtree)

Symptom: title page of `test.pdf` (image-heavy cover) rendered as skewed /
scrambled tiles at positive zoom scales, fine at negative scales.

Cause: `counting_get_pixmap` (`external/thumtoo/src/pdf_mupdf.cpp`,
`CountingImage` single-flight decode) handed the same pixmap object to the
decoding thread and all waiting threads. `fz_get_pixmap_from_image` subsamples
the returned pixmap **in place** when the remaining l2factor > 0, so
concurrent cells corrupted each other.

Fix: the flight keeps an untouched snapshot (clone when l2factor > 0); each
waiter gets a private clone when in-place subsampling is pending. Also
`fz_var(code)` for a `-Wclobbered` warning.

Repro/verification: render cells 0..3 x 0..5 of page 1 sequentially, then with
8 threads, compare bytes. Before: 20/24 cells differed at scale 0, 6 at scale
1. After: 0. **No regression test added** (needs a fixture with a large image
that triggers the in-place subsample).

## 3. thumtoo vendored as git subtree (done, commits 649ce363, 8a156f56, 0f979f98)

`external/thumtoo`, squashed subtree of `~/projects/thumtoo/thumtoo.git`
branch `master`. CMake defaults `THUMTOO_SOURCE_DIR` to it; flake input is
`path:./external/thumtoo` (not pinned in flake.lock; the subtree commit is the
pin); dev shell exports the live in-tree path. Sync commands are in
AGENTS.md ("Vendored subtree"). Notes:

- thumtoo's dev version loses the `.N+gHASH` suffix (no own `.git`).
- Keep thumtoo changes in their own commits touching only `external/thumtoo/`
  so `git subtree push` stays clean.
- The `pinMupdf` fallback in `flake.nix` is now dead code (harmless).
- **Open:** fixes made inside the subtree (section 4) are not yet pushed back to
  the standalone thumtoo repo:
  `git subtree push --prefix=external/thumtoo ~/projects/thumtoo/thumtoo.git master`
- **Open:** `nix build` / `nix flake check` were not run after the move (only
  `nix develop` + `biltoo-build`).

## 4. EPUB cover tiles scrambled (done, commit 1f3ba2d1)

Symptom: `test.epub` page 1 (cover): text pages fine, image tiles show
magnified fragments of the wrong region.

Cause: the cover is a **scalable** image (`fz_image.scalable == 1`, a display
list / SVG-wrapped image; 816x1056). In MuPDF 1.28.5
`fz_get_pixmap_from_image` the scalable branch calls
`image->get_pixmap(..., &subarea_copy, image->w, image->h, ...)` and returns
without `update_ctm_for_subarea` (the raster path does call it).
`display_list_image_get_pixmap` returns a pixmap covering only the sub-area
(`pix->x/y` offset), so the draw device stretches it over the whole image
extent. A full-page region render has sub-area == whole image, so it was right;
only partial cells were wrong. Deterministic, not threading; identical with
thumtoo's `CountingImage` wrapper bypassed.

Fix: in `counting_get_pixmap`, for scalable images force
`*subarea = {0,0,w,h}` before passing through.

Verification: render page 1 once as a whole-page region (reference) and as 6x8
cells stitched; before: scrambled; after: identical to reference. Eight-thread
vs sequential: 0 mismatches at scales 0 and 1.

**Open questions / to check later:**
1. Is this a MuPDF bug? Source reading (draw-device.c `fz_draw_fill_image`,
   image.c scalable branch) says the CTM is not adjusted for scalable
   sub-areas, but I did not test stock MuPDF. Reproduce with MuPDF alone:
   `mutool draw -r 144 -R x,y,w,h test.epub 1` vs the full page, or a small C
   program (new draw device over a pixmap with non-zero bbox origin +
   `fz_run_display_list` with a clip). If it fails there, report upstream.
   The thumtoo workaround should stay regardless.
2. Not tested with other scalable images (e.g. PDFs embedding SVG-ish form
   images) or at scale > 1 / large zoom. A scalable image renders at a fixed
   internal dpi and is upscaled; confirm zoomed tiles of the cover look
   acceptable and not worse than before.
3. **GUI not re-run** for the EPUB fix or the PDF fix (only the standalone
   thumtoo test harness). Rebuild with `biltoo-build` and look at
   `test.pdf` p.1 and `test.epub` p.1 at several zoom levels.
4. No regression test for either bug. A thumtoo test for the EPUB case needs a
   small EPUB with an SVG-wrapped cover.

## 5. Process notes

- Test harnesses lived in the session scratchpad (not in the repo):
  `stress.cpp` (PDF seq vs parallel), `estress.cpp` (EPUB seq/parallel/
  reference stitch), built against a standalone thumtoo CMake build with
  `-DTHUMTOO_BUILD_TOOLS=ON` (`thumtoo-tile --raw-pdf` renders one cell).
  Worth turning into a thumtoo test once fixtures exist.
- Stray files in the repo root (untracked, not committed): `TODO`, `test.pdf`,
  `test.epub`.
