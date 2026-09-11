# Content id vs variant id

Brainstorm / design note (2026-09-11). Not yet implemented as schema changes;
records the agreed mental model so later work does not re-mix the two layers.

Related: [IDENTITY.md](IDENTITY.md), [DOMAIN.md](DOMAIN.md), thumtoo
[DESIGN.md](https://github.com/Grumbel/thumtoo/blob/master/DESIGN.md)
(content identity / locators).

---

## Problem

Mixing “what the bytes are” and “this edited instance on the canvas” into one
hash (or one path-keyed map) is what breaks duplicate handling, independent
crops, filmstrip overrides, and cache invalidation.

You need **two different identities**.

---

## Two layers

| Layer | Meaning | Stable when… |
| ----- | ------- | ------------ |
| **Content id** | Bytes of the *source* (file / archive member / page raster input) | You only change presentation |
| **Variant id** | One concrete *view* of that content (crop, grade, flip, …) | You change non-destructive edits |

- “Find by hash” / “same book page” / “tags on this file” almost always means
  **content id**.
- “This tile on the canvas / this filmstrip row / this appearance blob” is a
  **variant** (session image), not a second content hash.

---

## Practical schemes

### 1. Content hash + variant id (recommended)

- `content_id = hash(source bytes)` (or thumtoo’s existing `sha256:…` / content row)
- `variant_id = SessionImageId` (monotonic `qint64` biltoo already allocates)
- Appearance / crop / grade keyed by `variant_id`
- Lookup “everything derived from this file” → index by `content_id`
- Lookup “this exact edited instance” → `variant_id`

Same file dropped twice → same `content_id`, two `variant_id`s. Good.

### 2. Content hash + edit fingerprint

- `variant_key = hash(content_id ‖ canonical(edit_params))`
- Same edits ⇒ same key (cache-friendly, dedupes identical grades)
- Different crops ⇒ different keys
- Harder for “same logical duplicate, temporarily different grade” and for
  unstable param serialization

Use this for **cache keys** (thumtoo ladder of a filtered pipe), not as the
only identity for session rows.

### 3. hash + uuid always

- `id = content_id + ":" + uuid`
- Simple, never collides
- You must still store `content_id` separately or you lose “find siblings by
  hash”

### 4. Don’t hash the displayed pixels for identity

Hashing the baked pixmap makes every grade a new “file”. Fine for export /
“save derivative”, wrong for library or session identity.

---

## How this maps to biltoo / thumtoo today

You already lean this way:

| System | Role |
| ------ | ---- |
| **thumtoo** | `content_id` / locator URI / path for bytes, size, ladder, TOC, tags |
| **biltoo** | `SessionImageId` per session row; appearance maps by id; path may repeat |

That is the right split. Non-destructive change must **not** change
`content_id`. Only:

- `SessionImageId` (instance / variant), and
- appearance blob (crop, colour, flips), and optionally
- a **derived cache key** if you later materialize filtered URIs
  (`//crop:…`, invert, …)

thumtoo DESIGN already states: Biltoo `SessionImageId` remains session/edit
identity and must **not** key durable pixels or tags.

---

## Rules of thumb

1. **One content hash → many variants** is normal; never the reverse for
   “same bytes”.
2. Filmstrip / undo / membership / canvas bind key off **variant id**
   (`SessionImageId`).
3. “Find duplicates / same book page / shared tags” keys off **content id**.
4. Export / “save derivative” may create **new content** (new hash); a
   session edit does not.
5. If pixel pipes land in thumtoo, treat
   `canonical(uri with filters)` as a **cache key**, and keep the base file
   (or page) hash as **content id**.
6. Path remains a **locator** (how to open bytes today), not identity. Prefer
   content id once known; path+mtime is only a fast staleness fingerprint.

---

## Minimal model

```text
Content {
  content_id,          // sha256:… or provisional until hashed
  source_uri / locators,
  width, height, …
}

Variant {              // biltoo session image
  variant_id,          // SessionImageId
  content_id,          // or path until content_id known
  appearance,          // crop, flips, grade, …
  pose?                // Workspace-only
}

CacheKey {             // thumtoo only
  hash(content_id + normalized_filters + size/edge)
}
```

**hash+uuid** works if the uuid *is* the variant id and the hash stays pure
content. Prefer **explicit fields** over a single concatenated string so you
can index both ways (siblings by content, instance by variant).

---

## What not to do

- Key appearance, filmstrip overrides, or peer crop sync by path alone once
  duplicates exist (already forbidden in IDENTITY.md §13–14).
- Treat a filtered/crop URI as a new content id for session membership.
- Recycle `SessionImageId` after delete/clear (IDENTITY.md §14).
- Store durable ladder/tags under `SessionImageId` inside thumtoo.

---

## Open questions (later)

- When / whether biltoo surfaces `content_id` in the UI (duplicate finder,
  “same file” indicator).
- Project file (`.biltoo`): already content-addresses external files by
  SHA-256; keep that as content layer, session rows as variants.
- Pixel-filter pipes (`//crop:`, colour grade as URI): cache key vs content
  id boundary must stay sharp (see thumtoo TODO filter notes).
- Provisional content ids before hash completes (thumtoo already sketches this).

---

## Status

Brainstorm only. No schema or API change required for current SessionImageId
work. Revisit when:

- adding content-hash UI or duplicate detection in biltoo, or
- implementing filtered URIs / pixel pipes that need stable cache keys
  distinct from session variants.
