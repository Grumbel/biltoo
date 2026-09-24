# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2467.1-own-image-mode-framing** (base `7d823d8`).

### Ownership transfer
- **2464–2466:** Workspace group / page-guide / chrome + ItemInteract
- **2467:** Image-mode framing + sticky pan capture/restore → `ImageController`
  (`imagecontroller_framing.cpp`). ImageView keeps thin host forwards + zoom chrome.

### Next
- size-book coordinator (optional)
- Dual PreferCache coordination (optional)

### Apply
```bash
git pull --ff-only …/biltoo-2467.1-own-image-mode-framing-7d823d8.bundle HEAD
```
