# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2418-no-imageview-friend** (base `d80d461`).

### Phase 5 ownership — slice 3
- Removed `friend class ImageView` from `ImageItem`
- Public **canvas host surface**: pose, session, mode chrome, applied xform, colour
- Pixel ops only via pipeline friends + `hostClearDecodedPixels` /
  `hostSetIntrinsicSize` / `hostSetPreviewImage`

### Next
4. Dual ImageView (0.3) on thin façade

### Apply
```bash
git pull --ff-only …/biltoo-2418.1-no-imageview-friend-d80d461.bundle HEAD
```
