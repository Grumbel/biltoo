<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Scripting API — brainstorm (hypothetical)

**Status:** design sketch only. No implementation commitment. No runtime code
in this note.

**Related:** [DOMAIN.md](../DOMAIN.md) (especially
[World vs viewpoint](../DOMAIN.md#world-vs-viewpoint)),
[SCENE_LANGUAGE_BRAINSTORM.md](SCENE_LANGUAGE_BRAINSTORM.md),
[TEXT_TO_SPEECH.md](TEXT_TO_SPEECH.md), [ACTIVITY.md](ACTIVITY.md),
[MODE_OWNERSHIP.md](MODE_OWNERSHIP.md), [IDENTITY.md](../IDENTITY.md),
[TAGS_AND_BOOKMARKS.md](TAGS_AND_BOOKMARKS.md).

---

## 1. Why script biltoo at all?

Users and power users eventually want to:

- Drive the **session** from outside (batch open, reorder, export, tag).
- Observe and control **background world state** (speech cursor, tile activity)
  without clicking through modes.
- Automate recurring chores (OCR this range, speak from here, pack Gallery
  layout X, dump selection) without shipping a new C++ feature each time.
- Prototype UI experiments and research workflows against real domain objects.

The hard constraint from domain philosophy: scripts should talk to the
**world**, not to “whatever widget is focused.” Modes are viewpoints; a script
that only works while Image mode is up is already the wrong shape.

This is **orthogonal** to the scene-description idea in
[SCENE_LANGUAGE_BRAINSTORM.md](SCENE_LANGUAGE_BRAINSTORM.md). That note is about
*declarative presentation graphs* (HyperCard-ish cards, closed action
vocabulary). This note is about an **embeddable / external programming
surface** over the same world. They can coexist: SDL might one day call into
script handlers; scripts might emit scene ops. Neither requires the other on
day one.

---

## 2. What Emacs/Elisp gets right — and what to improve

### Worth stealing

| Elisp idea | Biltoo translation |
|------------|-------------------|
| **Commands are first-class** | Named ops (`session-next`, `speak-selection`) invokable from script, keys, and menus from one table. |
| **Hooks** | World events (`on-session-changed`, `on-speech-cursor`, `on-activity`) not only keymaps. |
| **Buffer-local vs global** | Session-local vs process-global bindings; avoid one ambient “current buffer” for everything. |
| **Advice / wrappers** | Optional thin wrappers around existing ops for logging and metrics — not a second implementation. |
| **REPL for exploration** | A live console against a running biltoo is worth more than a batch-only CLI. |
| **Self-describing help** | Every exported op has name, args, docstring; `help` / completion from the same metadata. |

### Improve on Emacs

Emacs grew by treating the **editor UI** as the data model (buffers, windows,
points). That couples automation to display. Biltoo should invert that:

1. **Domain objects are the API.** Session, session image, text layer, speech
   plan, activity snapshot — not `QGraphicsScene` items or dock widgets.
2. **Handles, not raw pointers.** Stable ids (`SessionImageId`, speech plan
   id, activity id) survive mode switches and stashes; scripts never hold
   `ImageItem*`.
3. **Explicit viewpoint.** “Current mode” and “current session index” are
   readable properties of the shell, not the only way to name a target.
   Prefer `session.image(id)` over “whatever is on screen.”
4. **Structured results.** Ops return values (tables, lists, errors) suitable
   for composition — not only side effects and echo area strings.
5. **Capability tiers.** Read-only observation, session mutation, and
   “may shell out / write disk” are separate privileges — not one omnipotent
   interpreter.
6. **No second widget tree.** Scripts do not build Qt UIs. They may request
   host-defined panels later; v1 is data + commands + hooks.

---

## 3. Language choice

The host is C++/Qt. The script surface must be embeddable, sandboxable, and
small enough that “biltoo without scripting” stays a supported build.

| Candidate | Pros | Cons |
|-----------|------|------|
| **Lua** | De-facto standard for C++ games/tools; tiny; excellent C API; coroutines. | 1-based arrays surprise some; less “OO” than Squirrel. |
| **Squirrel** | C-like / JS-like syntax; native classes; used in game tools; embeds cleanly. | Smaller ecosystem; fewer biltoo contributors will already know it. |
| **Wren** | Small, class-based, pleasant. | Niche; fewer battle-tested host bindings. |
| **Python** | Everyone knows it; great for batch. | Heavy runtime; versioning pain; harder to ship “optional and small.” |
| **Scheme / Guile** | Maximum Emacs kinship; macros. | Syntax and culture mismatch for many image-viewer users. |
| **JavaScript (e.g. QuickJS)** | Familiar; JSON-native. | Easy to accidentally promise web-scale APIs we do not want. |

**Recommendation (hypothetical):** prefer **Lua** as the default embed for
v1 (maturity, size, C++ interop). Treat **Squirrel** as an equally valid
choice if the project prefers stricter OO syntax and is willing to own the
embedding — nothing in the *API shape* below depends on Lua vs Squirrel.

Surface the same **host object model** either way. Language is a skin over
handles and ops.

Illustrative snippets below use a Lua-ish dialect for readability; a Squirrel
skin would look similar with `class` / `<-` syntax.

---

## 4. Design principles

1. **World first** — API centres on session, speech, activity, tags; mode is
   one property among many ([DOMAIN.md](../DOMAIN.md#world-vs-viewpoint)).
2. **No core forks** — scripting calls the same domain operations the UI uses.
   No parallel “script-only” session model.
3. **Ids over indices** — `SessionImageId` is the durable key; list index is
   order only and may shift ([IDENTITY.md](../IDENTITY.md)).
4. **Observe without owning** — reading speech cursor or activity must not
   require Image mode or a visible tile.
5. **Fail honestly** — missing OCR layer, cancelled speech, unknown id →
   structured errors, not silent no-ops.
6. **Host thread policy is explicit** — scripts scheduled on a worker must not
   touch Qt; world mutations go through a documented marshal point (GUI thread
   or a dedicated session lock). Detail is an implementation concern; the API
   docs must state which ops are sync vs async.
7. **Closed core, open composition** — the host exports a finite op set;
   scripts compose them. Prefer adding one well-named op over opening raw
   C++ internals.
8. **Optional** — builds without the interpreter remain first-class.

---

## 5. Object model (handles)

Conceptual namespaces. Names are illustrative.

```text
biltoo                          -- process / host
  .version
  .mode                         -- "image" | "gallery" | "workspace"
  .session                      -- current session (world)
  .speech                       -- speech subsystem
  .activity                     -- async work snapshot
  .view                         -- viewpoint helpers (camera, layout name)

session
  .length / #session
  .current_id                   -- SessionImageId or nil
  .current_index                -- 0-based order (may shift)
  .image(id) / .image_at(i)
  .ids()                        -- list of SessionImageId
  .append(paths)
  .remove(ids)
  .reorder(ids_in_new_order)
  .select(ids)                  -- session selection (filmstrip)

session_image                   -- one row in the session
  .id
  .path
  .index
  .appearance                   -- crop, flips, content rotation (data)
  .text_layer                   -- nil or text_layer handle
  .speak_page() / .speak_from(region_id)

text_layer
  .regions()                    -- id, kind, bbox page-space, text
  .region(id)

speech
  .state                        -- idle | speaking | paused
  .plan                         -- current plan or nil
  .cursor                       -- { session_image_id, region_id, … } or nil
  .speak_selection()
  .speak_page(id)
  .pause() / .resume() / .stop()

activity
  .snapshot()                   -- list of running/recent ops (kind, uri, phase, …)
  .on_change(handler)

view                            -- viewpoint, not world ownership
  .mode / .set_mode(m)
  .goto_image(id)               -- enter Image on that session image
  .gallery_layout() / .set_gallery_layout(name)
  .highlight_speech_cursor()    -- request Gallery/Image cues (host paints)
```

Scripts hold **handles** (small integer or userdata keyed by host tables), never
C++ pointers. When a session image is removed, its handle goes stale and ops
return a clear error.

---

## 6. Hypothetical API surface (illustrative)

Not a promise of function names — a shape check against real workflows.

### 6.1 Session inspection and mutation

```lua
-- List paths without caring about mode
for i, id in ipairs(session.ids()) do
  local im = session.image(id)
  print(i, id, im.path)
end

-- Append and focus without requiring Gallery
session.append({ "/data/scan/page-42.png" })
local id = session.ids()[#session.ids()]
view.goto_image(id)
```

### 6.2 Speech as world state

```lua
-- Start speech; user may switch to Gallery — plan keeps running
speech.speak_page(session.current_id)

biltoo.on("speech.cursor", function(cur)
  -- Gallery may highlight cur.session_image_id; script can log or auto-scroll
  print("speaking", cur.session_image_id, cur.region_id)
end)

-- Later, from any mode:
if speech.state == "speaking" then
  speech.pause()
end
```

### 6.3 Activity observation

```lua
local snap = activity.snapshot()
for _, op in ipairs(snap.running) do
  print(op.kind, op.uri, op.phase, op.units_done, op.units_total)
end
```

### 6.4 Text / OCR

```lua
local layer = session.image(id).text_layer
if not layer then
  biltoo.ocr.run(id)          -- async; completion via hook
else
  for _, r in ipairs(layer.regions()) do
    if r.kind == "body" then print(r.text) end
  end
end
```

### 6.5 Hooks (world events)

Suggested event names (finite set, host-defined):

| Event | Payload (sketch) |
|-------|------------------|
| `session.replaced` | `{ reason }` |
| `session.changed` | `{ added, removed, reordered }` |
| `session.current` | `{ id, index }` |
| `mode.changed` | `{ from, to }` |
| `speech.state` | `{ state }` |
| `speech.cursor` | `{ session_image_id, region_id, … }` |
| `activity.changed` | `{ snapshot }` |
| `ocr.finished` | `{ id, ok, error }` |

Hooks must not assume a particular mode. A `speech.cursor` handler that calls
into Gallery highlight goes through `view.highlight_speech_cursor()` (or the
host paints automatically); the script does not dig into pack items.

### 6.6 Commands table (Emacs-like)

Every user-visible action that is safe to automate is registered once:

```text
command "session-next"     → session navigation
command "speak-selection"  → speech.speak_selection
command "gallery-layout"   → args: layout name
```

Keybindings and menus invoke the same commands scripts call. Scripts can
`(command-run "session-next")` without reimplementing edge behaviour.

---

## 7. Ways to run scripts

| Channel | Role |
|---------|------|
| **REPL dock / console** | Live exploration against the running world; print handles and snapshots. |
| **Script files** | `biltoo --script job.lua` or File → Run Script; batch jobs. |
| **Init / config scripts** | Optional user config directory (`…/biltoo/scripts/init.lua`) for hooks and personal commands. |
| **Headless / CI** | Same ops with offscreen Qt where needed; activity and session still real. |
| **External control (later)** | Optional JSON-RPC or socket speaking the *same* op names — not a second API. |

v1 can be “REPL + file run” only. External control is a transport over the same
command table.

---

## 8. Concurrency and the GUI

- **Reads** of snapshot-style data (`activity.snapshot`, speech cursor copy,
  session id list) should be safe from a documented context (GUI thread or
  locked snapshot).
- **Mutations** to session, mode, and speech control marshal to the host’s
  session/GUI thread — same as UI actions.
- Long work (OCR, prepare tiles) stays asynchronous; scripts await via hooks
  or futures, not by spinning on the GUI thread.
- Script errors never abort the host process; they surface in the Messages /
  script console path.

---

## 9. Capabilities and safety

Suggested tiers (configuration, not deep OS sandbox on day one):

| Tier | Allowed |
|------|---------|
| **Observe** | session read, speech state, activity snapshot, mode read |
| **Session** | append/remove/reorder, appearance tweaks, mode changes, speak control |
| **File** | export, explicit write paths, shell-out (if ever) |

Default interactive REPL = Session. Batch `--script` may require an explicit
`--allow-file` for export. Never implicit full filesystem from a downloaded
script.

---

## 10. Relation to scene language

| | Scene language (SDL) | Scripting API (this note) |
|--|----------------------|---------------------------|
| Nature | Data: scenes, targets, closed actions | Program: loops, hooks, composition |
| Author | Generators + optional hand-edit | User / extension author |
| Risk if overgrown | Second app framework | Second session model |
| Stay honest by | Closed action vocabulary | Finite command table + handles |

Scripts should not become an alternate Gallery implementation. If a script
needs a new presentation, that is an SDL or C++ feature — not an unbounded
`draw_tile()` surface in v1.

---

## 11. Non-goals

- Replacing Qt UI with a script-built widget tree.
- Full OS scripting (files, network) as the default posture.
- Guaranteeing binary-stable C ABI for scripts across releases (stabilize
  *names and meanings* first; version the module).
- Porting Emacs or shipping Guile unless the project explicitly chooses Lisp.
- Blocking current Gallery / OCR / TTS work on an interpreter landing.

---

## 12. Phased dream (only if this ever leaves the napkin)

| Phase | Deliverable |
|-------|-------------|
| **0** | This doc + command inventory: list existing UI actions worth exporting. |
| **A** | In-process interpreter, observe-only: session list, mode, speech state, activity snapshot; REPL dock. |
| **B** | Session mutations + speak control + hooks (`session.*`, `speech.*`). |
| **C** | Command table unified with keybindings; script files; init hooks. |
| **D** | Optional external transport; capability tiers; package user scripts. |

Each phase must keep “world first”: if a feature only works while Image mode is
active for an accidental reason, that is a bug in the export layer.

---

## 13. Open questions

1. Lua vs Squirrel vs QuickJS — pick when someone is ready to embed, not before.
2. Should appearance edits from scripts enter the same undo stack as UI?
   (Almost certainly yes.)
3. Multi-document world: is `biltoo.session` the only session, or
   `biltoo.sessions[k]` with a current pointer? Design handles now so a second
   session does not break scripts.
4. How much text-layer mutation (not just OCR run) belongs in scripts vs UI?
5. Do we expose thumtoo prepare/status directly, or only via `activity` and a
   few high-level `cache.ensure_tiles(id)` ops?

---

## 14. One-sentence summary

**Export the world (session, speech, activity) through stable handles, a finite
command table, and mode-independent hooks — so automation and a live REPL can
drive biltoo without caring which viewpoint is on the canvas, and without
growing a second core.**
