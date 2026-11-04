# Roadmap: SMATRS on TheXTech-Lua (desktop, then Android)

Status as of 2026-11-04. Goal: play *Super Mario and the Rainbow Stars* (SMATRS,
an SMBX2 episode) on this engine, first on macOS, then on Android.

## Where we are

- Engine builds and runs on macOS arm64 (see `CLAUDE.md` for the fixes).
- `smbx2compat.lua` v0.1 loads (`[smbx2compat] loaded v0.1.0`).
- SMATRS level 1-1 loads, but its scripts stop immediately:
  - episode `luna.lua:2` — `getSMBXVersionString` is nil
  - `_scripts/smatrs/graphicalQuality.lua:22` — `Darkness` is nil
- **Blocker:** a Lua error raised during the per-frame `onLoop` call kills the
  process (LuaJIT `lj_err.c:200`, "No C frame" → `exit()`).

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

## Phases

### Phase 0 — Fix the `onLoop` error crash (blocker)
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
- Get SMBX2's `data/scripts` tree (the built-in libraries) from the SMATRS
  "Download All" bundle (VERIFY exact layout). Keep it in `../TheXTech-data/`,
  never in git.
- `require` search order like SMBX2: level folder → episode folder → built-in
  scripts (`scripts/`, `scripts/base/`, VERIFY).
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
  (armeabi-v7a, x86) need a 32-bit host compiler, which macOS lacks. Plan:
  **arm64-v8a (+ x86_64 for the emulator) only**.
- Build Release with optimisations (the Debug build loads slowly).
- Assets: TheXTech asset pack + SMBX2 built-in scripts/graphics + SMATRS
  (~0.5–1 GB). Sideload to app storage, not bundled in the APK.
- Touch controls for SMATRS's extra inputs.
- Performance pass on a real device (270k lines of Lua per frame budget).
- Done when: SMATRS 1-1 runs on your phone.

## Milestones

1. M0 — `onLoop` errors no longer crash (Phase 0)
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
- **Upstream drift:** the fork (`upstream` remote) and TheXTech keep moving;
  keep our engine edits marked `// smbx2compat:` to ease merges.

## Open questions for Jake

1. **SMBX2 built-in libraries:** OK to download the SMATRS "Download All" bundle
   (458 MB, Google Drive, from the official SMATRS download page) just to extract
   `data/scripts` and `data/graphics`? Nothing from it would be run. (On a work
   machine, so your call.)
2. **Android ABIs:** OK to support only arm64-v8a (plus x86_64 for the emulator)?
   What phone will you test on?
3. **World map:** OK to start with a simple level list and leave the 3D map for
   later?
4. **Android assets:** sideloading the ~1 GB of data to the phone is fine for
   personal use?
5. **Approach:** happy with "real SMBX2 libraries on an emulated core" over
   growing the hand-written shim?
6. **Upstream:** keep merging vkdfkc's fork changes, or treat this as a hard fork?
7. **Tools:** debugging Phase 0 may want `lldb` (ships with Xcode — already
   installed?) and the Android work needs the Android SDK/NDK + Gradle. OK to
   install those when we get there (I'll ask first each time)?
