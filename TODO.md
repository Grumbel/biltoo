# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2473.1-own-gallery-layout-prefs** (base `7d823d8`).

### Ownership transfer
- **LayoutPrefs**, **LayoutDebounce**, **GalleryRelayoutSuppress**, **LayoutApplyGuard**
  moved from ImageView to GalleryController
- `hostLayout` / `hostGalleryRelayoutSuppress` / `hostLayoutApply` forward
- Debounce **QTimer** remains on ImageView (QObject parent)
- Residual: replace leftover `m_gallerySizeResolve` with `hostGallerySizeResolve()` (2469 incomplete)

### Stack
2471 Workspace input · 2471.2 moc · 2472 GalleryDecodeBook · 2473 layout bags

### Apply
```bash
git pull --ff-only …/biltoo-2473.1-own-gallery-layout-prefs-7d823d8.bundle HEAD
```
