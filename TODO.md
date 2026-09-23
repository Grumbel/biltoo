# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2410-gallery-pack-scrollbar-gutter** (base `d80d461`, includes 2403–2409).

### Gallery dual-scrollbar feedback loop
Pack measured with both scrollbar gutters reserved (`PackViewportGuard` AlwaysOn),
1px slack, fitted-axis `clampSceneRectToPack`. Virtual plan uses the same measure.

### 2410.2 — test compile
`PackViewportGuard` lived in `gallerypackfit.h` and pulled `<QAbstractScrollArea>`,
breaking `gallerylayout_test` (no Widgets). Guard is now local to
`gallerycontroller.cpp`; packfit stays pure (QtGlobal / QRectF only).

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2410.2-gallery-pack-scrollbar-gutter-d80d461.bundle HEAD
```

**Next:** RC smoke; VERSION 0.2.0 + tag.

## Backlog (0.2.0)
- [x] 2381–2410
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
