# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2665.1-pdf-crop-tile-scale` (base `2e49220`).

### Stack
1. 2663.1 — Open Selection content snapshot
2. 2664.1 — Menu Document/Thumbs flatten
3. **2665.1** — PDF crop + tiles: scale cropRect via cropSourceSize → page native

### Apply
```bash
git pull --ff-only …/biltoo-2665.1-pdf-crop-tile-scale-2e49220.bundle HEAD
ctest -R contentxform --output-on-failure
```

---

## Direction (not 0.2.0 tag work)

### Kill Soft — tiles everywhere

**Product rule:** sharpness and zoom come from **tiles** only. Do not keep
hauling whole-frame Soft PreferCache / soft ladder samples as a parallel
display path.

| Mode | Today | Target |
|------|--------|--------|
| **Gallery** | Soft already removed (LQIP + tiles) | Keep it that way — see `docs/GALLERY_SOFT.md` |
| **Filmstrip** | LQIP + TileSynth | Stay tiles-oriented |
| **Image / Workspace** | Soft underlay + climb + tiles | **Retire Soft**: LQIP/EMB placeholders only until tiles cover; interactive `request_tile` / durable pyramid for zoom |

**Why Soft keeps coming back:** crop bake, nav-hot Image, Workspace underlay, and
“need pixels before tiles exist” all short-circuit into soft ladder encode.
That duplicates the tile path, fights crop/native basis (e.g. PDF crop squish),
and burns workers that should encode cells.

**Out of scope for 0.2.0.** Track as a post-tag campaign:

1. Inventory remaining Soft call sites (`PreferCache`, soft ladder install,
   `SoftPreview` materialize, nav soft).
2. Replace each with LQIP/EMB hold + tile plan (same coverage rules as Gallery).
3. Drop soft-specific host cache keys / climb policies once nothing reads them.
4. Docs: fold `GALLERY_SOFT.md` / `IMAGE_MODE_NAV_SOFT.md` into a single
   “no whole-frame soft” pixel contract.

Do **not** reintroduce Gallery soft under a new name.
