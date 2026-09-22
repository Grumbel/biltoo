<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ECS ground truth vs GUI parallel authorities

Investigation snapshot (tip 2175). Ground truth for **bound** session images:

| Concern | Authority |
|---------|-----------|
| Identity | `SessionImageId` |
| Content orient / flip / crop | ItemWorld sparse (`contentBake`, `crop`, …) |
| Color grade | ItemWorld `Color` |
| Workspace pose | ItemWorld `Placement` (or Workspace durable snapshot of same) |
| Native file size | size book / thumtoo |
| Layout box | `ContentXform::layoutSize(native, want)` |
| Display pixels | materialize(host-raw, want) or tiles in native space |

Anything else that **drives paint, aspect, or orient** is a parallel authority.

---

## 1. Applied ContentXform on live `ImageItem` (high)

**Where:** `itemAppliedContentXform` / runtime table; `wantAppearanceForItem` prefers applied over sparse ItemWorld when set.

**Why it exists:** Mid-edit authority during rotate/crop on the GUI thread.

**Bypass risk:** After mode switch, live item is destroyed; applied is gone. New underlay rebuilds from ItemWorld. If ItemWorld lag behind applied at leave, open shows store not what user last saw. Conversely, displayReady soft carrying old applied while ItemWorld advanced → wrong orient (mitigated by 2174 removing stash soft).

**Respects ECS when:** bake/commit writes ItemWorld before leave; Image underlay only materializes from store.

---

## 2. Filmstrip `m_sessionIdImageOverrides` (high)

**Where:** `ThumbnailBar` hash; `sampleForImageModePending` returns displayReady; paint prefers override pixmap.

**Why:** Fast filmstrip paint of oriented/cropped thumbs without re-decode.

**Bypass risk:** Parallel **pixel** store for the same SessionImageId. Can lag ItemWorld after rotate if `sessionAppearanceChanged` skipped; can win over host-raw materialize path in Image pending soft (displayReady attach skips rematerialize). Aspect now forced through `contentLayoutSize` (2169+) — pixels still parallel.

**Respects ECS when:** override is always refreshed from `sessionAppearanceImage` after ItemWorld commit; Image mode prefers materialize(want) over stale override (optional strict mode).

---

## 3. ImageCache path-keyed host (high when contaminated)

**Where:** ~32 `ImageCache::put` sites; `installDisplayPixels` puts incoming as host-raw.

**Contract:** path → unoriented host sample only.

**Bypass risk:** Any put of content-baked soft (old seedFrom, filmstrip icon, wrong ladder) makes `materializeDisplay(want)` double-apply. Filmstrip `sampleForImageModePending` may `ImageCache::put(path, icon)` for non-override rows.

**Respects ECS when:** only true host-raw enters cache; never displayImage with applied xform.

---

## 4. Path map + XDG `loadContentAppearance` (medium)

**Where:** `getPathState` / `setPathState`; `ThumtooCache::loadContentAppearance`; `contentLayoutSize` XDG fallback; `seedSessionAppearanceFromState`; filmstrip `layoutAspectForRow` fallback; `applyStoredAppearanceToThumb`.

**Why:** Unbound tiles; cold seed before ItemWorld row exists; durable disk for path.

**Bypass risk for bound ids:** Path crop must not layout every session row sharing a file (IDENTITY). Orient-only XDG seed is intentional until ItemWorld has a row. Dual-write path map on bound tiles is a bug (mostly gated).

**Respects ECS when:** bound → ItemWorld only after seed; path map placement never for bound.

---

## 5. Workspace `m_savedItems` durable snapshot (medium)

**Where:** `WorkspaceController::snapshot` copies content fields + pose into `m_savedItems`.

**Why:** Rebuild Workspace after stash discarded.

**Bypass risk:** Snapshot is a **copy** of appearance at leave time. If ItemWorld is updated while tiles are stashed (e.g. edit in Image on same id — rare), restore can reapply stale content onto tiles. Pose belongs in snapshot; content should re-read ItemWorld on restore.

**Respects ECS when:** restore applies pose from snapshot, content from `sessionAppearanceValue(id)`.

---

## 6. Mode-stash `ImageItem*` presentation (medium — partially fixed)

**Where:** Gallery/Workspace `m_stashedItems`.

**Why:** Fast return with pixels + pose.

**Bypass risk:** Was used as Image underlay soft (2174 removed). Still: restore reattaches old display pixels; should rematerialize from ItemWorld if content changed while away.

**Respects ECS when:** stash is presentation cache only; content ops always re-validated from ItemWorld on restore/open.

**2196–2197:** `clearStaleAppliedFingerprintIfNeeded` shared by Gallery rematerialize and Workspace stash restore when store advanced while stashed.

---

## 7. `ImageItem::bakeRotate90` / `bakeFlip` incremental (medium)

**Where:** `imageitem.cpp`; last resort in `bakeItemRotate90` when host missing.

**Why:** GUI feedback without host in cache.

**Bypass risk:** Incremental transform on already-oriented display → not pure materialize(host, absolute want). Can desync fingerprint vs pixels until host rematerialize completes.

**Respects ECS when:** only absolute materialize from host-raw; never incremental on display.

---

## 8. Placement hFlip / rotation in filmstrip export (low–medium)

**Where:** `sessionAppearanceImage` applies **placement** hFlip/vFlip on top of displayImage.

**Why:** Legacy display flips on placement.

**Bypass risk:** Content should be fully in content bake; placement flips are a second channel. Filmstrip can show placement flip not in ItemWorld contentBake.

---

## 9. Gallery pack reads `imageSize()` (low if intrinsic correct)

**Where:** `gallerylayout.cpp` `nativeSize` → `item->imageSize()`.

**Contract:** intrinsic is already content layout size.

**Bypass risk:** If any path sets intrinsic to file-native while content orient exists, pack aspect is wrong. Mitigated by `contentLayoutSize` on placeholders (2170+).

---

## 10. Slideshow appearance snapshot helper (low)

**Where:** `snapshotSlideshowContentAppearance` — ItemWorld then path map.

**Mostly aligned** with ECS; path fallback for unbound.

---

## 11. Crop session local orient buffers (low)

**Where:** `cropsession.cpp` orientOnly from appearance / live.

**Scoped to crop draft**; commit should write ItemWorld.

---

## Priority cleanup order

1. Image underlay: never displayReady unless override fingerprint matches ItemWorld want (filmstrip path still open).
2. Workspace restore: content from ItemWorld, not `m_savedItems` content fields. (**2194–2195**, restore merges ItemWorld content)
3. Ban incremental `bakeRotate90` when host exists; require host for content edit. (**2200**)
4. Filmstrip override: treat as pure cache of `sessionAppearanceImage` after ItemWorld commit only.
5. Audit every `ImageCache::put` for baked samples. (see IMAGECACHE_PUT_AUDIT.md)
6. Delete path-map content writes for bound session ids — **verified 2211**: `rememberItemState`, Gallery pack afterEach, Workspace snapshot write Placement-only for bound ids; path map is unbound-only.

---

## Already aligned (reference)

- `applyContentLayoutSize` / `wantAppearanceForItem` install path  
- `contentLayoutSize` + filmstrip `LayoutAspectProvider`  
- Image pending soft: no mode-stash (2174)  
- `tileNativeSize` refuses oriented layout as native (2172)
- `SessionAppearance::orientAuthorityWant` (2211) — single gate for placement-only strip
- Workspace bound snapshot pose-only + Placement bridge restore (2212–2214)
- `resolveContentEditSessionId` for ImageView content paths (2215)
- Normative summary: [CONTENTXFORM_AUTHORITY.md](CONTENTXFORM_AUTHORITY.md) (2216)
- `DisplayPipelineController::resolveItemSessionId` (2219–2220) — pipeline materialize/layout/climb

## Work status

| # | Issue | Status |
|---|--------|--------|
| 1 | Image underlay trusts filmstrip displayReady | **2176/2202** host materialize; **2204** sampleForImageModePending never returns id override as displayReady |
| 2 | Filmstrip pixel override parallel store | Override after rotate; **2179** cold paint; **2204** Image pending soft ignores id override |
| 3 | ImageCache host-raw contract | **2176–2178** see IMAGECACHE_PUT_AUDIT.md |
| 4 | Applied ContentXform vs store at leave | **2177** flush on setViewMode; **2203** clearLiveContentMeta after flush (no applied on stash) |
| 5 | Workspace m_savedItems / freeze→Color | **2177** restore; **2194**/**2213** snapshot Placement-only (clearedContentOps); **2195** rememberItemState Placement-only; persist/bind keep durable Color over lag; **2212** updateWorkspaceSavedAppearance Placement-only; **2214** pose merge via applyPlacementToState |
| 6 | Incremental bakeRotate90 | **2178** disk host first; **2200** no incremental — clear pixels + async when host missing |
| 7 | Path/XDG seed / orient authority | **2205** Image no path-XDG seed; **2208–2210** withoutContentOrient + hasContentOrient + wantAppearance strip; **2211** `orientAuthorityWant` + unit tests + path-map bound-write verify; **2215/2219–2220** resolveContentEditSessionId / resolveItemSessionId |

