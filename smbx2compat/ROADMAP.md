# Roadmap: SMATRS on TheXTech-Lua (desktop, then Android)

Status as of 2026-11-04. Goal: play *Super Mario and the Rainbow Stars* (SMATRS,
an SMBX2 episode) on this engine, first on macOS, then on Android.

## Where we are

- Engine builds and runs on macOS arm64 (see `CLAUDE.md` for the fixes).
- `smbx2compat.lua` v0.1 loads (`[smbx2compat] loaded v0.1.0`).
- SMATRS level 1-1 loads, but its scripts stop immediately:
  - episode `luna.lua:2` — `getSMBXVersionString` is nil
  - `_scripts/smatrs/graphicalQuality.lua:22` — `Darkness` is nil
- ~~Blocker: Lua errors during `onLoop` kill the process~~ — fixed 2026-11-04
  (Phase 0 below).

## How big SMATRS is (measured)

| What | Count |
|---|---|
| Lua files / lines | 939 / ~270,000 |
| SMBX2 built-in libraries it requires | ~50 (`npcManager`, `blockManager`, `textplus`, `lib3d`, `handycam`, `particles`, `darkness`, `ext/easing`, `vectr`, `rng`, ...) |
| API uses | `vector` 4.8k, `Routine` 2.8k, `Graphics` 1.9k, `Misc` 1.5k, `Color` 1.5k, `mem` 1.4k, `SFX` 1.3k, `NPC` 1.1k, `Shader` 141 |
| `mem()` addresses | 16 global, 92 object offsets (SMATRS alone; the SMBX2 libraries add more) |
| Shader files | 189 |
| Custom NPC/block Lua (`npc-N.lua`) | 62 |
| Highest IDs used | NPC 780, block 1283, BGO 751+ |
| Engine limits today | `maxNPCType` 306, `maxBlockType` 702, `maxBackgroundType` 200 (`src/global_constants.h`) |

## Strategy decision (recommended)

**Run SMBX2's real built-in Lua libraries on an emulated LunaLua core**, rather than
hand-writing replacements in the shim.

- About 50 libraries and 270k lines of episode code can't be re-implemented by hand,
  and every re-implementation drifts from SMBX2's behaviour.
- Most of SMBX2's built-in libraries are themselves Lua (`npcManager`, `vectr`,
  `rng`, `textplus`, `ext/*`, ...). Once `require` finds them, they come for free.
  What they need underneath is LunaLua's **C++ API surface**: `mem()`, the object
  classes (`NPC`, `Block`, `Player`, ...), `Graphics.glDraw`, `Shader`, `Misc`,
  events and the input/text/audio functions.
- TheXTech already has SMBX 1.3 memory emulation (`src/script/luna/mememu.cpp`,
  `GetMem`/`MemAssign` for globals, `Player_t` and `NPC_t`, ~300 mapped offsets).
  That is the natural backend for `mem()`.

So `smbx2compat.lua` turns into the *LunaLua core layer*: thin Lua over new C++
bindings. The current hand-written NPC/Player wrappers get replaced as the real
classes land.

**Where the boundary is (confirmed 2026-11-04).** SMBX2's built-in scripts are in
`../TheXTech-data/smbx2/SMBX2 + SMATRS/data/scripts/` (858 files, ~175k lines;
graphics in `data/graphics/`). SMBX2 boots from `scripts/base/engine/main.lua`.
Only **17 files, `scripts/base/engine/ffi_*.lua`** (`ffi_npc`, `ffi_block`,
`ffi_player`, `ffi_graphics`, `ffi_mem`, `ffi_events`, `ffi_misc`, ...), talk to the
game: they `ffi.load("LunaDll.dll")`, call its `LunaLuaGet*` exports, and cast
raw SMBX memory. Everything above them is plain Lua. So the core layer = our
own replacements for those 17 files (same Lua-facing API, backed by TheXTech via
luabind/`mememu`), loaded in their place by a `require` redirect, plus whatever
`main.lua` expects from the host. Phases 1–2 below are mostly "port these files".

## Phases

### Phase 0 — Fix the `onLoop` error crash (blocker) — DONE 2026-11-04

Cause: our catch sites called `lua_pop` after `luabind::error`, whose constructor
had already popped the message, so every caught error popped one extra stack slot
and eventually wrecked LuaJIT's frame chain. Not an arm64 unwinding problem. The
error itself came from the shim comparing two luabind userdata with `~=` (no
`__eq`). Fixed both; engine errors now carry tracebacks and log once; checked with
`smoke/errtest.lvlx`. Original plan, kept for reference:

- Reproduce with the smoke level (`../TheXTech-data/smoke/bonus1.lvlx`, harness
  `luna.lua`). Run under `lldb`, break on `exit` / `lj_err_throw`.
- Find which `lua_State` raises the error and what its `cframe` is.
  Hypotheses: (a) luabind invokes or raises on a coroutine's `lua_State`
  (`Routine` uses coroutines); (b) arm64 external (DWARF) unwinding misses a frame
  through luabind/C++; (c) a Debug-only LuaJIT build flag mismatch.
- Rebuild LuaJIT with `LUA_USE_APICHECK`/`LUA_USE_ASSERT` to catch API misuse.
- Also: make the shim's `callSafe` use `xpcall` + `debug.traceback`, so script
  errors show where they came from.
- Done when: a deliberate `error()` in `onTick`/`onDraw` is logged and the game
  keeps running.

### Phase 1 — SMBX2 built-in libraries and the loader
- SMBX2's built-in scripts are downloaded and extracted (see "Where the
  boundary is"). Keep them in `../TheXTech-data/`, never in git.
- `require` search order like SMBX2: level folder → episode folder → built-in
  scripts (`data/scripts/`, `data/scripts/base/`), with `base/engine/ffi_*`
  redirected to our replacements. Read `base/engine/main.lua` for the boot order.
- Mirror SMBX2's startup order: core globals, then built-in libraries, then
  episode `luna.lua`, then level `luna.lua`; and `onInitAPI` / `onStart` timing.
- Core globals: `getSMBXVersionString`, `SMBX_VERSION`, `Defines`, `lunatime`,
  `Misc.*` basics, `Darkness` (stub first).
- Done when: SMATRS 1-1 runs all its scripts with rendering-heavy calls stubbed,
  and `smbx2compat.report()` lists what's missing.

### Phase 2 — `mem()` and object classes
- Lua `mem(addr, FIELD_*)` and `obj:mem(off, FIELD_*)` backed by `mememu`.
  Extend `mememu` to cover the offsets SMATRS and the libraries use (start with
  the 16 + 92 measured above), plus `Block`/`BGO`/`Layer`/`Section` objects.
- LunaLua-shaped classes: `NPC`, `Block`, `BGO`, `Player`, `Layer`, `Section`,
  `Camera`, `Warp`, `Liquid`, with `get`/`iterate`/`spawn` and the field names
  SMBX2 uses.
- Done when: `npcManager` / `blockManager` load and register SMATRS's custom
  NPCs.

### Phase 3 — Extended IDs and custom NPCs/blocks
- Raise `maxNPCType` / `maxBlockType` / `maxBackgroundType` past SMATRS's needs
  (≥ 1000 / 1300 / 1000), including arrays sized from them and save formats.
- Load SMBX2-style per-ID config (`npc-N.txt`, `npc-N.ini`) and graphics for IDs
  with no built-in definition.
- Let Lua own NPC AI for custom IDs (`npcManager` behaviour); engine physics/AI
  runs only when the config asks for it.
- Done when: custom NPCs in 1-1 appear and move.

### Phase 4 — Rendering
- `Graphics.drawImage*` with priority, opacity, source rects, `Graphics.glDraw`
  (vertex arrays), capture buffers, `Graphics.drawBox` / `drawScreen`.
- `Shader` — TheXTech's GL renderer already compiles GLSL and has the ANGLE
  translator; map SMBX2's shader API onto it.
- `Darkness`, `particles`, `textplus` fonts, `paralx2` backgrounds.
- Done when: 1-1 looks right.

### Phase 5 — Events, input, audio, saves
- Cancellable events (`eventObj.cancelled`): fire `onNPCHarm` / `onPlayerHarm` /
  `onBlockHit` *before* the engine acts, and respect cancel.
- `onEvent`, `onInputUpdate`, alt-jump/alt-run, `player:harm()` / `kill()`.
- `SaveData` / `GameData` persisted per episode; `Audio.*` music control.
- Done when: Chapter 1 levels are completable.

### Phase 6 — World map and episode flow
- SMATRS uses a 3D world map (`lib3d`). Interim: start levels from a simple
  level list or TheXTech's 2D world map. Full `lib3d` support depends on Phase 4
  (`glDraw` + shaders, 3D meshes, depth).
- Done when: Chapter 1 is playable start to finish on desktop.

### Phase 7 — Android
- Turn on Lua in `android-project/thextech/build.gradle`
  (`-DTHEXTECH_ENABLE_LUA=ON`).
- LuaJIT cross-compile: arm64-v8a and x86_64 work from a macOS host; 32-bit ABIs
  (armeabi-v7a, x86) need a 32-bit host compiler, which macOS lacks. The target
  (AYN Odin 3) is arm64, so: **arm64-v8a (+ x86_64 for the emulator) only**.
- Build Release with optimisations (the Debug build loads slowly).
- Assets: TheXTech asset pack + SMBX2 built-in scripts/graphics + SMATRS
  (~0.5–1 GB). Sideload to app storage, not bundled in the APK.
- Controls: the Odin 3's built-in gamepad goes through SDL's game-controller
  support; map SMATRS's extra inputs (alt-jump/alt-run, menus). Touch is optional.
- Performance pass on a real device (270k lines of Lua per frame budget).
- Done when: SMATRS 1-1 runs on your phone.

## Milestones

1. M0 — `onLoop` errors no longer crash (Phase 0) — reached 2026-11-04
2. M1 — all SMATRS scripts load, stubs logged (Phase 1)
3. M2 — 1-1 playable on desktop, rough visuals (Phases 2–3)
4. M3 — 1-1 looks right (Phase 4)
5. M4 — Chapter 1 playable on desktop (Phases 5–6)
6. M5 — Android build with Lua runs the smoke level (Phase 7 setup)
7. M6 — SMATRS on Android

Phases 0–1 are days of work; 2–6 are the bulk and realistically take weeks to
months. Phase 7 setup can start any time after Phase 0.

## Risks

- **Scale:** SMATRS leans on almost all of SMBX2. Expect a long tail of
  "one more API".
- **`mem()` fidelity:** libraries poke undocumented SMBX 1.3 offsets; every one
  needs a mapping.
- **Shaders / `lib3d`:** the 3D world map may be the hardest single feature.
- **Licensing:** SMBX2's built-in libraries and SMATRS assets are fine to use
  privately, but never commit or redistribute them.
- **Separate fork:** we no longer merge upstream, so fixes from vkdfkc or TheXTech
  have to be cherry-picked by hand if ever wanted.

## Decisions (Jake, 2026-11-04)

1. **SMBX2 built-in libraries:** approved downloading the SMATRS "Download All"
   bundle to extract `data/scripts` and `data/graphics` (kept in
   `../TheXTech-data/`, nothing from it is run, never committed).
2. **Target device: AYN Odin 3** (Android handheld, arm64, built-in gamepad).
   So: **arm64-v8a only** (+ x86_64 for the emulator); the built-in controls map
   through SDL's game-controller support, so touch controls are low priority. VERIFY
   the device's GLES version and screen resolution when Phase 7 starts; SMATRS
   targets SMBX2's 800×600, so expect letterboxing or a widescreen pass.
3. **World map:** start with a simple level list; the 3D map comes later.
4. **Android data:** ~1 GB sideloaded to the device is fine.
5. **Approach:** run SMBX2's real built-in libraries on an emulated LunaLua core
   (explained in "Strategy decision" above).
6. **Separate fork:** don't merge from vkdfkc or TheXTech going forward; the
   `upstream` remote stays only for reference. Engine code can be changed freely.
7. **Tools:** ask before each install (lldb/Xcode, Android SDK/NDK, Gradle).
