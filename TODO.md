# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2384-filmstrip-full-session-fill** (on top of `660c49c` stack).

Includes **2381–2383**.

### 2384
- Removed filmstrip **256-row cap**. Progressive chunked fill materializes the
  **full** session (16ms between 32-row chunks) so scroll/navigate work end-to-end.
- Thumb **pixels** stay viewport-virtualized (`scheduleVisibleThumbnailLoads`).
- `ensureMaterializedThrough` + `setCurrentIndex` catch-up when jumping ahead of
  the fill cursor.
- Dropped 200ms large-session filmstrip install delay.

**Note:** Letterbox variable cell widths need real `QListWidgetItem`s for correct
scroll extent. Item shells are progressive, not a sliding window; a true
model-window virtualization would need a different layout engine (0.3).

**Thumtoo pinned** → `f71d183` (thumtoo-323).

**Next:** RC smoke; VERSION 0.2.0 + tag.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2384.1-filmstrip-full-session-fill-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] 2381–2383 (probe cancel, orient plan, …)
- [x] 2384 filmstrip full-session fill (no 256 cap)
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag

## Backlog (0.3.0)
- True filmstrip item-window virtualization (QAbstractListModel + uniform or
  measured extent) if 20k QListWidgetItem RAM becomes an issue
- Size-resolve throughput / failure diagnostics
- ImageView ownership extraction
