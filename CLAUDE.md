# TheXTech-Lua + smbx2compat

Personal, private fork of [vkdfkc/TheXTech-Lua](https://github.com/vkdfkc/TheXTech-Lua)
(TheXTech with LuaJIT scripting via luabind). The goal is to run SMBX2 (LunaLua)
episodes, starting with SMATRS (Super Mario and the Rainbow Stars), through a Lua
compatibility layer. Desktop (macOS) first, Android later.

## Upstream policy

This is a **separate fork** (decided 2026-11-04): we don't merge from vkdfkc or
TheXTech; the `upstream` remote is kept only for reference. Upstream TheXTech's
`AGENTS.md` (deleted here) forbids AI-generated code in contributions to their
project, so never prepare anything from this repo as an upstream PR.

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
engine code expects fields upstream lacks, so after checkout apply our patch:

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
turns on (SMBX2's scripts need Lua 5.2 syntax, as LunaLua's LuaJIT allows). After
changing LuaJIT's build flags, delete `build/external/luabind/src/LuaJIT_local-*`
and the game binary, or the old library stays linked.

## Build (macOS, Homebrew cmake + ninja)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTHEXTECH_ENABLE_LUA=ON
cmake --build build
```

For performance numbers use a Release build in its own folder
(`-B build-release -DCMAKE_BUILD_TYPE=Release`; `run-level.sh` runs it with
`XTECH_BUILD=build-release`). Debug is much slower (1-1: ~9.4 vs ~5.8 ms a frame).

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
build/bin/TheXTech.app/Contents/MacOS/TheXTech -c $D/assets-smbx13 -u $D/userdir -l "<level>.lvlx"
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

Test levels in `$D/smoke/`: `bonus1.lvlx` (harness written like a typical SMBX2
level script), `errtest.lvlx` (raises Lua errors every frame, through the
shim and directly from the engine; the game must keep running and log each once)
`spritetest.lvlx` (sets, clears and re-sets sprite overrides, then kills the
player so the level reloads and the overrides are restored at unload) and
`collisiontest.lvlx` (goombas in collision groups against floor blocks; it
logs `collisiontest: ... -> OK` or `WRONG` for each).

Debugging with lldb: ad-hoc builds can't be attached to until re-signed with the
`com.apple.security.get-task-allow` entitlement:
`codesign -s - -f --entitlements debug.entitlements build/bin/TheXTech.app/Contents/MacOS/TheXTech`
(a plist with that one key set to true). Rebuilding relinks and drops it.

## SMBX2 core mode

Turns on when both exist (otherwise the v0.1 shim runs; `SMBX2COMPAT_MODE=shim`
forces the shim):
- `$D/assets-smbx13/smbx2` → symlink to `$D/smbx2/SMBX2 + SMATRS/data` (SMBX2's
  `scripts/`, `graphics/`, `sound/`, `music/`, `costumes/`)
- `$D/assets-smbx13/scripts/smbx2core` → symlink to this repo's `smbx2compat/core`
  (so edits take effect without copying; Android will need a real copy)

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
  first, blending is LunaLua's premultiplied one. SMBX2's images load
  premultiplied (`StdPictureLoad::premultiply`), as in LunaLua; batched draws
  of them use `m_program_premultiplied`. Capture buffers are framebuffers with
  top-down rows like the game buffer. `redirectCameraFB` and clip planes
  aren't done.
- Object IDs: the engine has SMBX2's limits (2000 blocks, 1000 NPCs/BGOs/
  effects; `src/global_constants.h`). TheXTech's own types that clashed with
  SMBX2's moved above them: NPCs 301-306 to 1001-1006, conveyor blocks
  701/702 to 2001/2002, effects 149/150 to 1001/1002. `src/main/smbx2_ids.h`
  translates the NPCs for files: without SMBX2's data, levels and saves
  still say 301-306; graphics and `npc-N.txt` always do. With SMBX2's data
  installed (`g_smbx2Ids`, set in `LoadGFX`), default sprites come from
  SMBX2's `graphics/<category>/` first, then the asset pack.
- Collision groups and LunaLua's extended object fields (`noblockcollision`,
  `nonpccollision`, `collisionGroup`, ...): `SMBX2NPCExt_t`/`SMBX2BlockExt_t`
  inside `NPC_t`/`Block_t` (so they move with the object) and
  `g_smbx2PlayerExt[]` (`src/globals.h`). lunadll.lua hands SMBX2 structs that
  span a whole engine object, with the fields at the engine's offset
  (`__xtech_extendedFields`), and mirrors `Misc.groupsCollide` flips into
  `src/main/smbx2_collision.h`, which the engine checks where LunaLua hooks
  SMBX: NPC-NPC, held NPC-NPC, NPC-block, player-block, player-NPC,
  player-player.
- Sprite overrides (`Graphics.sprites.block[id].img = ...`): lunadll.lua's
  `FFI_SetSpriteOverride` -> `xtech_gfx_setSpriteOverride` ->
  `SpriteOverride_Set` (`src/load_gfx.cpp`), which swaps the engine's texture
  for a straight-alpha load of the same file and keeps a backup, like the
  per-level custom graphics; `UnloadCustomGFX` restores them. `hardcoded-*`
  overrides are kept for scripts only.
- Screenshots for checking a run: `SMBX2COMPAT_SHOTS=300,900 run-level.sh ...`
  saves those frames as `smbx2shot-N.png` in the log folder (read them with
  the Read tool). macOS doesn't let the shell's game write its own screenshot
  folder, and `screencapture` can't capture the window.
- SMATRS plays at 960x540 (`FFI_GraphicsSetMainFramebufferSize` sets the
  engine's internal resolution for the level).
- Every file read goes through `core.resolve`: backslashes → `/`, case-insensitive
  names (SMBX2 ships `classexpander.lua` but requires `classExpander`), files in
  `core/override/` replace the SMBX2 file at the same path under `scripts/`
  (only `lockdown.lua` and `ffi_mem.lua` so far), and `core/patches.lua` lists
  in-memory source edits (written with `\n`; they also match SMBX2's CRLF files).
  `Block.iterateIntersecting`/`getIntersecting` are patched to use the engine's
  block quadtree (`xtech_block_queryIntersecting`), since a full scan per call
  was ~20 ms a frame in SMATRS 1-1.
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
  Unknown addresses log once as `smbx2compat: mem() read outside emulated
  memory`. To find who reads one, temporarily log `debug.traceback()` in
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
- Timing: `SMBX2COMPAT_PROFILE=100-400[,nosample]` turns on `core/profile.lua`
  (frame/event/listener times, `mem()` counts, slowest frames, JIT traces and
  flushes, LuaJIT's sampler); results log as `[smbx2core] profile ...`.
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
  Drawing works (shaders, capture buffers and sprite overrides too) except
  `redirectCameraFB`. `Level.exit()` does nothing yet (its mode flags aren't
  in `mem()`), nor do `Misc.doPOW` and friends. See ROADMAP.md Phase 1
  "Next round".
- TheXTech prunes old files in the log folder, so read a run's log right
  after it (`run-level.sh` prints its path). SMBX2's warning reports
  (`WARNING_*.txt`) land there too.

## Game data (not in git)

Assets and episodes live outside the repo in `../TheXTech-data/`:
- TheXTech SMBX 1.3 asset pack (`thextech-smbx13-assets-full-v1.7.4.1.7z`).
- SMATRS episode zip (Nintendo-derived fan content; never commit it).
- `smbx2/SMBX2 + SMATRS/data/{scripts,graphics,sound,music,costumes}`: SMBX2's
  built-in Lua libraries and media, extracted from the SMATRS "Download All"
  bundle (`downloads/smatrs-smbx2-full.zip`). Never run anything from that bundle.
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

## Conventions

- Match the surrounding code style (C++: 4-space indent, braces on new lines,
  `pLogInfo`/`pLogWarning` for logging). Mark our engine edits with a short
  `// smbx2compat:` comment so they're easy to find when merging upstream.
- Don't commit game assets, episodes, or build output.
