# Roadmap: SMATRS on TheXTech-Lua (desktop, then Android)

Status as of 2026-11-05. Goal: play *Super Mario and the Rainbow Stars* (SMATRS,
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
| Engine limits | SMBX2's since 2026-11-04: 1000 NPC, 2000 block, 1000 BGO and effect types (`src/global_constants.h`; were 306 / 702 / 200 / 200) |

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

*FileFormats round (2026-11-04).* SMBX2 runs every library's `onStart` in one
loop (`main_events.lua`), so one error there skips every handler after it.
That, not missing data, is why SMATRS never made `section.darkness` or its
per-section BGO lists: `editorevents.lua`'s `onStart` failed first. Fixed, in
the order they surfaced:
- **`FileFormats`**: `script/src/xtech_lua_fileformats.cpp` reads level,
  world and `npc-N.txt` files with PGE into tables with LunaLua's field names
  (`LuaProxyFileFormats.cpp`); `core/host.lua` resolves paths, decodes custom
  params with SMBX2's JSON library and adds the enums. `getLevelData()`
  rereads the current level file (LunaLua keeps it in memory). Our PGE names
  physics-zone types 6/7 and 13/14 differently; the enums use LunaLua's names.
- **Collision groups** (`core/lunadll.lua`): LunaLua's `CollisionMatrix` plus
  the newer name layer SMBX2 expects (`...AssignGroup`, `...GetGroupFromIndex`,
  `...GetGroupsCollide`, ...). Only scripts see it; the engine's own
  collisions don't consult it yet.
- **Shaders** compile in the engine since the shader round (below). One that
  doesn't compile here still reports success, with uniforms/attributes read
  from its GLSL declarations (`darkness.lua` errors without a uniform list),
  and is drawn without.
- **`inputConfig1`/`inputConfig2`** (`core/classes.lua`): keyboard, SMBX's
  default keys. TODO: from TheXTech's control profiles.
- **`NPC.spawn`**: SMBX2 builds it on the C++ class it finds as `NPC`
  (`LegacyNPC.spawn`); `core/classes.lua` provides that, backed by
  `xtech_npc_spawnLegacy` (a blank NPC, as LunaLua's). IDs past 306 spawn as 1,
  like the level loader.
- **`triggerEvent(name)`**: LunaLua's legacy global for level events
  (`xtech_event_triggerLevelEvent`, queued with the engine's `TriggerEvent`).
  The v0.1 shim's `triggerEvent` uses autocode events instead.
- **Layers**: `LunaLuaShowLayer`/`LunaLuaHideLayer` (`xtech_layer_show/hide`).
- Writes to `<SMBX>/logs/` (debugtools' warning list) go to TheXTech's log
  folder (`__xtech_logsDir`); TheXTech prunes old logs there.

*Audio round (2026-11-04).* TheXTech plays sound through MixerX, the same
mixer library LunaLua uses, so LunaLua's `Audio` namespace
(`LuaProxyAudio.cpp`) maps onto it almost call for call:
- **`script/src/xtech_lua_audio.cpp`** registers it as `xtech_lunaAudio`
  (plain Lua C API); `core/host.lua` builds `Audio` from it and only resolves
  file paths (LunaLua's `getSfxPath` order, cached per path).
- **Sounds**: `SfxOpen` returns a `Mix_Chunk` userdata with `allocated`,
  `alen` and `volume`, which `audiomaster.lua` checks before playing
  anything. Chunks are cached by path and freed with the Lua state. With
  sound off there is no mixer to load into, so a silent chunk stands in.
- **`SFX.play` objects**: `SfxPlayObj*` return a `PlayingSfxInstance`
  (`SetPanning`, `Volume`, `IsPlaying`, `Stop`, ...). The engine counts
  finished channels (`SoundScript_ChannelFinishCount`), so a handle stops
  controlling its channel once the channel goes to another sound.
- **Music**: scripts share the engine's music stream, as in LunaLua.
  `MusicOpen`/`MusicPlay`/position/speed/tempo/tags/loop points work on
  `g_curMusic`; `SeizeStream` makes the engine leave a section's music alone
  (`StartMusic`/`StopMusic`/`FadeOutMusic` return early); `MusicVolume`
  overrides the engine's levels (the player's music volume setting still
  scales it). Level unload releases seizes and overrides (`SoundScript_Reset`).
- **`Audio.sounds[A].sfx` / `.muted`**: per-sound overrides and mutes in
  `PlaySfx_Blocking`, kept in atomics (it runs on the SFX thread).
- **`MusicChange`**: sets the section's music and restarts it if player 1 is
  there; absolute custom-music paths now work in `StartMusic`.
- **Episode `sounds.ini`**: SMBX2 episodes leave extensions off and name
  stock sounds by file name alone (`file="player-slide"`). TheXTech loaded
  none of SMATRS's 91 sounds; it now tries LunaLua's extensions and then its
  own sound folder (`resolveSfxFile` in `sound.cpp`).
- **Type writes**: a `mem()` write giving a block, NPC or BGO a type past the
  engine's limits (SMATRS turns a block into 756) now writes 1, as level
  loading does, logged once per ID. It crashed on a range check (Jake hit
  it too). The sizable-block scan behind `sBlockNum` checks the range as well.
- Testing: `XTECH_AUDIO=dummy smbx2compat/run-level.sh ...` keeps sound on but
  sends it to SDL's silent dummy device, so the mixer paths run quietly. A
  probe showed SMATRS's `music.lua` opening `Ocean.ogg`, its position
  advancing in real time and its volume changing; a sound object reported
  playing, then finished.

*Drawing round (2026-11-04).* SMBX2's draws now reach the screen, between the
engine's objects where SMBX2 puts them:
- **Draw queue** (`script/src/xtech_lua_draw.cpp`, header
  `script/include/xtech_lua_draw.h`): `FFI_GLDraw`, `FFI_ImageDraw`,
  `FFI_TextDraw` and `LunaLuaAlloc` are C functions that `core/lunadll.lua`
  calls through the FFI (`__xtech_ffi*` pointers). Each draw is queued with
  its priority and the camera being drawn (LunaLua's `Renderer`), and cleared
  after each rendered frame. `glDraw` vertex arrays are malloc'd and freed by
  the queue, as in LunaLua; strips and fans become triangle lists; SMBX2's
  premultiplied vertex colours become straight alpha (the engine's blending).
- **Priorities**: LunaLua draws everything below priority p where SMBX's
  renderer reaches p (`RuntimeHookGeneral.cpp`: -95 back BGOs, -90 sizables,
  ... -25 players, ..., 5 HUD). `gfx_update.cpp` calls `SMBX2_DRAW_BELOW` right
  after switching to the matching draw plane, so it works with any draw order.
- **Events**: `onCameraUpdate` (every camera), `onDraw`, then per camera
  `onCameraDraw` and `onHUDUpdate`/`onHUDDraw`, in LunaLua's order. These were
  never sent before, which is why backgrounds, BGOs and sizables (drawn from
  `onCameraDraw`) were missing.
- **Take-overs**: SMBX2's `paralx2`, `betterbgo` and `sizable` switch the
  engine's backgrounds, BGOs and sizable blocks off and draw them in Lua; the
  render flags now do that (`xtech_gfx_setVanilla`), as does
  `FFI_GraphicsActivateHud` (TheXTech's `gSMBXHUDSettings.skip`).
- **Images** are engine pictures (`xtech_gfx_imageLoad`), shared per file and
  freed with the level's Lua state.
- **Renderer**: `XRender::renderTriangles` (textured triangles with per-vertex
  colour); only the OpenGL renderer implements it (no-op elsewhere).
- **Screen size**: SMATRS asks for a 960x540 framebuffer
  (`FFI_GraphicsSetMainFramebufferSize`); it now sets the engine's internal
  resolution at the script config level, which the next level's config
  rebuild clears. Before, 1-1 played in the corner of a 1280x720 screen.
- **Testing**: `SMBX2COMPAT_SHOTS=300,900` saves those frames as
  `smbx2shot-N.png` in the log folder (macOS keeps the engine's own
  screenshot folder out of reach of a shell).
Not drawn after this round: sprite overrides (`Graphics.sprites.block[id].img
= ...`; SMATRS's camera zoom and block 760 use them). Shaders, capture
buffers, points and lines came in the shader round, sprite overrides in the
sprite override round.

*Shader round (2026-11-04).* SMBX2's shaders and capture buffers work:
- **Translation** (`core/glsl.lua`): SMBX2 writes GLSL 1.20 for LunaLua's
  OpenGL 2.1 compatibility context, using fixed-function built-ins the
  engine's context lacks (macOS: OpenGL 4.1 core; Android: ES). They become
  `xt_*` attributes/uniforms the engine sets (`gl_Vertex` -> `xt_Vertex`,
  `gl_TexCoord[0]` -> `xt_TexCoord0`, `gl_ModelViewProjectionMatrix` ->
  `xt_MVP`, ...), plus `attribute`/`varying` -> `in`/`out`, `texture2D` ->
  `texture`, `gl_FragColor` -> an output. Names later GLSL reserves get an
  `xt_r_` prefix (SMATRS's backgrounds have a uniform called `textureSize`);
  lunadll.lua reports them by their own names. The engine picks the dialect
  (`xtech_gfx_shaderHeader`): `#version 330` on desktop, `300 es` or `100` on
  ES. Every shader SMATRS 1-1 uses compiles on the Mac.
- **Variables** come from GL (`glGetActiveUniform`/`Attrib`), shaped like
  LunaLua's `GLShaderVariableInfo` (id = location, `name[0]` for arrays).
- **Script draws** (`src/core/render_script.h`, `render_gl_script.cpp`): a
  draw with a shader, into or from a capture buffer, or of points/lines runs
  as its own GL draw, like LunaLua's `GLEngineCmd_LuaDraw`: the engine's
  queued draws are drawn first, then this one, with LunaLua's premultiplied
  blending and no depth test. Extra samplers (`silhouetteImage`, a capture
  buffer as a uniform) get texture units 6+ (the engine's use 0-5). Draws
  without a shader that need this path use SMBX2's `standard.vert/frag`.
  Everything else still joins the engine's batches.
- **Premultiplied alpha**: SMBX2's images load premultiplied
  (`StdPictureLoad::premultiply`), as LunaLua's do, so shaders see what they
  expect. Batched draws of them use a program that converts back to the
  engine's straight alpha (`m_program_premultiplied`).
- **Capture buffers** are framebuffers with top-down rows like the game
  buffer, so `gl_FragCoord` and texture coordinates match LunaLua.
  `captureAt` copies the camera's view at its priority, `clear` clears at
  its priority, `glDraw{target = ...}` draws into one. A buffer stays bound
  across consecutive draws into it.
- 1-1 now shows the shaded sunset sky, sand and water layers, Mario in his
  palette colours, the foreground water and the music-name box text.
- Not done: `Graphics.redirectCameraFB` (done in the camera redirect
  round, 2026-11-05), depth buffers as samplers, clip planes, double-precision
  uniforms. On ES, SMBX2's shaders will also need int -> float conversions
  added (desktop GLSL converts implicitly, ES doesn't): Phase 7.

*Where bonus1 and SMATRS 1-1 stand now:* no script errors in SMATRS 1-1 (sound
on, dummy or off); bonus1 has its two old ones, `ffi_graphics.lua:1598`
(`img` type) and `npc-21.lua:42`, plus `HUDOverride.lua:733`, which follows
from the first (bonus1's own `onDraw` error skips HUDOverride's `onDraw`, as
it would in LunaLua). No unknown `mem()` addresses. 1-1 draws its HUD,
shaded parallax backgrounds, BGOs, sloped ground, player palettes, screen
effects and music box at 960x540.
Other logged gaps: window (`LunaLuaSetWindowTitle`/`Icon`, `IsFullscreen`),
`LunaLuaBlocksTestCollision`, `Misc.loadCharacterHitBoxes`/
`registerCharacterId`, rumble, controller power level.
Checked by ear 2026-11-04 (Jake): SMATRS 1-1's sound works.

*Sprite override round (2026-11-04).* `Graphics.sprites.<category>[id].img =
image` now changes what the engine draws, as LunaLua's
`ImageLoader::OverrideByName` does:
- `SpriteOverride_Set` (`src/load_gfx.cpp`) works like the per-level custom
  graphics loader: the slot's texture (`GFXBlockBMP[id]`, `GFXNPCBMP`,
  `GFXEffectBMP`, `GFXBackgroundBMP`, `GFXBackground2BMP`, Yoshi and the five
  characters' power-up sheets) moves into a backup, and the override's file
  loads in its place. `nil` restores it; `ClearLevel` -> `UnloadCustomGFX`
  restores every one before the engine's own custom graphics unwind. Effects
  also get their frame size from the new image, as `loadCGFX` computes it.
- The engine loads its own copy of the file, straight alpha like its other
  sprites, so the premultiplied copy scripts draw with stays as it is.
- lunadll.lua's `FFI_SetSpriteOverride` splits `block-760` into category and
  ID. Overrides of IDs past the engine's limits (Phase 3) and of categories
  the engine has no sprite for (`hardcoded-*`) are kept for scripts and
  logged once.
- In 1-1: `animationPal` hides the engine's Mario and Peach sheets (it draws
  the player itself, so before this the engine drew a second Mario under
  it); `customCamera` hides its camera-zone block (694). Block 760's lock
  image and the badge effect's flipped image use the same path.
- Also: `player:harm()` / `player:kill()` (`Misc._playerHarm`/`_playerKill`)
  call the engine's `PlayerHurt`/`KillPlayer`, as LunaLua calls SMBX's; they
  were stubs, so SMATRS's custom enemies couldn't hurt Mario.
- Found while testing: `Level.exit()` does nothing yet. It writes SMBX's
  mode flags (`0xB2C5B4` episode mode, `0xB2C620` level mode, `0xB2C5D4` win
  type), which `mem()` doesn't emulate (logged as `MemEmu: Unknown ...`).
  Other `Misc` stubs that change the game: `npcToCoins`, `doPOW`,
  `doPSwitch`/`doPSwitchRaw`, `doBombExplosion`, `_npcHarmCombo*`,
  `_npcCollect`.

*ID and collision group round (2026-11-04)* (Phase 3's first half):
- The engine takes SMBX2's limits (LunaLua's `MAX_ID`s): 2000 block types,
  1000 NPC, BGO and effect types (`src/global_constants.h`). SMATRS's
  levels use up to block 1383, NPC 999 and BGO 923, and its effects go to
  975. Nothing in a level or a `mem()` write becomes ID 1 any more, and
  `NPC.spawn` takes LunaLua's 1-1000.
- Three ranges clashed: TheXTech's own NPCs 301-306 (power-ups and the flag
  exit; SMBX2's are Thwimp, Blurp, Torpedo Ted, ...), its conveyor blocks
  701/702 and its generic NPC death effects 149/150 (SMBX2's engine effects
  go to 161). They moved to 1001-1006, 2001/2002 and 1001/1002. Only the
  NPCs appear in files: `src/main/smbx2_ids.h` maps 301-306 to 1001-1006 in
  levels, the editor's level writer and saves when SMBX2's data isn't
  installed, and their images and `npc-N.txt` keep the 301-306 names.
- With SMBX2's data installed, default block/NPC/BGO/effect/background2
  sprites come from SMBX2's `graphics/` folders first (it has its own
  versions of 44 NPCs, 198 blocks and 22 effects that the asset pack also
  has), then the asset pack. Character and Yoshi sheets still come from
  the asset pack (SMBX2's go with its own hitbox files).
- Collision groups reach the engine. LunaLua's extended fields
  (`noblockcollision`, `nonpccollision`, `collisionGroup`; players'
  `nonpcinteraction`, `noplayerinteraction`) now live in the engine:
  inside `NPC_t`/`Block_t`, so they move when the engine moves an object,
  and in an array for players. The arrays SMBX2's `ffi_*.lua` index are
  structs as wide as a whole engine object (built from
  `__xtech_extendedFields`). `src/main/smbx2_collision.h` has the checks,
  placed where LunaLua hooks SMBX: NPC-NPC, held NPC-NPC, NPC-block (an NPC's
  stand-in block uses the NPC's group), player-block, player-NPC and
  player-player. lunadll.lua keeps the group names and copies each
  `Misc.groupsCollide` flip into the engine. `smoke/collisiontest.lvlx`
  checks a group falling through, no group landing, and a flipped pair.
- mememu: NPC `0xE8`/`0xEC` (animation timer and direction, Floats in SMBX)
  take floats without a warning; SMBX2's NPC scripts now run for real IDs,
  and those warnings had reached ~16,000 per 30 s. Block `0x52` (ShakeY)
  writes start the engine's bump (up or down, 6 or 12 px), as SMBX2's
  `Block:hit()` expects; `0x0A`/`0x54`/`0x64` writes from `Block.spawn` are
  accepted quietly.
- Not done: LunaLua's `noblockcollision` also turns off water physics and
  the "killed by a bumped block" check; only the block collisions honour
  it. Group reference counts don't drop when the engine removes an NPC, so
  an unused group name keeps its index until the level ends (harmless).

*Level flow and game actions round (2026-11-04)*:
- `Level.exit()` works. SMBX2 writes SMBX's globals: `0xB2C5D4` is
  `LevelBeatCode` (the win type a level exit unlocks paths with),
  `0xB2C5B4` is `LevelSelect` and `0xB2C620` `GameMenu`. In SMBX, setting
  `LevelSelect` stopped the level's frame loop; here, setting it in a level
  sets `EndLevel` instead, so the engine leaves through its usual level end
  (checkpoints, saves, `onExit`, clearing the level and its Lua state).
  `0xB2C59C` (`EndLevel`) is `Level.finish(0)`. The engine's end
  macros take `LevelMacroCounter = 1000` (`Level.finish` hurrying one).
- SMBX2's end states past SMBX's 7 (switch palace, flagpole, SMW orb) are
  run by its `endstates.lua`; TheXTech's own macro 8 is its flag exit.
  Those values are stored in `LevelMacro` past 1000, where the engine runs
  no macro, and read back as written.
- `Misc` game actions call the engine (`xtech_lua_bindings.cpp`, `LuaMisc`):
  - `npc:harm(type[, damage])` (`_npcHarmCombo*`): `NPCHit` with the dummy
    NPC 0 as the other side, as LunaLua does; the fire/ice/hammer harm
    types set the dummy's type. A damage argument replaces the damage of
    every hit (`g_npcHitDamage` in `npc_hit.cpp`, where LunaLua patches
    SMBX's 32 damage constants). Before this, `npc:harm()` did nothing.
  - `npc:collect(player)`: `TouchBonus`.
  - `Misc.npcToCoins()`: LunaLua's version, which asks SMBX2's
    `notcointransformable` property instead of the engine's list of IDs
    (SMATRS's star system calls it).
  - `Misc.doPOW()`: `PowBlock`. `Misc.doBombExplosion`: `Bomb` (SMBX2
    replaces it with its own `explosion.spawn` anyway).
  - `Misc.doPSwitchRaw` (SMATRS's respawn rooms): the engine's switch can
    stop the frame to run level events, so the request waits for
    `UpdateBlocks` and runs there with its own resume point
    (`PSwitchScript` in `blocks.cpp`). `Misc.doPSwitch` also sets the timer
    and music.
- Fixed: turning bricks back into coins crashed (`Layer[255]` assert) when
  the coin had no layer, which is normal for NPCs spawned by scripts in
  LunaLua; `blocks.cpp` now uses no layer offset for them.
- mememu: 32-bit reads of true/false globals are fine (SMBX2's default HUD
  reads `0xB2C89C` that way every frame; 900+ warnings per run).
- `smoke/actiontest.lvlx` checks each action and ends with `Level.exit()`.
- Not done: `NPC:toCoin()` / `toIce()` call LunaLua's C++ NPC class
  (`LegacyNPC(idx)`), which classes.lua doesn't have; `onNPCCollect` /
  `onNPCHarm` aren't sent from the engine yet (Phase 5).

*Camera redirect round (2026-11-05)*:
- `Graphics.redirectCameraFB(buffer, start, end)`: everything drawn to the
  camera from `start` to `end`, the engine's blocks, NPCs and players too,
  goes into the capture buffer (LunaLua's `GLContextManager` redirect stack).
  The draw queue gets `OP_REDIRECT`/`OP_UNREDIRECT` ops at the two
  priorities, nesting like LunaLua's stack; redirects end with each camera.
  `RenderGL::scriptRedirect` puts the capture buffer in `BUFFER_GAME`'s slot
  (framebuffer and texture), so the engine's own extra passes that rebind
  "the game buffer" (screen copies, lighting, multipass) follow it; a first
  version that only bound the buffer lost every draw after the first such
  pass. The buffer gets its own depth buffer (cleared per redirect) for the
  engine's opaque draws. The camera sits at the buffer's top-left, unscaled,
  as in LunaLua, so it matches the engine's coordinates for one unscaled
  camera only (split screen would need more).
- SMATRS's glowing moon (2-1 Warped Woods, `dynamicBackgrounds.lua`) now
  shows above the level's darkness.
- TheXTech reads SMBX2's section `darkness` settings for its own lighting;
  with SMBX2's `darkness.lua` also running, levels with darkness (1-1, 2-1)
  were darkened twice. The engine's lighting is off in core mode now.
- `playMusic(section)` (a LunaLua global; `Player:teleport` calls it) was
  missing; 2-1's start-up stopped at it.
- Not checked: 2-3, 2-5 and the badge screen, the other redirect users
  (they need playing to reach). Not done: clip planes.

*Performance round (2026-11-05)*: 1-1 went from ~6.6 to ~4.5 ms a frame
(Lua ~4.5 to ~2.5); details in the Performance track, steps 4-10. No
change in what scripts see, apart from the sizable list being rebuilt once a
frame instead of on every read.

*Next round, in order:*
1. Box light NPC 675 in 2-1: `data._settings` without `sourceSize`, though
   the level's settings for it are complete (settings reaching the wrong NPC?).
2. `smoke/actiontest`: the mushroom check (powerup at tick 180) is flaky, not
   broken: the same build passes and fails on back-to-back runs, the player
   still mid grow-animation (`forcedState=1`) both times. Make the check wait
   for the animation, or find what makes its timing vary.
3. Cancellable engine events (`onNPCHarm`, `onNPCCollect`, ...; Phase 5).
4. More speed, if needed: the Performance track's candidates (struct views
   for SMBX2's field reads, luabind writes).
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
  Done 2026-11-04 (Phase 1, "ID and collision group round").
- Load SMBX2-style per-ID config (`npc-N.txt`, `npc-N.ini`) and graphics for IDs
  with no built-in definition. Graphics done; SMBX2's own configs come from
  its `npc-N.lua`/`block-N.lua` (`npcManager.setNpcSettings`) through
  `mem()`, which now reaches the engine's traits for every ID.
- Let Lua own NPC AI for custom IDs (`npcManager` behaviour); engine physics/AI
  runs only when the config asks for it.
- Done when: custom NPCs in 1-1 appear and move.

### Phase 4 — Rendering
- `Graphics.drawImage*` with priority, opacity, source rects, `Graphics.glDraw`
  (vertex arrays), capture buffers, `Graphics.drawBox` / `drawScreen`.
  Done 2026-11-04 (Phase 1 "Drawing round" and "Shader round") and
  `redirectCameraFB` 2026-11-05, except clip planes.
- `Shader` — done 2026-11-04: translated by `core/lunadll.lua`'s `glsl.lua`,
  compiled and drawn by the engine (Phase 1 "Shader round").
- `Darkness`, `particles`, `textplus` fonts, `paralx2` backgrounds.
- Done when: 1-1 looks right.

### Phase 5 — Events, input, audio, saves
- Cancellable events (`eventObj.cancelled`): fire `onNPCHarm` / `onPlayerHarm` /
  `onBlockHit` *before* the engine acts, and respect cancel.
- `onEvent`, `onInputUpdate`, alt-jump/alt-run, `player:harm()` / `kill()`
  (harm/kill done 2026-11-04).
- `SaveData` / `GameData` persisted per episode. `Audio.*` is done (Phase 1,
  "Audio round"); audio sources (`SFX.create`) are SMBX2 Lua on top of it.
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
Some runs are ~25% slower in every listener at once (the machine, not the
code); since the performance round, compare the best or median of 3 runs.
`,memcallers` adds which lines read `mem()` most (sampled; slows the run).

For where native time goes, sample the running game without the probe:
start `run-level.sh` in the background, then `sample $(pgrep -n TheXTech)
10 -file out.txt`. The main thread's tree shows Lua (under `lua_pcall`),
the renderer and the swap; offsets inside a function (`mem_get + 1492`)
map to code with `objdump -d` on the Release binary.

**Done so far** (Release, M3 Pro, SMATRS 1-1 frames 100-400, ms per frame):

| Step | Frame avg | Lua avg | Notes |
|---|---|---|---|
| Debug, before this track | ~9.4 | ~7.6 | the first probe run |
| Release baseline | 3.7-6.4 | 2.5-5.2 | 0 to 116 JIT flushes per run |
| + one 8 MB JIT code area | 3.7-5.1 | 2.6-3.9 | no flushes after loading |
| + `mem()` reads through the FFI | 3.8-3.9 | 2.7-2.8 | 17,400 reads a frame |
| FileFormats round (more SMATRS runs) | 5.2-5.5 | 3.4-3.6 | 33,700 reads a frame |
| Audio round (all `onDraw` handlers run) | 6.7-6.8 | 5.0-5.1 | dummy audio; 7.1 / 5.3 with sound off |
| Drawing round (Lua draws backgrounds, BGOs, sizables, HUD) | 7.8-7.9 | 6.2 | sound off, frames 200-900; engine ~1.6 |
| Shader round (shaders and capture buffers drawn) | 9.7-11.4 | 6.6-7.4 | same; engine 3.1-4.0; runs vary ~1 ms |
| Sprite override round | 8.9 | 6.1 | frames 300-900, real sound (low volume); engine 2.8 |
| ID and collision group round | 11.3 | 8.2 | frames 300-900, muted; engine 3.1; 48,200 `mem()` reads a frame |
| Level flow and game actions round | 10.5 | 7.6 | same; engine 2.9; 12.5 of 13.9 traces abort a frame |
| Camera redirect round | 7.0 | 5.1 | same; engine 1.9 (no TheXTech lighting pass); 5.4 of 6.7 traces abort; Lua drop not explained by the round's changes |
| Same code, start of the performance round | 6.6 | 4.5 | same; engine 2.1; 12.4 of 13.8 traces abort |
| Performance round (steps 4-10 below) | 4.45-4.5 | 2.45-2.55 | same, 3 runs; engine 1.9-2.0; 11,700 `mem()` reads; ~1 of 2 traces aborts |

The FileFormats round lets SMATRS's whole `onStart` chain run, so far more of
its code runs each frame (`cutscenePal` and `darkness` `onDraw` ~0.35-0.6 ms
each, `extraBGOProperties` ~0.15 ms). Image sizes are now cached by path, as
LunaLua caches images: SMATRS's dialogue code rebuilds a font from the same
PNG every time it formats text.

The audio round adds more again: `music.lua`'s `onDraw` errored every frame
before, and one error ends that event's handler loop, so every `onDraw`
handler after it was skipped. Now `sizable.lua` (~1.35 ms, it draws sizable
blocks in Lua and rebuilds the `sBlockArray` list per read) and the rest run.
With sound off SMATRS reopens its music every frame (it never reports
playing); caching resolved paths took that from ~1.9 ms to nothing.

The drawing round sends `onCameraDraw` and `onHUDDraw` for the first time, so
SMBX2 draws backgrounds, BGOs and sizables itself (~1.3 ms of `onCameraDraw`:
`extraBGOProperties` 0.57, `darkness` 0.29, `editorevents` 0.10, `paralx2`
0.10); the engine stops drawing those, so its part stays ~1.6 ms. Top
listener is still `sizable.lua:onDraw` (1.32 ms). One frame per run spiked to
17-20 ms with ~11.5 ms in `onCameraDraw` (frame 831 both times; probably first
use of some picture or code path; not looked at yet).

The shader round draws what was skipped before: per frame in 1-1, ~45 script
draws with shaders (~20 into capture buffers), 5 captures and 6 clears. Each
screen capture is followed by a full-screen shader pass that reads it (three
in a row at priority 0), so they can't be merged. A CPU sample (macOS
`sample`) puts most of the engine's extra ~1.5 ms in Apple's OpenGL-on-Metal
starting render passes: every framebuffer switch or copy of the current
target begins a new one (~0.1 ms per capture; `glBlitFramebuffer` costs the
same). Keeping a capture buffer bound across consecutive draws into it is in;
it didn't measurably help here. Lua grew ~0.4 ms (SMBX2 packing uniforms in
`validateAndConvertVariableTable`). Android's drivers will behave differently;
measure there (Phase 7).

The ID round gives SMBX2's own objects their real IDs (before, SMATRS's 863
SMBX2-only objects in 1-1 were all ID 1), so their scripts now have work:
`blockeventmanager.lua:update` 0.81 ms, `npcEventManager.lua:update` 0.15
ms, and `mem()` reads went from ~34k to ~48k a frame. LuaJIT aborts ~14 of
15 traces it starts each frame (`LLEAVE`/`LUNROLL`/`LINNER` in
`ffi_mem.lua`, `blockeventmanager.lua:156`, `npcEventManager.lua:184`,
`ffi_npc.lua:444`), so much of that code runs interpreted; worth a look.
The frame-831 `onCameraDraw` spike (14 ms) is still there.

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

Performance round (2026-11-05), each step measured on 1-1 frames 300-900 with
`nosample`. A macOS `sample` of the running game (no probe) found most of
these; see "Measuring" for how.

4. **The sizable list once a frame.** SMBX2's `iterateSizable` reads the
   list's length at every step, and each read rebuilt it (scan every block,
   sort): `memSizableBlockCount` was ~470 of `mem_get`'s 839 samples.
   Now it's rebuilt once a frame (`xtech_lua_memNewFrame`) and after SMBX2
   writes the count or the list (spawning or deleting a sizable).
   `sizable.lua:onDraw` 0.97 -> 0.08 ms; frame 6.57 -> 5.75.
5. **No tail calls in `ffi_mem.lua`.** LuaJIT counts every tail call in a
   trace against its loop-unroll limit (`loopunroll`, 15, `lj_record.c`
   `lj_record_tailcall`), and the counter only resets when a trace starts.
   `mem()` -> `readmem` -> the FFI call was 2 tail calls per read (3 with
   the probe on), and SMBX2's generated field getters (`"=!Block:x"`,
   `ffi_utils.lua`) add one more, so any trace with more than a handful of
   reads aborted (`LUNROLL`). Results now go through a local. 5.75 -> 5.44;
   aborts 13.7 -> 7.5 a frame.
6. **Block and BGO ID scans in the engine.** `Block.iterateByFilterMap` /
   `iterate(ids)` (and BGO's) read every object's ID through `mem()`:
   `darkness.lua:964` and `blockeventmanager.lua:131` each read 14,000 block
   IDs a frame. Patched (`core/patches.lua`) to `__xtech_ffiBlockNext` /
   `__xtech_ffiBgoNext`, which find the next matching engine index, still
   fresh at every step like SMBX2's loop; the ID map becomes a byte table
   (`core.idFilter` in boot.lua, refilled when a loop starts).
   `BGO.getIntersecting` uses the BGO quadtree (`xtech_bgo_queryIntersecting`).
   `mem()` reads 48,200 -> 17,600; `blockeventmanager` update 0.38 -> 0.13
   ms, `darkness` `onDraw` 0.23 -> 0.05.
7. **Pointer globals by binary search.** Global reads walked all ~100
   pointer globals first; reads of object counts (in every BGO field
   access) made that `mem_get`'s hottest part. `mem_get` samples 281 -> 66.
8. **LuaJIT's penalty cache: 64 -> 1024 slots** (`smbx2compat/luajit.patch`).
   A spot that keeps aborting is blacklisted after ~10 aborts, but LuaJIT
   remembers the last 64 spots only; SMATRS has hundreds, so they pushed each
   other out and the same traces were recorded and thrown away every frame.
   Each abort is also a C++-style throw, and on macOS arm64 every throw makes
   dyld look up unwind tables (`_dyld_find_unwind_sections`), so aborts cost
   far more than their recording. The table moved out of `jit_State`: the
   arm64 VM reaches the dispatch table past `jit_State` with 12-bit offsets
   (`GG_G2DISP` must stay under 4096; it went 3992 -> 2976). Aborts 7-10 ->
   ~0.9 a frame; the trace compiler (`lj_ir_kgc`, unwinding) dropped out of
   the sample. Over two minutes, compiling settles at ~0.2 traces a frame
   and nothing flushes.
9. **BGO writes without tree updates.** `extraBGOProperties` hides every BGO
   it draws in `onCameraDraw` and shows it again in `onDrawEnd`; each write
   ran `syncLayers_BGO` (every layer's tree). Hidden writes now just store
   the value (drawing checks `Hidden`, as after `ShowLayer`), and location
   writes resync only when the location changed (`BackgroundMemory`,
   mememu.cpp). `extraBGOProperties` `onDrawEnd` 0.10 ms -> off the list.
10. **BGO count in one call.** Every BGO field read checks the BGO against
   `bgoGetCount()`, two `mem()` reads; now `__xtech_ffiBgoCount` (patched).
   `mem()` reads 17,600 -> 11,700; frame 4.45-4.47 over 3 runs.

Tried and dropped: `hotloop=56,hotexit=10` (LuaJIT's defaults; SMBX2 sets 3)
and `maxrecord=4000` both measured slightly worse; `loopunroll=60` removed
the tail-call aborts but not the time (step 5 does it without the side
effects). `SMBX2COMPAT_JITOPT` (boot.lua) sets JIT options for such trials.

**Open:** since the FileFormats round, every 1-1 run has had one ~31-35 ms
frame (onTick ~15 ms, heap +1.9 MB, dozens of traces compiled); not looked at
yet. The audio round's runs didn't show it (worst after frame 100: ~19-23 ms),
except one sound-off run with a 96 ms frame (frame 187, all in `onDraw`).
The engine's part also went from ~1.1 to ~1.85 ms a frame. Before that, a
rare single frame of ~60 ms (about 1 run in 5; not caught yet). The probe's "slowest" line shows the
events and heap change of such a frame; a large GC step is the main suspect
(SMBX2 runs the GC by hand in `onDrawEnd`, `main_events.lua`).

**Candidates, roughly in order of expected payoff (measure first):**
0. **Struct views.** The 11,700 `mem()` reads left in 1-1 are ordinary field
   reads (`v.x`, `v.id`, `v.width` in `blockeventmanager`, `blockutils`,
   `extraBGOProperties`, `sizable`). Each costs an FFI call plus `mem_get`'s
   range lookup and mememu's field tables (~10-20 ns). Making SMBX2's
   getters read engine memory directly would need the engine's structs laid
   out as SMBX's, or per-field getters; a project of its own.
   `extraBGOProperties` `onCameraDraw` (~0.25 ms) is the top listener.
1. **SMBX2's class lookup** around `mem()` (`player.x` is ~50 ns, of which
   the read is 6). The sampler shows `ffi_player.lua` `__index`/`get` and
   `classValidityChecker` near the top. Patch carefully: these are SMBX2's
   semantics.
2. **The other full scans in SMBX2's `ffi_*.lua`.** Each is O(objects) `mem()`
   calls per call:
   - `NPC.iterateIntersecting`/`getIntersecting` (`ffi_npc.lua:577`) — the
     NPC tree;
   - `BGO.iterateIntersecting` (`ffi_bgo.lua:345`; `getIntersecting` is
     done) — `treeBackgroundQuery`;
   - `Block.get(id)`/`NPC.get(id)`/`getByFilterMap` reading the type of
     every object (the iterate versions are done);
   - `Events.get(name)` and `Layer.get(name)` reading every name as a string.
     `ffi_events` runs `Events.get` on every triggered event
     (`onPostEventDirect`). Use the engine's `FindEvent`/`FindLayer`.

   Patch these the way `patches.lua` patches the block queries, keeping
   SMBX2's ordering and loop keys. Check the NPC index mapping (SMBX2's
   `NPC(idx)` is 0-based).
3. **Allocation.** String reads build a new `std::string` and Lua string on
   every call. `PlayerSettings`/`Animation` objects and the iterator closures
   are new tables each call.
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
