# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2811.4-crop-in-tool-radio` (base `2085c07`).

### 2811.4
- Crop joins Exclusive canvas tool radio (`enterCropFromCanvasTool`)
- Re-select Crop / C while cropping → Select (toggle-off)
- Gallery→Image deferred crop holds Select until pixels ready; syncCanvasToolChrome priority crop > annot > view

### Prior
- 2811.3 harden set*Tool + syncCanvasToolChrome
- 2811.2 Exclusive radio (not Optional)
- 2811.1 unified palette

### Next
- Manual: Crop ↔ Select/Pan; C toggle; Gallery crop → Image
- Optional: fold Attention into the canvas radio
- Run contentxform_test locally

### Apply
```bash
git pull --ff-only …/biltoo-2811.4-crop-in-tool-radio-2085c07.bundle HEAD
```
