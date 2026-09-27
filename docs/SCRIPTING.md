<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Scripting API — brainstorm (hypothetical)

**Status:** design sketch only. No implementation commitment. No runtime code
in this note. Second pass: tighten layering, handles vs values, ambient
“current”, and anti-patterns.

**Related:** [DOMAIN.md](../DOMAIN.md) (especially
[World vs viewpoint](../DOMAIN.md#world-vs-viewpoint)),
[SCENE_LANGUAGE_BRAINSTORM.md](SCENE_LANGUAGE_BRAINSTORM.md),
[TEXT_TO_SPEECH.md](TEXT_TO_SPEECH.md), [ACTIVITY.md](ACTIVITY.md),
[MODE_OWNERSHIP.md](MODE_OWNERSHIP.md), [IDENTITY.md](../IDENTITY.md),
[HANDLES.md](../HANDLES.md), [TAGS_AND_BOOKMARKS.md](TAGS_AND_BOOKMARKS.md).

---

## 1. Problem

Biltoo already has a rich **world**: ordered session, stable
`SessionImageId`s, content appearance, text layers, speech plans, async tile
and OCR work. Most of that is only reachable by clicking through modes.

Power users and future automation want to:

- Inspect and reshape the session without playing Gallery ↔ Image ping-pong.
- Drive **speech** and watch the **speech cursor** while the canvas shows
  something else.
- Observe **activity** (tiles, soft, archives) as data, not only a status line.
- Automate chores (OCR a range, export, retag, reorder) without a new C++
  feature each time.

**Hard rule:** scripts address the **world**, not “whatever widget has focus.”
A script that only works in Image mode is the wrong shape — same law as
[World vs viewpoint](../DOMAIN.md#world-vs-viewpoint).

**Not the same as** [SCENE_LANGUAGE_BRAINSTORM.md](SCENE_LANGUAGE_BRAINSTORM.md).
That note is declarative *presentation graphs* (scenes, closed actions).
This note is a *programming surface* (loops, hooks, composition) over domain
ops. They may meet later; neither blocks the other.

---

## 2. Layer cake

Keep three layers distinct so the idea does not collapse into “embed a
language and hope.”

```text
┌─────────────────────────────────────────────────────────────┐
│  Transports (optional)                                      │
│  REPL dock · script file · --script CLI · later: RPC        │
├─────────────────────────────────────────────────────────────┤
│  Language skin (optional embed)                             │
│  Lua or Squirrel (or none — JSON command stream is enough)  │
├─────────────────────────────────────────────────────────────┤
│  Host command table  ←── single source of truth             │
│  Named ops + args + docstring + capability tier             │
│  Same table keybindings / menus should eventually call      │
├─────────────────────────────────────────────────────────────┤
│  Domain / world (already exists in C++)                     │
│  Session · appearance · text · speech · activity · view ops │
└─────────────────────────────────────────────────────────────┘
```

The valuable design work is the **command table and value types**, not the
choice of Lua vs Squirrel. A language skin is sugar over `invoke("session.append", …)`.

---

## 3. Lessons from Emacs — kept and rejected

### Keep

| Idea | Biltoo form |
|------|-------------|
| Commands as data | One registry: name, parameters, docstring, tier, handler. |
| Hooks | Finite set of **world** events (session, speech, activity, mode). |
| REPL | Live console against a running process beats batch-only. |
| Self-description | `help` / completion from the same registry metadata. |
| Advice (light) | Optional pre/post wrappers for logging — not forks of ops. |

### Reject or invert

| Emacs habit | Why not |
|-------------|---------|
| Buffer / point as the universe | Canvas items and “current path” are viewpoints. Prefer `SessionImageId`. |
| Ambient global “current” for everything | Ambient *session current* may exist for UX, but every op should accept an **explicit id** when the caller has one. |
| Scripts building arbitrary UI | No script-built Qt trees in v1. Host-defined surfaces only. |
| Omnipotent interpreter by default | Capability tiers (observe / session / file). |
| Lisp-required culture | Language is a skin; contributors should not need Scheme to automate a session. |

---

## 4. Design principles

1. **World first** — session, speech, activity, tags; mode is a readable
   property, not the namespace for all ops.
2. **One core** — scripts call the same domain operations the UI uses. No
   parallel session model for “script mode.”
3. **Ids over indices** — `SessionImageId` is durable; list index is order and
   may shift ([IDENTITY.md](../IDENTITY.md)).
4. **Values cross the boundary; pointers do not** — scripts never see
   `ImageItem*`, `QGraphicsScene`, or raw Qt types.
5. **Explicit targets preferred** — `speak.page(id)` over “speak whatever is
   current” when the script already knows the id.
6. **Snapshots for reads** — iterating `session.ids()` uses a consistent
   snapshot; mutations do not magically resize a live iterator mid-loop.
7. **Honest failure** — unknown id, missing text layer, cancelled speech →
   structured errors, not silent success.
8. **Thread policy is documented per op** — mutations marshal like UI actions;
   long work stays async (hooks / completion events).
9. **Finite surface** — grow the command table deliberately; do not expose
   internal controllers “for convenience.”
10. **Optional** — a build without an interpreter remains first-class; the
    command table can still serve tests and a future RPC.

---

## 5. Handles vs values

**Handles** are small host-owned references the script may hold:

| Handle | Identifies | Goes stale when |
|--------|------------|-----------------|
| `SessionImageId` | One session row | Row removed (id never reused) |
| Speech plan id (optional) | Active plan | Plan stopped / replaced |
| Activity id | One tracked op | Op leaves the recent ring |

**Values** are plain data copies returned from queries:

- paths (strings), dimensions, layout name, mode name  
- appearance records (crop rect, flips, content rotation)  
- text regions (id, kind, page-space bbox, text)  
- activity records (kind, uri, phase, units)  
- speech cursor `{ session_image_id, region_id, … }`

Rules:

- Returning a value **copies**; later UI edits do not mutate the script’s table.
- Holding a handle across a mode switch is **fine** (world outlives viewpoint).
- Holding a handle across **remove** yields a defined error on next use.
- No deep object graphs of live host objects in the script heap.

---

## 6. Ambient “current” vs explicit targets

The UI has a session cursor and a mode. Scripts may read them:

```text
session.current_id
session.current_index
view.mode
```

But **mutating ops should take explicit targets** whenever the caller has them:

| Prefer | Avoid as the only form |
|--------|------------------------|
| `session.remove({ id1, id2 })` | `session.remove_current()` only |
| `speech.speak_page(id)` | `speech.speak()` meaning “whatever Image shows” |
| `view.goto_image(id)` | `view.goto_current()` as the sole navigation API |

Convenience wrappers that use ambient current are OK if they are thin aliases
documented as such. Automation and tests should prefer explicit ids so hooks
and multi-step scripts do not race the user’s cursor.

---

## 7. Namespaces (illustrative)

```text
host.version / host.invoke(name, args) / host.help(name?)
host.commands()                     -- registry dump

session                             -- world: membership + order
  .snapshot() -> { ids, current_id, paths_by_id }
  .ids() / .length
  .current_id / .current_index
  .get(id) -> session_image value
  .append(paths) / .remove(ids) / .reorder(ids)
  .set_current(id)

appearance                          -- world: per-id content transforms
  .get(id) / .set(id, record)       -- enters undo like UI

text                                -- world: layers & OCR
  .layer(id) -> nil | { regions… }
  .ocr(id, opts?) -> async job id

speech                              -- world: plan + cursor
  .state / .cursor / .plan_info
  .speak_page(id) / .speak_selection()
  .pause() / .resume() / .stop()

activity                            -- world: async work
  .snapshot() -> { running, recent }

view                                -- viewpoint (allowed to change presentation)
  .mode / .set_mode(m)
  .goto_image(id)
  .gallery_layout / .set_gallery_layout(name)
  .request_highlight_speech_cursor()  -- host paints; script does not pack tiles

tags / bookmarks                    -- when those features exist
  … see TAGS_AND_BOOKMARKS.md
```

`view.*` is intentionally small. Scripts do not set per-tile scene positions in
Gallery; they do not own Workspace chrome. Presentation engines stay in C++
(or future SDL).

---

## 8. Commands and hooks

### Command table

Every exported op is a row:

```text
name:        "session.append"
args:        paths: string[]
returns:     ids: SessionImageId[]
tier:        session
doc:         "Append paths to the session; returns new ids in order."
undo:        yes   -- joins the UI undo stack when it mutates document state
thread:      gui   -- marshalled
```

Keybindings and menus should eventually call the **same** names. Scripts call
`host.invoke("session.append", { paths = { … } })` or sugar methods that do.

Growing the table: start from an inventory of actions MainWindow / ImageView
already expose (open, append, next, speak, layout). Do not invent script-only
ops that the UI cannot perform.

### Hooks (finite, host-defined)

| Event | Typical payload |
|-------|-----------------|
| `session.replaced` | `{ reason }` |
| `session.changed` | `{ added, removed, reordered }` |
| `session.current` | `{ id, index }` |
| `mode.changed` | `{ from, to }` |
| `speech.state` | `{ state }` |
| `speech.cursor` | `{ session_image_id, region_id, … }` |
| `activity.changed` | `{ snapshot }` |
| `ocr.finished` | `{ id, ok, error }` |
| `command.failed` | `{ name, error }` |

Handlers run on the host’s script/dispatch policy (document whether re-entrant
session mutation is allowed; safest default: queue mutations).

---

## 9. Worked scenarios

Illustrative Lua-ish sugar. Real syntax follows the chosen skin.

### 9.1 Dump session from any mode

```lua
local snap = session.snapshot()
for i, id in ipairs(snap.ids) do
  local im = session.get(id)
  print(i, id, im.path)
end
```

### 9.2 Speak a page, then watch in Gallery

```lua
local id = session.current_id
speech.speak_page(id)
view.set_mode("gallery")
-- plan keeps running; cursor events still fire
host.on("speech.cursor", function(cur)
  print("cursor", cur.session_image_id, cur.region_id)
end)
```

### 9.3 OCR every page that lacks a layer

```lua
for _, id in ipairs(session.ids()) do
  if text.layer(id) == nil then
    text.ocr(id)
  end
end
host.on("ocr.finished", function(ev)
  print(ev.id, ev.ok and "ok" or ev.error)
end)
```

### 9.4 Activity pulse

```lua
local s = activity.snapshot()
for _, op in ipairs(s.running) do
  print(op.kind, op.uri, op.phase, op.units_done, op.units_total)
end
```

---

## 10. Language skin

| Candidate | Notes |
|-----------|------|
| **Lua** | Default lean: tiny, proven C API, enough for hooks and tables. |
| **Squirrel** | Fine if the project wants C-like classes and accepts a smaller ecosystem. |
| **None (JSON ops)** | Valid v0: stdin/REPL sends `{ "op": "session.ids" }` → JSON value. Proves the table before embedding. |
| Python / JS / Scheme | Possible later skins over the **same** table; not required to start. |

Pick the skin when someone embeds; until then, specify ops and values as data.

---

## 11. Transports

| Channel | Role |
|---------|------|
| REPL dock | Exploration, support, agent debugging of world state. |
| Script file | `biltoo --script job.lua` or Run Script. |
| Init hooks | Optional user `scripts/init.*` registering hooks/commands. |
| Headless | Offscreen Qt where needed; session/speech/activity still real. |
| RPC (later) | Transport only — same op names, not a second API. |

Multi-window ([DOMAIN.md](../DOMAIN.md) shortcuts): v1 binds scripts to the
**window that owns the REPL** / the process’s primary session. A future
`host.windows[]` can wait until multi-session exists.

---

## 12. Concurrency, undo, capabilities

**Threading.** Queries that return snapshots should be consistent as of a
point in time. Mutations follow the same path as UI (GUI/session thread).
Async jobs (OCR, prepare) return a job id; completion is a hook.

**Undo.** Document-mutating ops (`session.*`, `appearance.*`) should push the
same undo stack as the equivalent UI action. Transient view changes (mode,
scroll) need not.

**Capabilities.**

| Tier | Examples |
|------|----------|
| `observe` | snapshots, cursor, mode read, help |
| `session` | membership, appearance, speak control, mode change |
| `file` | export, explicit writes |

Interactive REPL defaults to `session`. Batch scripts request `file` explicitly.

---

## 13. Anti-patterns

Flag these in review (script surface *or* C++ “helpers” for scripts):

1. **Mode-gated world reads** — “speech.cursor only works in Image mode.”
2. **Pointer smuggling** — userdata that is really `ImageItem*`.
3. **Second session list** — script-side cache of paths that can drift from
   `MainWindow` / `SessionDocument`.
4. **Silent ambient targeting** — op mutates “current” without documenting it
   when the user moved the cursor mid-script.
5. **Script-owned Gallery layout** — setting per-cell poses from script
   instead of asking for a named layout / SDL action.
6. **Unbounded `eval` of trusted host internals** — expose ops, not the C++
   heap.
7. **Blocking the GUI** on OCR/network inside `host.invoke`.

---

## 14. Relation to scene language

| | Scene language | Scripting (this note) |
|--|----------------|------------------------|
| Kind | Data: scenes, targets, closed actions | Program: control flow + hooks |
| Failure mode if overgrown | Second app framework | Second session model |
| Discipline | Closed action vocabulary | Finite command table + handles |

Scripts must not reimplement Gallery packing. If presentation must be data-
driven, that is SDL (or C++ engines), not an open draw API in v1.

---

## 15. Non-goals

- Script-built widget trees or a general GUI toolkit in-process.
- Full OS automation (arbitrary filesystem/network) as the default tier.
- Binary-stable C ABI on day one — stabilize **op names and value shapes**
  first; version the module (`host.api_version`).
- Blocking Gallery / OCR / TTS work on an interpreter.
- Requiring any particular language community (Emacs, Python, …) to use biltoo.

---

## 16. Phased path (napkin)

| Phase | Outcome |
|-------|---------|
| **0** | This doc + inventory of UI actions → candidate command names. |
| **A** | Command table in C++ callable from tests; JSON or minimal REPL; **observe-only**. |
| **B** | Session mutation + speech control + hooks; undo participation. |
| **C** | Language skin (Lua or Squirrel); init scripts; unify a few keybindings onto the table. |
| **D** | Optional external transport; `file` tier; user script directory conventions. |

Each phase is judged by world-first behaviour: if an op fails only because the
wrong mode is up, the export layer is wrong.

---

## 17. Open questions

1. **Lua vs Squirrel vs JSON-only v0** — decide at embed time; table first.
2. **Undo granularity** for bulk `session.append` of hundreds of paths.
3. **Multi-session** — `host.sessions[k]` vs single session; design handles so
   a second document does not break scripts.
4. **Text mutation** — beyond OCR run, how much region editing belongs here?
5. **Thumtoo** — expose `cache.ensure_tiles(id)` or only `activity` + existing
   prepare UX?
6. **Hook re-entrancy** — queue vs allow nested `invoke` from handlers.

---

## 18. One-sentence summary

**Publish the existing world through a finite, id-centred command table and
plain value snapshots — so a REPL, a script file, or a later RPC can automate
biltoo without caring which mode is on the canvas and without growing a second
core.**
