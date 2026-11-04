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
- **SMBX2 core mode boots** (Phase 1, 2026-11-04): with SMBX2's data folder
  installed, each level runs SMBX2's own `main.lua` and libraries unmodified
  on our emulated LunaLua host (`smbx2compat/core/`). On `smoke/bonus1` all of
  `main.lua` and `__onInit` complete (lunabase, characters, NPC AI, the level's
  own `luna.lua`), and per-frame events fire. What's left is runtime gaps,
  logged as `[smbx2core] missing: ...`.

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

Goal: run SMBX2's real `scripts/base/engine/main.lua` unmodified, with our own
versions of only the files that talk to `LunaDll.dll`.
Done when: SMATRS 1-1 runs all its scripts with rendering-heavy calls stubbed, and
the missing pieces are listed in the log.

**Survey (2026-11-04).** `S` = `../TheXTech-data/smbx2/SMBX2 + SMATRS/data/scripts`.

*Host contract.* What LunaLua's C++ side does, which our engine must now do:
1. Create host globals, then run `S/base/engine/main.lua` in `_G` (once per Lua
   state). Needed before/while it runs: `getSMBXPath()` (data root; main.lua
   appends `/scripts/...`), `isOverworld` (bool), and the C++-made tables that Lua
   files only *extend*: `Misc`, `Level`, `DBG`, `CaptureBufferFuncs`, plus
   `Graphics`, `Text`, `Audio`, `SFX`, `console` (`console:println`), `Native*`,
   `Shader`, `CaptureBuffer` (counts of use in `base/engine/`: Graphics 238,
   Misc 187, NPC 140, Block 128, Player 89, getSMBXPath 45, Text 41, Level 31).
2. At level load: `__onInit(episodePath, lvlName)`. episodePath ends with a
   separator; lvlName like `"1-1.lvlx"`. It builds the level folder as
   `episodePath .. lvlName_without_ext .. "\\"` (Windows separators!).
3. Each engine event: `__callEvent(eventObj, args...)`. `eventObj` fields:
   `eventName`, `directEventName` (`""` → `eventName.."Direct"`), `loopable`,
   `cancellable` (+ `cancelled` read back). See `main_events.lua:260`.
4. Each frame: `__doEventQueue()` (also runs classic SMBX events).

*Boot order inside main.lua.* `lockdown.lua` via `dofile` → `require.lua` via
`loadfile` (all later file reads go through `io.readFile`, which lockdown
defines) → JIT options → `main_events` → `constants`, `type`, `ffi_mem`,
`ffi_misc`, `ffi_graphics`, `ffi_player`, `ffi_camera`, `ffi_legacyrng`, then
(levels only) `ffi_npc`, `ffi_bgo`, `ffi_warp`, `ffi_block`, `ffi_liquid`,
`ffi_layer`, `ffi_section`, `ffi_level`, `ffi_events`, `ffi_customparams`.
`__onInit` then loads `defines`, `uservar`, `lunatime`, `rng`, `debugtools`,
`classExpander`, `pAnim`, `color`, `vectr`, `transform`, `sprite`, `colliders`,
`particles`, `routine`, `explosion`, `timer`, `base/game/savedata`,
`checkpoints`; core: `testmodemenu` (only if `mem(0x00B2C62A, FIELD_WORD) == 0`),
`dbg`, `classicevents`, `profiler`; `npcconfig_core`, `blockconfig_core`; then
code files: `base/game/lunabase.lua`, episode `luna.lua`, level `luna.lua`,
`lunadll.lua`, `lunaworld.lua` (maps: `map.lua`, `lunaoverworld.lua`).
So `getSMBXVersionString`, `Darkness`, etc. come for free once this runs.

*Files that call `LunaDll.dll`* (must be replaced): `lockdown`, `ffi_mem`,
`ffi_misc`, `ffi_graphics`, `ffi_player`, `ffi_npc`, `ffi_block`, `ffi_bgo`,
`ffi_layer`, `ffi_legacyrng`, `ffi_customparams` (all in `base/engine/`), plus
`testmodemenu` (skip: test mode only) and `legacy/fade`, `legacy/smb3goalcard`
(legacy, check if SMATRS uses them). `ffi_camera`, `ffi_section`, `ffi_warp`,
`ffi_liquid`, `ffi_level`, `ffi_events`, `ffi_utils` don't load LunaDll; try
them unmodified once `mem()` works. Plain `ffi` (`ffi.new`, as in `ext/lpeglj`)
works in our LuaJIT.

**Plan.**
1. Engine: an "SMBX2 core" mode in `xtech_lua_main.cpp`. When an SMBX2 scripts
   folder is configured (decide: CLI flag / env var / folder in the asset pack),
   define `getSMBXPath`/`isOverworld`, run `main.lua` instead of the v0.1 shim,
   call `__onInit` at level load, `__doEventQueue` from `onLoop`, and turn the
   fork's hooks into `__callEvent` event objects. Keep the shim as the fallback.
2. Redirect: our `lockdown.lua` replacement (main.lua `dofile`s it by path, so
   the host wraps `dofile`, or `getSMBXPath` points at an overlay) implements
   `io.readFile`/`readFileLines`/`exists`/`writeFile` with plain `io`, and maps
   `.../base/engine/<name>.lua` to `smbx2compat/core/<name>.lua` when we have one.
   Normalize paths: backslashes → `/`, and **case-insensitive lookup** (SMBX2 requires
   `classExpander`, `pAnim` but ships `classexpander.lua`, `panim.lua`; macOS
   forgives this, Android's filesystem won't).
3. Replacement files in `smbx2compat/core/`, starting as stubs that log what's
   missing, filled in over `xtech_*` bindings. Host tables (`Misc`, `Level`,
   `Graphics`, `Text`, ...) as Lua over `xtech_*` first.
4. Iterate with `smbx2compat/run-level.sh` on `smoke/bonus1.lvlx`, then
   SMATRS 1-1: run, read the first error, fix, repeat.
5. Reference: LunaLua's C++ source, cloned (read-only, nothing from it is run)
   to `../TheXTech-data/reference/LunaLua` (WohlSoft/LunaLua `eef9a51`, 2025-06):
   - `LunaDll/LuaMain/LunaLuaMain.cpp`: `CLunaLua::init` (line ~178) loads
     `main.lua` and calls `__onInit`; `bindAll()` (~627) registers every C++
     global (`mem`, `Native.getSMBXPath`, `Text`, `Graphics`, `Misc`, classes);
     `isOverworld` (~585); `__doEventQueue` per frame (~1586).
   - `LunaDll/LuaMain/LuaProxyFFI.cpp`, `LuaProxyFFIGraphics.cpp`: the 127
     `LunaLuaGet*`/FFI exports the `ffi_*` files call. Our replacements mirror
     these signatures in meaning, backed by `xtech_*` bindings.
   - `LunaDll/LuaMain/LuaProxy.h/.cpp` + `LuaProxyComponent/`: the C++ side of
     each API; `LunaLuaMain.cpp` event objects for `__callEvent`.


**Progress (2026-11-04).** Steps 1–3 done; step 4 started on `smoke/bonus1`.
- Engine: `xtech_lua_main.cpp` core mode (`findSmbx2Core`, `loadSmbx2Level`).
  It turns on when `<assets>/smbx2/scripts/base/engine/main.lua` and
  `<assets>/scripts/smbx2core/boot.lua` both exist; `SMBX2COMPAT_MODE=shim`
  forces the old shim. Like LunaLua, every level gets a **fresh Lua state**.
  New bindings: `xtech_misc_resolveFileCI`, `xtech_mem_get/set` (mememu
  globals), `xtech_misc_clock`, `xtech_misc_imageSize`, `xtech_misc_imagePixels`,
  `xtech_misc_listDir`, `xtech_misc_set/getGameData`.
- `core/boot.lua`: host globals, one file resolver (backslashes, case-insensitive
  names via `DirListCI`, `override/` files, `patches.lua` source edits), runs
  `main.lua`, maps engine frames to `onStart`/`onLoop`/`onInputUpdate`/`onTick`/
  `onTickEnd`/`onDraw`/`onDrawEnd`/`onExitLevel`/`onExit`.
- **Changed from the plan:** instead of rewriting the `ffi_*` files, `ffi.load(
  "LunaDll.dll")` returns `core/lunadll.lua`, a Lua table of the DLL's exports, so
  SMBX2's own `ffi_*` files run as-is. Only `lockdown.lua` and `ffi_mem.lua` are
  replaced (`core/override/base/engine/`). C++-side host tables are `core/host.lua`,
  C++ classes (`Animation`, ...) `core/classes.lua`, constants `core/constants.lua`.
- **Virtual `C:` drive:** SMBX2 tests "absolute path?" with a drive-letter match
  in many files, so script-visible paths are `C:/Users/...` and the resolver drops
  the `C:`. No per-file patches needed for that.
- **LuaJIT in Lua 5.2 mode:** SMBX2 code needs it (empty statements, e.g.
  `lunatime.lua:57`), so LunaLua's LuaJIT must have `LUAJIT_ENABLE_LUA52COMPAT`.
  Ours now does too (`cmake/library_luajit.cmake` + `smbx2compat/luajit.patch`).
- Extracted `data/sound`, `data/music`, `data/costumes` from the bundle too
  (scripts load sounds from there).

*Still failing on bonus1 (each logged once):* `LunaLuaGetNpcCustomParams` and the
other custom-params exports; `editorevents` (`beatTimer`); `audiomaster` (sound
objects); `Player` fields (`_ptr` nil: object memory); drawing (`FFI_TextDraw`,
`Graphics.drawImage`). Object memory reads/writes return 0 (Phase 2), and the NPC
and block property tables (`npc-n.txt` config) point at address 0 until then.

*Done in the next round (2026-11-04):*
- **Custom params.** Blocks, BGOs and NPCs keep their level-file JSON in a new
  `CustomParams` field (an index into `g_objectCustomParams`, so it survives
  TheXTech's post-load sorting); level and section params are kept too.
  `xtech_misc_customParams` hands them to `core/extrasettings.lua`, a port of
  LunaLua's config-pack defaults merge (`config_manager.cpp`), reading
  `PGE/configs/SMBX2-Integration` (its .ini/.json files were extracted from the
  bundle) and `scripts/extra-settings`.
- **SMATRS 1-1 boots in core mode** and runs its episode scripts; it first
  segfaulted on `LunaLuaAlloc` returning nothing (now LuaJIT buffers kept
  for a frame). `run-level.sh` force-kills a game stuck in its crash dialog.
- **Phase 2 started: object memory.** `xtech_mem_get/set` answer the pointer
  globals for players (`0x00B25A20`), NPCs (`0x00B259E8`), blocks (`0x00B25A04`)
  and BGOs (`0x00B259B0`) with made-up base addresses (`0x10000000`... in
  `xtech_lua_bindings.cpp`) and map `base + idx*size + offset` to the object
  through mememu. New in mememu: `BlockMemory`, `BackgroundMemory`, NPC `0x64`
  (generator) and globals `qScreen`/`selSave`. Character templates
  (`Player(1000+id)`) are our own `Player_t` copies. MemEmu log warnings went
  from ~34,000 to ~100 per 15 s run.

*Where bonus1 and SMATRS 1-1 stand:* both run without crashing; the two
remaining script errors are `PlayerSettings` (a LunaLua class: per-character
hitboxes, grab and sprite offsets) and `darkness.lua` reading NPC/BGO config.
1-1 also has 863 objects with SMBX2-only IDs, which the engine resets to ID 1
on load (Phase 3).

*Done in the third round (2026-11-04), all in `xtech_lua_bindings.cpp` unless noted:*
- **Type arrays** (`s_memTypeArrays`): SMBX's per-type arrays behind pointer
  globals: the 40 NPC config arrays (`NPCWidth` ... `NPCFrameStyle`, onto
  `NPCTraits`), the block ones (`BlockIsSizable`, slopes, sizes, frames, lava,
  ...), `BackgroundFence`, camera X/Y, section tables (bounds, music,
  background, wrap, underwater, ...), `iBlock`, the event queue delays, tile/
  scene/level frames. LunaLua's extended NPC/block properties
  (`Misc.__getNPCPropertyTableAddress`) are plain storage. IDs past the engine's
  limits (SMBX2 goes to 1000 NPCs / 2000 blocks / 1000 BGOs) are storage too, so
  `npc-N.txt` values for SMBX2's own NPCs read back.
- **`PlayerSettings`** (`core/classes.lua`) over `Physics.PlayerWidth/Height/
  DuckHeight/GrabSpotX/Y` and `PlayerFrameX/Y`. This was what stopped
  `lunabase.lua` at line 6, and with it `bgoconfig` (the `darkness.lua` error).
- **Struct arrays without a mememu emulator** (`MemField` tables): layers,
  events (SMBX2's `ffi_events.lua` layout, stride 0x588, which is 8 bytes off
  LunaLua's `Types.h`), warps, water, cameras (`vScreen`), the event queue
  (`NewEvent`) and section music paths.
- **`mem()` strings**: `xtech_mem_getString/setString`. Object fields map
  layer/event names onto TheXTech's indexes (`FindLayer`/`FindEvent` + the
  `syncLayers_*` call); globals come from mememu (`GetMemString`). A string
  field read as a DWORD gives 0 when empty, which `ffi_layer.lua` relies on.
- mememu: `LevelMacro` (win state), `ScreenShake` (writes shake the screen),
  `ClearBuffer`, `selWorld`, `GoToLevel`, `iBlocks`, checkpoint and file-name
  strings; `0xB2D710` is now `newEventNum` (it was mapped to `numEvents`, which
  SMBX doesn't store); NPC `Special`..`Special5` take doubles without warnings.
- **Block queries through the quadtree.** SMBX's block lookup
  (`FirstBlock`/`LastBlock`) has no TheXTech equivalent, so it spans all blocks;
  `Block.iterateIntersecting`/`getIntersecting` would then scan all ~14,000 of
  1-1's blocks per call (~5 calls a frame, ~20 ms/frame in Debug). `patches.lua`
  points them at `xtech_block_queryIntersecting` (the engine's block quadtree +
  SMBX2's own overlap test): ~0.03 ms per camera-sized query, identical results.
- Compromises: the sizable list is rebuilt from the engine when its length is
  read; NPC traits TheXTech derives from the ID (`isyoshi`, `isshoe`, ...) are
  read-only; water `Buoy` reads 0.

*Where bonus1 and SMATRS 1-1 stand now:* no unknown `mem()` addresses; ~15 MemEmu
lines per run (episode list `0xB250FC` and one-time float writes to physics
globals). Errors, each logged once:
- `editorevents.lua:495` `FileFormats` (LunaLua namespace) and, after it,
  `editorevents.lua:702` `beatTimer`;
- `ffi_player.lua:252` `ffiGroup`: collision groups
  (`LunaLuaCollisionMatrixGetGroupFromIndex`);
- SMATRS: `graphicalQuality.lua:34` (`section.darkness` nil),
  `extraBGOProperties.lua:314` (per-section BGO list nil),
  `ambientSound.lua:91` (audio sources), `debugtools.lua:136`;
- bonus1: `ffi_graphics.lua:1598` (`img` type), `npc-21.lua:42`.

*Performance round 1 is done* (Performance track section below): SMATRS 1-1
now takes ~3.8 ms a frame in Release on an M3 Pro, with no JIT flush storms.
The remaining performance candidates are smaller, so next is this list again.

*Next round, in order:*
1. **`FileFormats`** (LunaLua's level/world file reader for Lua; TheXTech has
   PGE built in) and the collision matrix exports.
2. **SMATRS's section darkness and BGO lists**: find why `section.darkness`
   is never made (the darkness/section-settings `onStart` chain).
3. **Drawing** (Phase 4: `FFI_ImageDraw`, `FFI_TextDraw`, `FFI_GLDraw`) and
   **audio** (Phase 5); then Phase 3 IDs.
Load time is ~2.5 s in the Debug build (see the Performance track).

### Phase 2 — `mem()` and object classes (started 2026-11-04)
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

## Performance track (started 2026-11-04)

Jake's main worry is performance: SMATRS runs ~270k lines of SMBX2 Lua on an
emulated core, and the target is an Android handheld (AYN Odin 3). This track
runs alongside the phases. It's meant to start in a fresh session, so it
repeats what that session needs.

**Rules.** Measure before and after every change, on the same level and frame
range. Record the numbers here. Results must stay identical to SMBX2's: check
each replacement against SMBX2's own algorithm, as done for block queries.
Prefer engine-side answers (TheXTech's quadtrees, its index maps) over
re-creating SMBX 1.3's data structures.

**Measuring.** Build Release in its own folder and run it with the probe:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DTHEXTECH_ENABLE_LUA=ON
cmake --build build-release
SMBX2COMPAT_PROFILE=100-400,nosample XTECH_BUILD=build-release smbx2compat/run-level.sh "<level>" 14
```

`core/profile.lua` (the probe; its header lists everything it logs) reports
frame time (the engine's whole frame, with vsync off), Lua time per event and
per library listener, `mem()` calls, the 5 slowest frames, JIT trace events
and flushes, and, without `nosample`, LuaJIT's sampling profiler. Frames
100-400 of 1-1 vary by about +-0.3 ms between runs; compare 3+ runs. The
sampler's function/line attribution is rough (see the probe's header); its
VM-state split (compiled / interpreted / C / GC / JIT compiler) is reliable.

**Done so far** (Release, M3 Pro, SMATRS 1-1 frames 100-400, ms per frame):

| Step | Frame avg | Lua avg | Notes |
|---|---|---|---|
| Debug, before this track | ~9.4 | ~7.6 | the first probe run |
| Release baseline | 3.7-6.4 | 2.5-5.2 | 0 to 116 JIT flushes per run |
| + one 8 MB JIT code area | 3.7-5.1 | 2.6-3.9 | no flushes after loading |
| + `mem()` reads through the FFI | 3.8-3.9 | 2.7-2.8 | 17,400 reads a frame |

bonus1: ~3.4 ms (Lua ~1.8). The engine's own part is ~1 ms.

1. **Block queries** through the block quadtree (`xtech_block_queryIntersecting`,
   patched in `core/patches.lua`): ~20 ms to ~0.15 ms a frame in Debug. A
   camera-sized query is ~0.03 ms and matches a full scan exactly.
2. **JIT flush storms.** LuaJIT keeps compiled code in 64 KB areas, and on
   arm64 every area must sit within branch range (+-64 MB) of the others. As
   the engine loads graphics, that space fills; a new area can't be placed
   (`MCODEAL`), and LuaJIT throws away *all* compiled code and recompiles. In
   1-1 that ranged from never to every 2-3 frames, at random (it depends on
   where memory lands), costing up to ~2.7 ms a frame in the compiler.
   Found with the probe's trace events plus an lldb backtrace on
   `lj_trace_flushall` (the call site follows `cmp w20, #0x1b`, 27 =
   `MCODEAL`). Fix: `boot.lua` sets `sizemcode=8192` (one 8 MB area, equal
   to SMBX2's `maxmcode=8192`) before anything compiles. SMBX2 never sees
   this: it runs 32-bit. What's left is SMBX2's own `maxtrace=5000` limit: one
   flush right after loading (`__onInit` compiles thousands of traces), and
   another every few thousand frames, each costing a frame or two of ~10 ms.
3. **`mem()` reads through the FFI.** `ffi_mem.lua` calls
   `LuaMisc::ffi_mem_get` through a C function pointer (`__xtech_ffiMemGet`,
   set in `xtech_lua_register_bindings`), which LuaJIT compiles into traces;
   no symbol export needed, so it works the same on Android. `mem_get` also
   skips its ~130 pointer-global compares for addresses in the made-up array
   ranges. One read: 50 ns -> 6 ns; `player.x`: 137 -> 50 ns (the rest is
   SMBX2's class lookup). Checked: 27,794 reads across all arrays and globals
   gave identical results both ways. Writes (~90 a frame) stay on luabind:
   some reach engine code, and an FFI-called function must never call back
   into Lua.

**Open:** a rare single frame of ~60 ms (seen in about 1 run in 5, before and
after these changes; not caught yet). The probe's "slowest" line shows the
events and heap change of such a frame; a large GC step is the main suspect
(SMBX2 runs the GC by hand in `onDrawEnd`, `main_events.lua`).

**Candidates, roughly in order of expected payoff (measure first):**
1. **SMBX2's class lookup** around `mem()` (`player.x` is ~50 ns, of which
   the read is 6). The sampler shows `ffi_player.lua` `__index`/`get` and
   `classValidityChecker` near the top. Patch carefully: these are SMBX2's
   semantics.
2. **The other full scans in SMBX2's `ffi_*.lua`.** Each is O(objects) `mem()`
   calls per call:
   - `NPC.iterateIntersecting`/`getIntersecting` (`ffi_npc.lua:577`) — the
     NPC tree;
   - `BGO.getIntersecting`/`iterateIntersecting` (`ffi_bgo.lua:217/345`; 1-1
     has ~1,090 BGOs) — `treeBackgroundQuery`;
   - `Block.get(id)`/`NPC.get(id)` reading the type of every object;
   - `Events.get(name)` and `Layer.get(name)` reading every name as a string.
     `ffi_events` runs `Events.get` on every triggered event
     (`onPostEventDirect`). Use the engine's `FindEvent`/`FindLayer`.

   Patch these the way `patches.lua` patches the block queries, keeping
   SMBX2's ordering and loop keys. Check the NPC index mapping (SMBX2's
   `NPC(idx)` is 0-based).
3. **Allocation.** String reads build a new `std::string` and Lua string on
   every call. `PlayerSettings`/`Animation` objects and the iterator closures
   are new tables each call. The sizable list (`memSizableBlockCount`) is
   rebuilt and sorted on every read of its length; cache it per frame
   (`core.onFrameEnd`).
4. **Logging in hot paths.** mememu's `MemEmu:` warnings aren't capped (a
   per-frame one once logged 29,000 lines). Cap or rate-limit them like
   `mem_unknown`.
5. **Load time.**
   - `npcconfig.loadAllTxt` probes `npc-1..1000.txt` (and blocks to 2000)
     through `core.resolve`'s case-insensitive lookups. Cache directory
     listings.
   - `extrasettings.lua` parses layout JSON on first use per ID.
6. **Android specifics** (with Phase 7). Confirm LuaJIT's JIT is actually on
   for arm64 Android (the probe's first line says `jit on`/`OFF`). LuaJIT
   disables itself if the very first code area can't be allocated. The
   one-area fix above matters there too (same arm64 branch range). Then
   measure on the Odin 3 itself.

## Milestones

1. M0 — `onLoop` errors no longer crash (Phase 0) — reached 2026-11-04
2. M1 — all SMATRS scripts load, stubs logged (Phase 1) — bonus1 boots SMBX2's
   full library set as of 2026-11-04; SMATRS 1-1 not tried yet
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
8. **LunaLua source:** approved cloning it as a read-only reference (done, see
   Phase 1 plan item 5).
