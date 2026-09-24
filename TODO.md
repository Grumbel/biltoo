# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2474.1-own-image-edge-nav** (base `7d823d8`).

### Ownership transfer
- Image-mode **edge hover** + **drawEdgeAffordances** on `ImageController`
- State: `EdgeNavPolicy::Zone m_hoverEdge`
- ImageView keeps public `EdgeZone` enum + thin host bridges
- `hostSessionNav()` added for controller access to SessionNavFlags

### Stack
2471 Workspace input · 2471.2 moc · 2472 DecodeBook · 2473 layout · **2474 edge nav**

### Apply
```bash
git pull --ff-only …/biltoo-2474.1-own-image-edge-nav-7d823d8.bundle HEAD
```
