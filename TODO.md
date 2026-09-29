# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2815.1-annot-pages-list` (base `2085c07`).

### 2815.1
- Annotation panel: list of pages that already have annotations; jump on activate
- Future cutouts board: [docs/ANNOTATION_CUTOUTS.md](docs/ANNOTATION_CUTOUTS.md)
- Fix: define `reportSessionOpenFailed` (linker error from 2814.3 empty-expand path)

### Prior
- 2814.4 OCR This Page opens panel only
- 2814.3 surface failed Open/Bookshelf (declaration without definition — fixed in 2815.1)
- 2814.2 load error no GUI stat
- 2814.1 load error UI

### Apply
```bash
git pull --ff-only …/biltoo-2815.1-annot-pages-list-2085c07.bundle HEAD
```
