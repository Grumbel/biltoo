# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2406-warm-restash-verify** (base `d80d461`, includes 2403–2405).

### Verification pass (2406)
Residuals fixed after 2405:

1. **`enterGalleryMode`** (layout menu / go-to-gallery from Image) still always
   ran `populateGalleryCanvas` after a warm stash restore. Now uses the same
   warm skip as `returnToGallery`.
2. **Session drift:** drop-append while in Image can grow the session without
   discarding Gallery stash. Warm skip requires `itemCount() == session.size()`;
   mismatch forces populate.

Already confirmed OK (no code change):
- Size gate is size-only (no `ImageCache::has` in `GallerySizeResolve`).
- `TileLodRegistry::invalidateAll` / `ImageCache::clear` / `m_sizeBook.clear`
  only on session replace.
- Session open discards Gallery/Workspace stashes.
- Probe FIFO still requires underlay to *skip* Store `request_size` —
  intentional; size gate never enqueues known sizes.

### Stack
| Tip | What |
|-----|------|
| 2403 | Size gate size-only |
| 2404 | ImageCache memory budget |
| 2405 | Warm restash on returnToGallery |
| 2406 | enterGalleryMode warm + membership check |

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2406.1-warm-restash-verify-d80d461.bundle HEAD
```

## Prior (2026-09-23)

**Tip: biltoo-2402-underlay-path-complete** (on top of `660c49c` stack).

Includes **2381–2401**.

### Verification pass (2402)
Found and fixed residual underlay installs outside `tryInstallGalleryUnderlay`.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2402.1-underlay-path-complete-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2401
- [x] 2402 underlay path completeness
- [x] 2403 size gate size-only (mode switch no full re-probe)
- [x] 2404 ImageCache memory budget
- [x] 2405 warm Gallery restash (skip populate on return)
- [x] 2406 warm restash verification (enterGalleryMode + membership)
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
