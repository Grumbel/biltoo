# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2476.1-own-text-layer-controller** (base `7d823d8`).

### Ownership transfer
- **TextLayerController** owns `TextLayerSession` and text/link behaviour
  (`src/text/textlayercontroller.{h,cpp}`)
- ImageView: thin routers; `hostTextLayer()` → `m_textCtrl.session()`

### Stack
2471–2475 · **2476 text layer**

### Apply
```bash
git pull --ff-only …/biltoo-2476.1-own-text-layer-controller-7d823d8.bundle HEAD
```
