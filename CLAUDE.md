# TheXTech-Lua + smbx2compat

Personal, private fork of [vkdfkc/TheXTech-Lua](https://github.com/vkdfkc/TheXTech-Lua)
(TheXTech with LuaJIT scripting via luabind). The goal is to run SMBX2 (LunaLua)
episodes, starting with SMATRS (Super Mario and the Rainbow Stars), through a Lua
compatibility layer. Desktop (macOS) first, Android later.

## Upstream policy

We merge **TheXTech's `main`** from time to time (decided 2026-11-06), with a
real `git merge` of the `thextech` remote (https://github.com/TheXTech/TheXTech)
on its own branch, so later catch-ups bring only new commits; it goes into
`main` after Jake's OK. The first catch-up brought TheXTech from `bc30bf2f`
(2026-07-18, where vkdfkc stopped) to `69d73af1` (2026-11). vkdfkc's fork has
nothing newer; the `upstream` remote is kept only for reference. Merging:
new TheXTech object IDs that land in SMBX2's ranges move above them (see
"Object IDs" below); keep our `// smbx2compat:` hooks; move submodule pins
only forward (our PGE pin may be newer than theirs) and re-apply our
submodule patches. We never contribute back: upstream TheXTech's
`AGENTS.md` (deleted here) forbids AI-generated code in contributions to
their project, so never prepare anything from this repo as an upstream PR.

## Layout that matters here

- `script/src/` — the fork's Lua layer.
  - `xtech_lua_main.cpp` — Lua state, script loading, level/world hooks.
  - `xtech_lua_bindings.cpp` — luabind registrations (`xtech_*` globals).
  - `xtech_lua_fileformats.cpp` — **ours**. Level/world/`npc-N.txt` files as
    Lua tables for LunaLua's `FileFormats` (plain Lua C API, PGE underneath).
  - `xtech_lua_audio.cpp` — **ours**. LunaLua's `Audio` namespace on the
    engine's mixer (MixerX, as LunaLua uses), as the table `xtech_lunaAudio`.
    What it needs from `src/sound.cpp` is declared in `src/sound_script.h`.
  - `xtech_lua_draw.cpp` — **ours**. SMBX2's draw queue (`Graphics.glDraw`,
    `drawImage`, `Text.print`), drawn by priority between the engine's objects.
    The engine side (`gfx_update.cpp` calls, priorities, take-over switches) is
    declared in `script/include/xtech_lua_draw.h`. Shader and capture-buffer
    draws go to the renderer's script draws: `src/core/render_script.h`,
    `src/core/opengl/render_gl_script.cpp` (**ours**).
  - `xtech_lua_loadscreen.cpp` — **ours**. SMBX2's loading screens: a second
    Lua state drawn while a level loads (see "SMBX2 core mode").
  - `luabind_num_t.h` — **ours**. luabind converter for `num_t` (fixed-point
    type). Without it any Lua read/write of `Location.X`, `SpeedX`, etc. fails with
    "Trying to use unregistered class: num_t".
- `smbx2compat/` — **ours**. See `smbx2compat/README.md`.
  - `smbx2compat-engine.patch` — the C++ changes above (already applied here).
  - `scripts/smbx2compat.lua` — the v0.1 shim mapping SMBX2 APIs onto `xtech_*`
    (fallback when SMBX2's own scripts aren't installed).
  - `core/` — **SMBX2 core mode**: an emulated LunaLua host that runs SMBX2's
    real `main.lua` and libraries. See "SMBX2 core mode" below.
  - `luajit.patch` — our edit inside the LuaJIT submodule (Lua 5.2 compat option).
  - `test/` — mock harness: `cd smbx2compat/test && luajit run.lua`.
  - `test/sweep/` — many-level checks in the real game: `sweep.sh` (`-l`
    levels), `episode.sh` (levels entered from SMATRS's map), `reg.sh` (the
    regression set), screenshot tools. See its README.
  - `ROADMAP.md` — **the plan**: phases from here to SMATRS on Android, measured
    scale of SMATRS, and open questions for Jake. Start here.
- `3rdparty/PGE_File_Formats` — level/world file library (see below).

## Engine changes already applied (from smbx2compat-engine.patch)

`xtech_lua_loadLevel()` now:
1. Loads the shim into `_G` once: episode `smbx2compat.lua`, else
   `<assets>/scripts/smbx2compat.lua`. It logs `[smbx2compat] loaded v<version>`,
   so that line only appears after a **level** loads, not at startup.
2. Calls `__smbx2_beginLevel(levelDir, episodeDir)` (sets `require` paths).
3. Runs episode-wide `luna.lua` then level `luna.lua` / `level.lua` / `lunadll.lua`,
   each in its own sandbox (`runSandboxed`), and calls `__smbx2_attach(...)`.

## PGE_File_Formats

The fork's submodule pin (`b2e1cc5`) was never pushed upstream, so the submodule
is pinned to upstream `WohlSoft/PGE-File-Library-STL` (`e5fdc53`) instead. The fork's
engine code expects fields upstream lacks, so our patch goes on top. CMake
applies it, and LuaJIT's below, at configure time when they aren't applied
yet (`cmake/smbx2compat_patches.cmake`; a fresh clone needs nothing by
hand). By hand:

```bash
git -C 3rdparty/PGE_File_Formats apply ../../smbx2compat/pge-file-library.patch
```

It adds `name` (LVLX `NA`) to `LevelBlock`/`LevelNPC`; `extx`/`exty` (sprite-sheet
frame offsets, no file key) to `LevelBlock`/`LevelBGO`/`LevelNPC`; and 26 extended
`NPCConfigFile` traits read from `npc-N.txt` (`nolava`, `immortal`, `float`, ...).
We can't push to WohlSoft's repo, so edits inside the submodule must be
re-exported to that patch (`git -C 3rdparty/PGE_File_Formats diff > ...`).

Same for LuaJIT: `git -C 3rdparty/LuaJIT apply ../../smbx2compat/luajit.patch`
adds a `LUAJIT_ENABLE_LUA52COMPAT` CMake option, which `cmake/library_luajit.cmake`
turns on (SMBX2's scripts need Lua 5.2 syntax, as LunaLua's LuaJIT allows), and
raises the trace penalty cache from 64 to 1024 slots, moved out of `jit_State`
(`lj_jit.h`, `lj_trace.c`; why: ROADMAP.md Performance track, step 8). Keep
`jit_State` from growing: the arm64 VM reaches the dispatch table past it with
12-bit offsets. After changing LuaJIT's sources or build flags, delete
`build/external/luabind/src/LuaJIT_local-*` and the game binary, re-run
`cmake build` (configure writes the stamp files), then build; otherwise the old
library stays linked.

## Build (macOS, Homebrew cmake + ninja)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTHEXTECH_ENABLE_LUA=ON
cmake --build build
```

For performance numbers use a Release build in its own folder
(`-B build-release -DCMAKE_BUILD_TYPE=Release`; `run-level.sh` runs it with
`XTECH_BUILD=build-release`). Debug is much slower (1-1: ~9.4 vs ~5.8 ms a frame).

Memory errors: `build-asan` is a Debug build with AddressSanitizer
(`-DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer"`, the same
for `CMAKE_CXX_FLAGS`, `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address`;
part of clang). It covers the engine and the script library, not LuaJIT
or the external libraries. `run-level.sh` drops stderr, so run it with
`ASAN_OPTIONS=log_path=<scratchpad>/asan XTECH_BUILD=build-asan`; a report
lands in `asan.<pid>`. It is ~3x slower (`REG_SLOW=3 reg.sh`).

`THEXTECH_ENABLE_SDL_NET` (TheXTech's netplay) stays OFF, our default since
the 2026-11 TheXTech merge (TheXTech turns it on for macOS): with it on,
saves load only through the netplay session's `PreloadGame`, which the
library's Continue and `--save-slot` starts skip. A build folder configured
with it on needs `-DTHEXTECH_ENABLE_SDL_NET=OFF` (the option is cached).

`THEXTECH_FIXED_POINT` stays ON (the default); `num_t` is then a fixed-point class,
and `num_t(double)` is deliberately deleted, so convert with `num_t(x, nullptr)`.

Build fixes we carry (beyond the PGE patch):
- `cmake/library_luabind.cmake`: LuaJIT path uses `${PGE_LIBS_DEBUG_SUFFIX}`
  (Debug builds produce `libluajitd.a`).
- `CMakeLists.txt`: `-pagezero_size`/`-image_base` only on x86_64. On Apple Silicon
  they make the kernel SIGKILL the binary at exec (exit 137, no crash report).
  The standalone `build/output/bin/luajit` still has them from LuaJIT's own
  makefile, so it won't run here; use the game itself to test Lua.
- `xtech_lua_bindings.cpp`: `RangeArr::base()` doesn't compile in the default
  range-checked mode; index math uses `&Arr[first]` instead.

If submodule working trees ever show every file as deleted, restore them with
`git -C 3rdparty/<name> restore --source=HEAD --staged --worktree -- .`

## Running

```bash
D=../TheXTech-data
"build/bin/Ikoonat Launcher.app/Contents/MacOS/Ikoonat Launcher" -c $D/assets-smbx13 -u $D/userdir -l "<level>.lvlx"
```

`-c` asset pack, `-u` separate user dir (logs go to `$D/userdir/logs/`), `-l` level
test. The Debug build takes ~10 s to load assets on first run. The shim must be at
`$D/assets-smbx13/scripts/smbx2compat.lua` (copy it after editing). Running the
game from Claude's shell needs the sandbox disabled. The test userdir's
`settings/thextech.ini` has `background-work = true` under `[main]`; without it the
game pauses as soon as its window loses focus, which looks like scripts stopping.

Quick test run (sound off, window moved to AeroSpace workspace 1 so it stays out
of Jake's way, stops after N seconds and prints the log path):
`smbx2compat/run-level.sh <level.lvlx> [seconds]`. Prefer it over launching by hand.
With sound off (`-s`) there is no mixer at all; `XTECH_AUDIO=dummy` keeps sound
on but sends it to SDL's silent dummy device, so audio code really runs.
`XTECH_AUDIO=on` plays it at the test userdir's low volumes
(`settings/thextech.ini` `[audio]`, music/SFX 15); only when Jake asks.
`run-level.sh menu` opens the main menu instead (no level test).
Given a `.wld`, `run-level.sh` plays the episode as from the menu, in save
slot 3 (`XTECH_SAVE_SLOT`; slots 1-2 are Jake's). `XTECH_ARGS=-x` turns off
the frame limit, for long cutscenes. Test runs write SMBX2's
`<episode>/save3-ext.dat` and TheXTech's `userdir/settings/gamesaves/`.
`smbx2compat/play.sh` opens the game at its main menu with sound on, for
playing. Both use the same user folder, so `run-level.sh` deletes only its
own `smbx2shot-*.png` (logs and screenshots there may be Jake's). SMATRS
lives in `userdir/worlds/smbx/` so the menu lists it
(`$D/smatrs/SMATRS Chapter 1+2` is a symlink to it). The menu skips
symlinked episode folders on macOS (DirManager falls back to `d_type`).

Test levels in `$D/smoke/`: `bonus1.lvlx` (harness written like a typical SMBX2
level script), `errtest.lvlx` (raises Lua errors every frame, through the
shim and directly from the engine; the game must keep running and log each once)
`spritetest.lvlx` (sets, clears and re-sets sprite overrides, then kills the
player; the level ends after the death animation) and
`collisiontest.lvlx` (goombas in collision groups against floor blocks; it
logs `collisiontest: ... -> OK` or `WRONG` for each) and `actiontest.lvlx`
(`npc:harm`, `npcToCoins`, P-switches and the Block object of a block they
turn into a coin, `npc:collect`, then `Level.exit()`;
`actiontest: ... -> OK`/`WRONG`, then `actiontest: onExit`) and
`eventtest.lvlx` (the engine's events: start order, each cancellable one
cancelled once then let through, custom-NPC harm types, an instant warp;
`eventtest: ... -> OK`/`WRONG`, then `eventtest: onExit`) and
`pausetest.lvlx` (message boxes and the pause menu cancelled, `Misc.pause`,
color switch, generator, explosion, NPC transform, `onJump`,
`onCheckpoint`, a -1 flag through `mem()`; `pausetest: ... -> OK`/`WRONG`,
then `pausetest: onExitLevel 7`). A level test that shows an engine message
box or pause menu waits for a key until the run's time runs out; so does any
`-l` level that ends (a death, `Level.load`), at TheXTech's "restart?" pause
screen.

Debugging with lldb: ad-hoc builds can't be attached to until re-signed with the
`com.apple.security.get-task-allow` entitlement:
`codesign -s - -f --entitlements debug.entitlements "build/bin/Ikoonat Launcher.app/Contents/MacOS/Ikoonat Launcher"`
(a plist with that one key set to true). Rebuilding relinks and drops it.

## SMBX2 core mode

Each episode plays in **SMBX mode** (plain TheXTech: SMBX 1.3 behavior, the
asset pack's sprites) or **SMBX2 mode** (core mode, SMBX2's object IDs and
default sprites), decided when it starts (`src/main/smbx2_mode.cpp`,
`EpisodeMode_begin` in `StartEpisode`, battle levels, and before the first
graphics load for `-l` tests): `--episode-mode auto|smbx|smbx2`, else the
episode's `mode` in `<user>/settings/library.ini` (the library app's data,
`src/main/episode_library.cpp`; also records the detected mode and why,
last played, last slot), else auto-detect: Lua files, LVLX/WLDX with
`CPID:"SMBX2..."` in their HEAD (TheXTech's own say `"TheXTech"`), or
`npc-N`/`block-N`/... files past SMBX 1.3's and TheXTech's IDs, within two
folder levels. Each start logs `smbx2compat: episode <key>: <mode> (<why>)`.
Switching the default sprites reads every file again (~1.2-2.9 s in Debug,
`ReloadDefaultObjectGFX`), so the main menu turns SMBX2's scripts off but
keeps the sprites, and the game starts with the sprites of the episode
played last. SMBX2 mode also needs (otherwise SMBX mode; the v0.1 shim runs
levels that have scripts; `SMBX2COMPAT_MODE=shim` forces it):
- SMBX2's data folder (`scripts/`, `graphics/`, `sound/`, `music/`,
  `costumes/`, `config/`): the first that has SMBX2's `main.lua` of
  `--smbx2-data <dir>`, `library.ini` `[library] smbx2-data`, `<user>/smbx2`,
  `<assets>/smbx2` (here a symlink to `$D/smbx2/SMBX2 + SMATRS/data`).
- core mode's scripts: `<assets>/scripts/smbx2core` first (here a symlink to
  this repo's `smbx2compat/core`, so edits take effect without a rebuild),
  then the app's own copy (`Contents/Resources/smbx2core`, copied at each
  link), then `<user>/smbx2core` (where Android will copy it out).
Both are logged as `Lua: SMBX2 data in ..., core scripts in ...`.

How it works (details in `core/boot.lua`'s header):
- Each level gets a **fresh Lua state** (LunaLua does the same); the engine
  sets `__xtech_smbx2Path`/`__xtech_coreDir`/`__xtech_episodePath`/
  `__xtech_levelFile`, runs `boot.lua`, then calls `__onInit`. Frames call
  `__xtech_onLoop`/`__xtech_onRenderStart`/`__xtech_onRenderEnd`, which send
  SMBX2's events; level exit calls `__xtech_onExit`.
- `ffi.load("LunaDll.dll")` returns `core/lunadll.lua`, a Lua table of the DLL's
  exports; SMBX2's `ffi_*.lua` files run unmodified. Implement exports there,
  using LunaLua's `LuaProxyFFI*.cpp` as the spec. C types used in it are declared
  by the `ffi_*` files, so create cdata inside functions, not at file load.
  Return a NULL cdata (`ffi.cast("T*", nil)`), not `nil`, where C returns NULL.
- `core/host.lua` = LunaLua's C++ tables (`Misc`, `Text`, `Audio`, `Native`,
  `Level`, `FileFormats`, `UserData`, `console`, `triggerEvent`);
  `core/classes.lua` = its C++ classes (`Animation`, `PlayerSettings`,
  `inputConfig1/2`, the legacy `NPC` that SMBX2's `NPC.spawn` builds on);
  `core/constants.lua` = `setupDefaults()`.
- SMBX2 calls every library's handler for an event in one loop, so one error
  in an `onStart` handler silently skips all the later ones (whole features
  then look "missing"). Check the first `onStart` error in a log first.
  The same holds for every event: an error in one `onDraw` handler skips the
  rest each frame, so fixing it can make later handlers' costs appear.
- Audio: `host.lua` builds `Audio` from `xtech_lunaAudio` and resolves paths.
  Scripts share the engine's music stream; `Audio.SeizeStream` makes the
  engine's `StartMusic`/`StopMusic` leave a section's music alone until the
  level unloads (`SoundScript_Reset`).
- Drawing: `FFI_GLDraw`/`FFI_ImageDraw`/`FFI_TextDraw`/`LunaLuaAlloc` are C
  functions in `xtech_lua_draw.cpp`, called through the FFI (`__xtech_ffi*`),
  so they must never call into Lua. Draws queue with a priority; `gfx_update.cpp`
  draws everything below LunaLua's priority p (`SMBX2_DRAW_BELOW`) right after
  switching to the draw plane of the objects SMBX2 gives priority p. Triangles
  go through `XRender::renderTriangles` (OpenGL renderer only). The engine
  sends `onCameraUpdate`, `onDraw`, `onCameraDraw`, `onHUDUpdate`/`onHUDDraw`
  from its render loop (LunaLua's order). SMBX2's `paralx2`/`betterbgo`/
  `sizable` turn the engine's backgrounds/BGOs/sizables off and draw them.
- Shaders: `core/glsl.lua` translates SMBX2's GLSL 1.20 (fixed-function
  built-ins -> `xt_*` names the engine sets; `#version 330` on desktop, ES on
  Android) and `xtech_gfx_shaderCompile` compiles it; uniforms come from GL.
  A shader that fails here (logged with its error and first lines) still
  reports compiled, with uniforms read from its source, and draws without.
  Draws with a shader, into/from a capture buffer, or of points/lines are
  immediate GL draws (`RenderGL::scriptDraw`): the engine's queue is flushed
  first, blending is LunaLua's premultiplied one. As in LunaLua, a draw's
  `color` multiplies its `vertexColors` when it has both (textplus tints
  whole texts that way). SMBX2's images load
  premultiplied (`StdPictureLoad::premultiply`), as in LunaLua; batched draws
  of them use `m_program_premultiplied`. Capture buffers are framebuffers with
  top-down rows like the game buffer. `glDraw{depthTest=true}` (lib3d's 3D
  meshes) tests and writes depth (LEQUAL) in a depth buffer the capture
  buffer gets on first use (24-bit, as LunaLua's: lib3d's flat objects fought
  through 16); clearing the buffer clears it. A script shader's own attributes
  are bound after ours (`xt_Vertex` 0, ...) and the program relinked, since
  the linker gives a shader that reads no `gl_Vertex` (lib3d's) location 0
  for one of them otherwise. A shader over the
  GPU's limits (not a translation failure) reports uncompiled with no
  variables, so scripts with smaller fallbacks retry (SMATRS's map terrain).
  `Graphics.redirectCameraFB` queues
  `OP_REDIRECT`/`OP_UNREDIRECT` at its two priorities; `RenderGL::scriptRedirect`
  puts the capture buffer in `BUFFER_GAME`'s slot meanwhile, so the engine's
  own draws and passes go into it too (camera at its top-left; one camera,
  unscaled). Redirects end with each camera. Clip planes
  (`glDraw{clipPlanes=}`, fixed-function `glClipPlane` in LunaLua) are
  tested in the fragment shader: glsl.lua wraps every shader's `main` and
  discards pixels behind the planes (`xt_ClipPlane`/`xt_ClipPlanes`, in view
  coordinates); clipped draws take the immediate path.
- Shaders are kept for the session (each level's new Lua state would build
  them again, ~0.5 s of a level change): SMBX2's preprocessor output
  (patches.lua wraps `shader_preprocessor.execute`; reused while its
  `#include`s resolve to the same unchanged files), glsl.lua's translation
  (`xtech_misc_sessionCacheGet/Set`), and compiled programs by source
  (xtech_lua_draw.cpp; an idle one taken up again gets its uniforms reset to
  their values as linked, since SMATRS's shaders have initializers such as
  `framebufferSize = vec2(800.0, 600.0)`; the loading screen's state has
  programs of its own). `SMBX2COMPAT_SHADERCACHE=0` turns all three off.
- The engine's texture draws (`render_gl_frontend.cpp`) skip a source
  rectangle wholly outside the texture, as LunaLua's `GLDraw::DrawSprite`
  does: SMBX2's `npcutils.hideNPC` hides NPCs with frame -999, and the GPU
  stretched the texture's edge row into a bar instead (2-A's barrels).
- Script draws in scene coordinates are offset by the engine's whole-pixel
  camera (`vScreen::CameraAddX() - 0.5`, `xtech_lua_draw.cpp`), and images
  and text round with `floor(x + 0.5)`, so they land on the same pixel as
  the engine's `floor(CameraAdd + x)`. Rounding `CameraAdd + x` drew all of
  SMBX2's BGOs (betterbgo) a pixel off the engine's blocks (1-1's seams).
- Frames that aren't drawn (Options > SMBX2 > Draw rate "Half", the battery
  saver; TheXTech's frameskip; a blocked renderer) still send every render
  event (`UpdateGraphicsScriptsOnly` in `gfx_update.cpp`): SMBX2's scripts
  count on onTickEnd/onDraw/onCameraDraw/onHUDDraw/onDrawEnd each frame. The
  queue then runs only what LunaLua's render thread never skips: draws into,
  captures to and clears of capture buffers made nonskippable
  (`xtech_lua_drawSkipFrame`; SMATRS's map, pause menu and Rainbowpedia use
  them). The SMBX2 options section (`src/config.h`, `smbx2_*`) also has
  Loading screens (off: the engine's enter screen) and Frame time (average
  and slowest of the last 66 frames under the FPS; `g_microStats`). Keep
  `Config_t`'s members outside `#ifdef ENABLE_XTECH_LUA`: the script library
  is built without that define and must see the same struct. It inherits
  the engine's `A2XT_Int` definitions (`script/CMakeLists.txt`; TheXTech's
  netplay options sit inside `#ifdef THEXTECH_ENABLE_SDL_NET`), and
  `xtech_lua_init` stops the game with a critical log line if the two
  `sizeof(Config_t)` differ (it once read every option from the wrong place:
  "Lua engine disabled in config").
- TheXTech's own section lighting (`SectionLighting`, read from the same LVLX
  `darkness` settings) is off in core mode (`SMBX2_CORE()` in `gfx_update.cpp`):
  SMBX2's `darkness.lua` draws it, and both together darkened twice.
- SMBX2 keeps one Lua object per NPC/block index (with its `data` and
  `_settings`) and relies on LunaLua's internal events to follow the
  engine's moves: `KillNPC` calls `xtech_lua_event_npcRearrange` ->
  `__xtech_onNPCRearrange` -> `onPostNPCRearrangeInternal`. P-switches
  (`PSwitch` in `src/blocks.cpp`) keep a block's slot when it becomes a coin
  (id 0, hidden, `smbx2PSwitchFree`) instead of compacting, put returning
  blocks back into those slots (`xtech_lua_event_blockReuse` ->
  `onBlockInvalidateForReuseInternal`) and skip the classic-order re-sort,
  as LunaLua does. Any new engine code that moves or drops objects in the
  arrays needs the same.
- Engine events: the engine sends LunaLua's per-object events from where
  LunaLua hooks SMBX (`onNPCKill` in `KillNPC`, `onNPCHarm` in `NPCHit`,
  `onNPCCollect`, `onBlockHit`, `onBlockRemove`, `onPOW`, `onPlayerHarm`,
  `onPlayerKill` in `PlayerDead` (the start of a death; `player:kill()`
  calls it too, as LunaLua's does), `onWarpEnter`/`onWarp`, `onEventDirect` in
  `ProcEvent_Safe`, `onMessageBox` at each engine message box, `onPause`
  in `GameLoop`, `onColorSwitch`, `onNPCGenerated`, `onExplosionInternal`
  in `Bomb`, `onNPCTransform` around `NPCHit` and in `Deactivate`) through
  `xtech_lua_event_smbx2*`
  (`script/include/xtech_lua_events.h`) -> boot.lua `__xtech_on*`, and
  stops when a script cancels; SMBX2's `main_events.lua` sends the
  `onPost*` ones. `onStart` comes just before "Level - Start", then
  `onSectionChange` per player. These call into Lua, so no function
  scripts reach through the FFI may lead to them. SMBX2's NPCs take damage
  through LunaLua's `vulnerableharmtypes` (`NPCHit`); the extended
  properties reset at each level (`xtech_lua_memNewLevel`).
- Frame order and pausing: `__xtech_onLoop` (onLoop, the event queue) runs
  before the engine reads input, `__xtech_onInputUpdate` (onInputUpdate,
  onTick) right after it, so scripts can override `player.keys`.
  `Misc.pause()` holds the game from the next frame: `GameLoop` then only
  reads input, sends onInputUpdate (`__xtech_onPausedFrame`) and draws
  (`xtech_lua_smbx2LuaPauseFrame`). Key presses are queued from SDL's
  events (`keyboard.cpp`, as Windows virtual-key codes) and sent at the
  start of the next frame (`__xtech_onKey`), never from the event loop.
  `core.updateRawKeys` (lunadll.lua) copies players 1-2's keys as the
  engine read them into LunaLua's raw keymap (`player.rawKeys`, which
  SMATRS's menus read) before onInputUpdate; `__xtech_onKey` keeps
  `Misc.GetKeyState`'s array. A debug file can press keys by setting the
  player's controls (`player:mem(0xFA, FIELD_BOOL, true)`) in a wrapper
  around `core.updateRawKeys`.
- Episodes: `Misc.inEditor()` is SMBX2's `save slot == 0`, so `-l` level
  tests count as the editor and a world started with `--save-slot N` (as
  the menu does) is played. `Level.load` writes `GoToLevel` and leaves the
  level; TheXTech's hub code loads it next. `Misc.exitEngine`/`exitGame`
  (LunaLua ends the process; SMATRS's main-menu Quit, its only way out)
  leave the episode for the menu as TheXTech's pause-menu Quit does,
  without saving (`QuitEpisodeToMenu` in `game_main.cpp`), so a
  command-line world start quits (Android's library comes back); a level
  test closes the game. `SMBX2COMPAT_QUITAFTER=N` does the same N frames
  into each episode (test runs; SMBX mode too). `Misc.saveGame` ->
  `SaveGame()` (`src/main/game_save.cpp`), which sends `onSaveGame` first
  (`__xtech_onSaveGame`), as LunaLua does; SMBX2's `savedata.lua` then
  writes `<episode>/save<slot>-ext.dat`. With SMBX2's data a level without
  a player start starts anyway, at (0,0), as in SMBX (`game_main.cpp`).
  Start points go by their ID (LVLX files can list player 2's first).
  `Player.getTemplate(id)` (`Player(id+1000)`) is TheXTech's `SavedChar`
  for SMBX's characters (`memTemplate` in `xtech_lua_bindings.cpp`), as
  LunaLua's are SMBX's; `Player:transform` takes the powerup from it.
- Object IDs: the engine has SMBX2's limits (2000 blocks, 1000 NPCs/BGOs/
  effects; `src/global_constants.h`). TheXTech's own types that clashed with
  SMBX2's moved above them: NPCs 301-312 to 1001-1012, conveyor blocks
  701/702 to 2001/2002, its blocks 689, 697 and 701-730 (maze zones, tiny
  blocks, X2 functional blocks) to 2089, 2097 and 2101-2130 (+1400),
  effects 149/150 to 1001/1002. `src/main/smbx2_ids.h` translates them for
  files (`NPCIDFromFile`/`BlockIDFromFile` and back): without SMBX2's data,
  levels and saves still say 301-312 / 689-730; graphics, `npc-N.txt` and
  `block-N.txt` always do. With SMBX2's data installed (`g_smbx2Ids`, set
  in `LoadGFX`), default sprites come from SMBX2's `graphics/<category>/`
  first, then the asset pack. The in-game editor's palette still lists
  them by their file numbers (objects placed from it take the engine's
  number after a save and reload).
- Collision groups and LunaLua's extended object fields (`noblockcollision`,
  `nonpccollision`, `collisionGroup`, ...): `SMBX2NPCExt_t`/`SMBX2BlockExt_t`
  inside `NPC_t`/`Block_t` (so they move with the object) and
  `g_smbx2PlayerExt[]` (`src/globals.h`). lunadll.lua hands SMBX2 structs that
  span a whole engine object, with the fields at the engine's offset
  (`__xtech_extendedFields`), and mirrors `Misc.groupsCollide` flips into
  `src/main/smbx2_collision.h`, which the engine checks where LunaLua hooks
  SMBX: NPC-NPC, held NPC-NPC, NPC-block, player-block, player-NPC,
  player-player.
- Level endings and game actions: `Level.exit()` sets `LevelSelect`
  (`0xB2C5B4`), which mememu turns into `EndLevel`, so levels end through
  the engine's own path. SMBX2's script-run end states (8+) live in
  `LevelMacro` past 1000. `Misc.npcToCoins`/`doPOW`/`doPSwitch*`/
  `doBombExplosion` and `npc:harm`/`npc:collect` are `LuaMisc` functions in
  `xtech_lua_bindings.cpp`; `doPSwitchRaw` is queued for `UpdateBlocks`
  (`PSwitchScript`), since the switch's events can interrupt the frame.
- Sprite overrides (`Graphics.sprites.block[id].img = ...`): lunadll.lua's
  `FFI_SetSpriteOverride` -> `xtech_gfx_setSpriteOverride` ->
  `SpriteOverride_Set` (`src/load_gfx.cpp`), which swaps the engine's texture
  for a straight-alpha load of the same file and keeps a backup, like the
  per-level custom graphics; `UnloadCustomGFX` restores them. `hardcoded-*`
  overrides are kept for scripts only.
- Debugging episode scripts: `SMBX2COMPAT_DEBUG=/path/file.lua` runs that file
  in the level's state after its scripts load (before onStart), with `core`
  as its argument; `Misc.getCustomEnvironment()._G.require(...)` reaches the
  episode's modules and `SaveData`; `core.ffi` is LuaJIT's FFI (SMBX2's
  lockdown hides `require("ffi")`), e.g. to wrap the `LunaDll.dll` draw
  exports (`core.ffi.load("LunaDll.dll").FFI_GLDraw`) and log every draw;
  `core.getenv` is `os.getenv` (hidden too).
  Keep one-off debug files in the scratchpad (it is wiped overnight); reusable
  ones go in `smbx2compat/test/sweep/`.
- SMATRS's 3D world map is the level `World Map.lvlx` (hub of `World.wld`),
  drawn by the episode's `map.lua`/`lib3d.lua`. Played as an episode, a new
  save goes to `_prologue.lvlx` and a save boots to SMATRS's start menu over
  the map. Launched with `-l` it counts as in the editor (no main menu) and,
  with no save, leaves the player at (0,0) over open sea; set
  `SaveData.lastEnteredMapLevel` (debug file) to start at a level's spot.
  See ROADMAP.md Phase 6.
- `SMBX2COMPAT_CONFIG="draw-rate=2,show-frame-time=1"` sets TheXTech options
  (ini names, enums by number) for a test run at each level start (boot.lua),
  without touching the user folder's settings file, which Jake plays with.
- Screenshots for checking a run: `SMBX2COMPAT_SHOTS=300,900 run-level.sh ...`
  saves those frames as `smbx2shot-N.png` in the log folder (core mode
  only: boot.lua takes them, so SMBX-mode runs have none) (read them with
  the Read tool). macOS doesn't let the shell's game write its own screenshot
  folder, and `screencapture` can't capture the window.
  `SMBX2COMPAT_LOADSHOTS=10,60` does the same for frames of each loading screen.
- Loading screens (LunaLua's Misc/LoadScreen.cpp): a second Lua state for the
  session (`xtech_lua_loadscreen.cpp`), set up by `boot.lua` with
  `__xtech_loadScreen` (no host.lua/classes.lua), runs `core/loadscreen.lua` ->
  SMBX2's `base/engine/loadscreen.lua` -> the episode's `loadscreen.lua`. It
  replaces the engine's enter screen in core mode: started before `OpenLevel`
  (after it for `-l` tests), it draws a frame whenever 15 ms have passed at
  `xtech_lua_loadScreenUpdate("step")` calls (in `OpenLevelData`, the core-mode
  level start, and `core.resolve` for every file the level's scripts look up),
  and ends at the level's first render (`UpdateGraphicsDraw`) once the script's
  `_loadScreenTimeout` has passed (SMATRS's closing animation; the game waits,
  as in LunaLua). Its images, shaders, buffers and draw queue are swapped in
  only while it runs (`xtech_lua_drawSwapLoadScreen`), so handles never mix
  with the level's. Each one logs `Lua: loading screen: loaded in ... ms,
  ... longest gap N ms (step)`: the step after the longest stretch without a
  frame. `SMBX2COMPAT_LOADSCREEN=0` shows the engine's enter screen instead
  (to compare load times). Since both states call the bindings, a binding
  that builds a Lua value takes the calling `lua_State* L` as its first
  parameter (luabind fills it in), never `xtech_lua_getState()` (the level's).
- SMATRS plays at 960x540 (`FFI_GraphicsSetMainFramebufferSize` sets the
  engine's internal resolution for the level). When a script sets the size,
  NPCs activate on that whole screen (`ModernNPCScreenLogic`,
  `xtech_lua_drawScriptScreenSize`), not on TheXTech's 800x600 one in its
  middle; other levels keep the 800x600 one, SMBX's screen.
- TheXTech's controller overlay doesn't flash the input profile's name
  ("Keyboard 1") in core mode (`speedRun_renderControls`); the missing-
  controller picture ("P1 ?") still shows, so test runs with no key
  pressed show it in the bottom-left corner throughout.
- Every file read goes through `core.resolve`: backslashes → `/`, relative
  paths from SMBX2's data folder (its working folder on Windows), case-insensitive
  names (SMBX2 ships `classexpander.lua` but requires `classExpander`), files in
  `core/override/` replace the SMBX2 file at the same path under `scripts/`
  (only `lockdown.lua` and `ffi_mem.lua` so far), and `core/patches.lua` lists
  in-memory source edits (written with `\n`; they also match SMBX2's CRLF files).
  `Block.iterateIntersecting`/`getIntersecting` are patched to use the engine's
  block quadtree (`xtech_block_queryIntersecting`), since a full scan per call
  was ~20 ms a frame in SMATRS 1-1; `BGO.getIntersecting` the same way.
  Block/BGO `iterate(ids)`/`iterateByFilterMap` find matches in the engine
  (`__xtech_ffiBlockNext`/`__xtech_ffiBgoNext`, FFI pointers like
  `__xtech_ffiMemGet`), and `bgoGetCount` is one FFI call.
  Opening a file costs ~0.4 ms on Jake's Mac, so file checks use folder
  listings kept for the level (`xtech_misc_pathKind`; `lockdown.lua`'s
  `io.open` drops a folder's listing when a script writes there), file
  contents stay in memory across levels while their size and time match
  (`xtech_misc_readFileCached`, LunaLua's cached reads), and so do script
  images (`s_pictures` in xtech_lua_draw.cpp; textures freed each level end).
- `mem()` (`override/.../ffi_mem.lua`) sends every address to `xtech_mem_get/set`
  (strings: `xtech_mem_getString/setString`) in `xtech_lua_bindings.cpp`.
  Globals go to mememu. SMBX keeps arrays behind pointer globals; each gets a
  made-up address range:
  - `s_objMemArrays`: struct arrays. Players/NPCs/blocks/BGOs go through
    mememu's per-object emulators (`PlayerMemory`, `NPCMemory`, `BlockMemory`,
    `BackgroundMemory` in `src/script/luna/mememu.cpp`); layers, events, warps,
    water, cameras, and every object's string fields go through the `MemField`
    tables next to it (SMBX offset -> engine getter/setter).
  - `s_memTypeArrays`: arrays of one value per ID or section (NPC/block
    config, section bounds, ...). IDs past the engine's limits are storage.
  Reads go through LuaJIT's FFI (a C function pointer, `__xtech_ffiMemGet`
  -> `LuaMisc::ffi_mem_get`) so traces can include them; writes and strings
  stay on luabind. Nothing an FFI-called function does may call back into Lua.
  Return those results through a local, not as `return f(...)`: LuaJIT counts
  each tail call in a trace against its loop-unroll limit (15) and aborts the
  trace past it. SMBX's sizable list (`0xB2BEE4`/`0xB2BED8`) is rebuilt from
  the engine's blocks once a frame and after SMBX2 writes it.
  Engine flags read as -1 (VB6's True) through `FIELD_WORD`, as SMBX
  stores them (`valueToMem(bool)` in mememu.cpp; it gave 65535 before).
  Double globals also take Float reads/writes (SMBX's Singles, e.g. SMBX2's
  `Defines`). Fields TheXTech dropped that scripts use get engine values or
  storage: NPC spawn size 0xB8/0xC0 (`NPC_t::smbx2SpawnWidth/Height`),
  the episode list (`SelectWorld`), `Star`, Defines without an engine value,
  NPC 0x74 (`GeneratorActive`, which SMATRS sets to run a room's generators
  off screen) and 0xE0 (`NPC_t::smbx2DefaultSpecial2`, `ai2` on respawn).
  SMBX2's NPC type flags (`isflying`, `isshoe`, ...) are fixed per type in
  TheXTech (`npc_constant_traits.h`); writes are dropped, logged once.
  `iswalker` (SMBX's `NPCDefaultMovement`; SMBX2's Grrrol rolls by it) is
  kept in `NPCTraits_t::smbx2DefaultMovement` instead, which
  `npc_movement_logic.cpp` checks.
  Unknown addresses log once as `smbx2compat: mem() read outside emulated
  memory`; MemEmu's unknown object fields and type mismatches (a script
  reading SMBX's Single/Double where TheXTech keeps an integer) log once
  per field too. To find who reads one, temporarily log `debug.traceback()` in
  `ffi_mem.lua`'s `readmem` for that address range.
- Custom params: the `CustomParams` field on `Block_t`/`Background_t`/`NPC_t`
  (set in `level_file.cpp`) + `core/extrasettings.lua` (needs SMBX2's
  `PGE/configs/SMBX2-Integration` .ini/.json files in the data folder).
- Paths handed to scripts carry a virtual `C:` (`C:/Users/...`), because SMBX2
  checks for a drive letter to decide a path is absolute; `core.normalize` strips it.
- Gaps log `[smbx2core] missing: X` once each, plus a summary at level exit.
  Script errors log once as `[smbx2core] ==> file:line: message` with a traceback.
- `run-level.sh` passes an absolute level path; LunaLua's episode paths are always
  absolute.
- `boot.lua` sets `jit.opt.start("sizemcode=8192", ...)` before anything
  compiles: with LuaJIT's 64 KB code areas, arm64's branch range made it throw
  away all compiled code every few frames in SMATRS (ROADMAP.md, Performance
  track). Don't remove it.
- Memory: every core-mode level start logs `[smbx2core] memory: ...`
  (`logMemory` in `xtech_lua_main.cpp`: process memory, both Lua heaps,
  GL objects, session caches, leftover handles); compare the lines across a
  long run for growth (ROADMAP.md, Milestones, "Before M6"). The loading
  screen's state lasts the session, so anything it keeps per load piles up:
  `core/loadscreen.lua` drops SMBX2's `childEnvsByGlobal` contexts and
  LPegLJ's `valuetable` entries made since it started before each init,
  and flushes that state's JIT traces (they kept each old context alive
  until LuaJIT's next flush). The line's loading-screen heap is counted
  after a full collection, swapped in like every call into that state
  (`xtech_lua_loadScreenLuaKB`): its image finalizers free from the draw
  tables in place.
- Timing: `SMBX2COMPAT_PROFILE=100-400[,nosample][,memcallers][,load]` turns on
  `core/profile.lua` (frame/event/listener times, `mem()` counts and, with
  `memcallers`, the lines reading most, slowest frames with their slowest
  listener, JIT traces and flushes, LuaJIT's sampler; with `load`, LuaJIT's
  sampler over the level's load, from boot.lua to the first frame drawn);
  results log as `[smbx2core] profile ...`.
  Compare the best or median of 3 runs (some runs are ~25% slower overall).
  `SMBX2COMPAT_JITOPT="hotloop=56,..."` overrides SMBX2's JIT options for
  trials. For native time, `sample` the running game (ROADMAP.md,
  Performance track, "Measuring").
- Reading a run: `grep -n "Lua: \|Lua error\|smbx2core" <log> | grep -v MemEmu`;
  a distinct-error tally: `grep "==>" <log> | sed 's/.*==> //' | sort | uniq -c`.
  The Lua files under `core/` can be syntax-checked with the system LuaJIT
  (`/opt/homebrew/bin/luajit -e "assert(loadfile('f.lua'))"`); it runs here,
  unlike `build/output/bin/luajit`.

## Lua error handling

- luabind's `luabind::error` constructor **pops the error message off the Lua stack**
  and keeps it in `e.what()`. Catch sites must log `e.what()` and never `lua_pop`
  again: an extra pop per error corrupted the VM's frames until LuaJIT found no
  protected frame and called `exit()` (`lj_err.c:200`, "No C frame") — the old
  "onLoop errors crash the game" bug.
- `luabind::set_pcall_callback` installs a `luaL_traceback` handler, so every
  engine-called Lua error is logged with a stack traceback; `logLuaError` in
  `xtech_lua_main.cpp` logs each distinct error once instead of every frame.
- The shim's `callSafe` uses `xpcall` + `debug.traceback`, also once per error.
- luabind userdata (`xtech_player_get`, `xtech_npc_get`, ...) has no `__eq`:
  `==`/`~=` between two of them raises "No such operator defined". Each fetch
  returns a new userdata anyway, so compare indices instead.

## Known issues

- In core mode, SMATRS 1-1 runs with no script errors and no unknown `mem()`
  addresses (bonus1 has its old ones). Audio works (checked by ear).
  Drawing works (shaders, capture buffers, `redirectCameraFB`, sprite
  overrides and clip planes). 2-1 runs with no errors too. The
  episode plays from `World.wld`: prologue, 1-1, the map and back.
- `-l` level tests skip what SMATRS only does in episode play (1-1's intro
  cutscene, entered from the map); check those through `World.wld` with a
  debug file that presses keys (above) and, to leave a level, calls
  `Level.load`.
- TheXTech prunes old files in the log folder, so read a run's log right
  after it (`run-level.sh` prints its path). SMBX2's warning reports
  (`WARNING_*.txt`) land there too.

## Game data (not in git)

Assets and episodes live outside the repo in `../TheXTech-data/`:
- TheXTech SMBX 1.3 asset pack (`thextech-smbx13-assets-full-v1.7.4.1.7z`).
- SMATRS episode zip (Nintendo-derived fan content; never commit it).
- `smbx2/SMBX2 + SMATRS/data/{scripts,graphics,sound,music,costumes,config}`:
  SMBX2's built-in Lua libraries, media and configs (`config/effects/` gives
  `Effect.config`), extracted from the SMATRS "Download All" bundle
  (`downloads/smatrs-smbx2-full.zip`). Never run anything from that bundle.
- `reference/LunaLua`: LunaLua's C++ source (WohlSoft/LunaLua, shallow clone), the
  reference for what SMBX2's scripts expect from the host. Read only; never build
  or run it. Pointers to the key files are in ROADMAP.md, Phase 1 plan item 5.

## SMBX2 compatibility status

v0.1 shim covers players, NPC get/spawn/iterate, events, Text/Graphics/SFX basics,
camera, sections, layers, SaveData/GameData. Not available: `mem()`, SMBX2 basegame
libraries (`npcManager`, `npcutils`, ...), shaders, render priority, cancellable
events. SMBX2-only block/NPC IDs won't render. Unimplemented calls warn once; call
`smbx2compat.report()` for the list. `SMBX2COMPAT_STRICT = true` makes them errors.
Values marked `VERIFY` in the shim come from memory and need checking in play.

## App name and icon

The macOS app is *Ikoonat Launcher* (`THEXTECH_BUNDLE_NAME`, bundle ID
`io.github.sortelone.ikoonat`), so it isn't confused with upstream's
TheXTech: `build*/bin/Ikoonat Launcher.app/Contents/MacOS/Ikoonat Launcher`
(quote the path; find the process with `pgrep -x "Ikoonat Launcher"`).
Jake's AeroSpace config sends windows with that bundle ID to workspace 1.
Build folders configured before the rename need
`cmake -B <dir> -DTHEXTECH_BUNDLE_NAME="Ikoonat Launcher"` (cached). Icons:
`resources/ikoonat/` (`leaf.png`, Jake's 100x100 pixel art;
`make_icons.py` writes `icon_<size>.png` and `thextech.icns`). On macOS the
engine no longer sets the asset pack's logo as the Dock icon
(`WindowSDL::updateWindowIcon`). Android's icon and app ID come with the
episode library app (ROADMAP.md Phase 7).

## Episode library (the first screen)

The app opens on the episode library (`src/main/screen_library.cpp`; design
in ROADMAP.md Phase 7), run from `GameMain`'s `GameMenu` branch in place of
the title menu (off for command-line level tests; `SMBX2COMPAT_NOLIBRARY=1`
shows TheXTech's menu). Settings has Theme, Accent color, Game options
(TheXTech's `OptionsScreen`), Classic menu (Back on its main page returns)
and Quit; Continue starts the episode straight away with the players and
character saved for it (`library.ini`), as the command-line world start does.
Text uses the system font (`/System/Library/Fonts/SFNS.ttf`, Android's
Roboto): the asset pack's TTFs are pixel fonts for one size. Episode
pictures: ~45 s into an episode a frame is saved to
`<user>/settings/library/<key>.png` (`thumbAfterFrame` after each repaint).
Importing (`src/main/library_import.cpp`): drag a zip or folder onto the
window, or Import (Finder's panel, `library_picker_macos.m`); zips are
unpacked with `/usr/bin/unzip` (no shell, no Windows programs) into
`<user>/import-tmp`; SMBX2's data goes to `<user>/smbx2`, episodes to
`<user>/worlds/<name>` (existing save files kept). It never follows
symlinks. `--library-import <path>` imports without opening a window.
Testing the screen (it only draws in `run-level.sh menu`):
`SMBX2COMPAT_LIBKEYS="60:do,90:right"` presses keys (up/down/left/right/do/
back/erase) on those frames, `SMBX2COMPAT_LIBSHOTS=40,100` saves
`libshot-N.png` in the log folder, `SMBX2COMPAT_LIBDROP="60:/path.zip"`
drops a file, `SMBX2COMPAT_THUMBFRAMES=N` captures pictures sooner,
`SMBX2COMPAT_LIBCYCLE="a|b"` selects whichever of those episode folders
was played least recently (with `SMBX2COMPAT_QUITAFTER`, a loop through
episodes of both modes in one process; `test/sweep/README.md`). Test
imports and settings in a scratch data folder (`XTECH_DATA`, with a symlink
to `assets-smbx13` and a copy of `userdir/settings/thextech.ini`), not Jake's.

## Android app (native library)

On Android the library is a native screen, not the engine's (Jake,
2026-11-06): Kotlin + Jetpack Compose in
`android-project/thextech/src/main/kotlin/io/github/sortelone/ikoonat/`
(its `README.md` explains the pieces). It starts the game
(`thextechActivity`, own process `:game`) with the episode's command-line
arguments in the `ikoonat-args` extra, as `run-level.sh` does; the game
quits back to it when the player leaves the episode. Engine side for it:
`--classic-menu` (TheXTech's menu instead of the in-engine library) and
`SaveSlots_recordInLibrary` (`src/main/game_save.cpp`: each slot's stars,
progress and play time into `library.ini` when an episode starts, saves
and ends; `DeleteSave` also deletes SMBX2's `save<N>-ext.dat`). The app ID
is `io.github.sortelone.ikoonat`; the Java/JNI package stays
`ru.wohlsoft.thextech`. Android launcher icons:
`python3 resources/ikoonat/make_icons.py --android`. There is no Android
SDK on this Mac (don't install one without asking): Kotlin changes can't
be compiled here, so read them twice; Jake builds on his Windows PC in
WSL (`smbx2compat/ANDROID-BUILD.md`: setup, `./gradlew assembleApkRelease`,
keeping each build's unstripped `libthextech.so`) and tests on the Odin.
Release APKs are signed with the building computer's debug key and keep
the plain app ID (`thextech/build.gradle`).

Script graphics on OpenGL ES (the Odin: Adreno 830, ES 3.2, 256 uniform
vectors per stage):
- glsl.lua translates for `#version 310 es` with
  `GL_EXT_shader_implicit_conversions` where the driver has it; ES has no
  uniform initializers, so glsl.lua takes them out and returns them as
  `name=v,v\n` lines, which `scriptShaderCreate` sets after linking (part of
  the program cache key); whole-number literals passed to GLSL 1.20's
  float-only built-ins get `.0` (Adreno doesn't apply the extension there).
  GLSL 1.30+ sources too (lib3d's): there only the float-only built-ins
  get `.0` directly; `min`/`max`/`clamp` (int versions exist) get it when
  their first argument surely is a float, else go through `xt_<name>L`
  helpers overloaded for float and int types (`floatArguments`,
  `limitHelpers`). This was the cause of SMATRS's invisible map terrain on the
  Odin: lib3d's alpha cutoff `clamp(sign(a - 0.5),0,1)` (terrain is the only
  ALPHA_CUTOFF material) discarded every pixel; v11/v12 showed the terrain's
  pixels never appear even as a plain colour, while uniforms, matrices and
  geometry match the cliffs'. Confirmed in v13 (the terrain drew).
- Drivers without `GL_EXT_shader_implicit_conversions` (the Retroid Pocket
  Nova: Adreno 740, driver of 12/2023; header `#version 300 es`) compiled
  none of SMATRS's map, intro book or lighting shaders (`const int` and
  `float` mixed). `core/glsltypes.lua` (`glsl.types`) follows each ES
  shader's types (declarations, structs, functions, the preprocessor's
  active branches and object-like macros) and writes the conversions out
  (`1` -> `1.0`, `float(i)`, `vec2(iv)`): mixed arithmetic and comparisons,
  assignments, returns, `?:`, the shader's own functions' arguments and the
  built-ins' (GLSL < 1.30: all float-only, so every int argument). It leaves
  what it can't type alone; a shader it can't parse (a function-like macro
  left in) falls back to `floatArguments` (logged `shader types not
  followed`), and a converted shader that doesn't compile is retried
  without (logged `compiles only without glsltypes.lua's conversions`).
  Checked offline with `smbx2compat/test/glsl/esconvert.lua` (glslc): of
  262 SMBX2/SMATRS shader variants, 108 failed under ES rules before, 1
  after (SMBX2's `map3d/texturetiler.frag`, unused by SMATRS). ES 3.x
  built-ins and reserved words no SMBX2 shader version has (`imageSize`,
  SMATRS's uniform name) are renamed `xt_r_*`, and GLSL 1.30 sources'
  variables named like 1.30 built-in functions (`texture`, `textureSize` in
  poison.glsl) are renamed where they aren't called.
- Uniform arrays of single ints/uints (SMATRS's map terrain: `variants`,
  `sizes`) are packed four to a vector for ES 3 (`packScalarArrays` in
  glsl.lua: `xt_p4_<name>` as `uvec4`/`ivec4`, uses rewritten to
  `[int(i) >> 2][int(i) & 3]`; reported to scripts as the vector array, so
  SMBX2's ffi_graphics fills it from the same numbers). Suspected Adreno
  misreading of dynamically indexed scalar arrays: the terrain drew nothing
  on the Odin, while every map shader with vector arrays drew. Not yet
  confirmed on the device (v10); `probeUintUniforms` in
  render_gl_script.cpp logs what the first such draws sent and what GL holds
  (`smbx2compat probe:`), temporary.
  v10 on the Odin: GL holds the arrays as sent (and packed, the 96-piece
  terrain fits), yet the terrain still doesn't show, so packing wasn't the
  cause; v11 coloured the terrain's pixels by what its shader sees (a patch
  in `core.shaderPatches`, below; removed again in v13).
- Android has no environment for `SMBX2COMPAT_DEBUG`: boot.lua runs
  `<user folder>/smbx2compat-debug.lua` when it exists (logged as `running
  debug file ...`). `core.shaderPatches` (lunadll.lua) swaps text in shader
  sources before translation (`{find, replace, only}`, plain text); a debug
  file can replace the list to try shader changes without a new APK (the
  core scripts in `<user folder>/smbx2core` can also be edited in place
  until the next app update copies them again).
- After Ikoonat's menu closes, `s_ikoonatMenuCheck` blanks every player's
  buttons until all are let go (the press that picked Continue reached the
  scripts too).
- Settings shows `Ikoonat <build> · built <time>` under its title
  (`BuildConfig.IKOONAT_BUILD`/`BUILD_TIME`; `./gradlew -PikoonatBuild=v11`).
- Non-square matrix and `uint` uniforms use ES3 functions loaded with
  `SDL_GL_GetProcAddress` (`SCRIPT_GL3_UNIFORMS`, render_gl_script.cpp):
  Android builds only have the ES2 header.
- Captures blit instead of `glCopyTexSubImage2D`: ES can't copy the game
  buffer (GL_RGB) into an RGBA texture, and every capture stayed empty.
- `ScriptGLCheck` (render_gl_script.cpp) logs the first GL error before or
  inside each kind of script operation (`smbx2compat: GL error 0x... in
  script <op>`); it found the capture bug.
- Android drops the GL context when the device sleeps ("Android: Render
  Device Reset"): `FrmMain::restartRenderer` makes a new RenderGL, then
  `xtech_lua_drawRendererRestarted` compiles every kept program again and
  makes the capture buffers again under the same handles. Their contents
  would be lost (SMATRS's pause menu labels, drawn into buffers once, went
  blank), so `xtech_lua_drawWillLoseContext` reads every buffer into memory
  at `SDL_APP_WILLENTERBACKGROUND` (`events_sdl.cpp`; the bundled SDL2 backs
  up the EGL context only after the app has taken the background events, so
  it still works there) and the restart writes them back
  (`scriptBufferRead`/`scriptBufferWrite`, RGBA in GL's row order). If the
  context survives, the copies go 10 frames after `DIDENTERFOREGROUND`. Logs:
  `going to sleep; kept N capture buffers' pixels (MB, ms)`, then `restored
  N buffers' pixels` or `the GL context outlasted the sleep`.
- Ikoonat's menu (`PauseScreen::RequestIkoonatMenu`, `s_ikoonatMenuCheck`
  in game_loop.cpp): in SMBX2 mode Android's Back key (no longer the pause
  key there) or Start + Drop Item held 60 frames opens TheXTech's pause
  screen with Continue / Options / Quit to Library, without asking the
  scripts (SMATRS greys out its own quit).
- `EpisodeMode_begin` empties SMATRS's `<episode>/_loadscreen/data.txt` (the
  next loading screen's card, written at level end): a game closed inside a
  level showed that level's card at the next start.

## Conventions

- Match the surrounding code style (C++: 4-space indent, braces on new lines,
  `pLogInfo`/`pLogWarning` for logging). Mark our engine edits with a short
  `// smbx2compat:` comment so they're easy to find when merging upstream.
- Don't commit game assets, episodes, or build output.
- Commits use Jake's GitHub no-reply address
  (`19493690+SortelOne@users.noreply.github.com`; GitHub refuses pushes
  that expose the account's own email). The repo is private; making it
  public needs Jake's explicit go.
