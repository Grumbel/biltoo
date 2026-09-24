# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2484.1-own-pending-appearance-in-bind-book** (base `7d823d8`).

### Ownership transfer
- **PendingItemAppearanceBook** nested in SessionBindBook
- Session wipe: single `m_bindBook.clear()` clears binds + staged appearance

### Apply
```bash
git pull --ff-only …/biltoo-2484.1-own-pending-appearance-in-bind-book-7d823d8.bundle HEAD
```
