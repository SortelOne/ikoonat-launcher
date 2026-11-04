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
  - `luabind_num_t.h` — **ours**. luabind converter for `num_t` (fixed-point
    type). Without it any Lua read/write of `Location.X`, `SpeedX`, etc. fails with
    "Trying to use unregistered class: num_t".
- `smbx2compat/` — **ours**. See `smbx2compat/README.md`.
  - `smbx2compat-engine.patch` — the C++ changes above (already applied here).
  - `scripts/smbx2compat.lua` — the shim mapping SMBX2 APIs onto `xtech_*`.
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

## Build (macOS, Homebrew cmake + ninja)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTHEXTECH_ENABLE_LUA=ON
cmake --build build
```

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
game from Claude's shell needs the sandbox disabled.

## Known issues

- **Lua errors raised during `onLoop` (per-frame) crash the game.** LuaJIT's
  `err_unwind` (`3rdparty/LuaJIT/src/lj_err.c:200`, "No C frame") finds no
  protected frame and calls `exit()`, although luabind's `lua_pcall` is on the
  stack; errors while loading scripts are caught fine. Reproduced with the
  smoke level (harness `luna.lua`, "No such operator defined" from luabind's
  `dispatch_operator`). Root cause not yet found; suspect a luabind call or error
  on a different `lua_State` (coroutines via `Routine`) or arm64 external
  unwinding. Investigate under lldb with a Debug LuaJIT and `LUA_USE_APICHECK`.
- SMATRS (`../TheXTech-data/smatrs/`) loads level 1-1 but its scripts stop at the
  first missing SMBX2 globals: `getSMBXVersionString` (episode `luna.lua:2`) and
  `Darkness` (`_scripts/smatrs/graphicalQuality.lua:22`). It also requires
  `npcManager`, `blockManager`, `lib3d`, `handycam`, `ext/easing` heavily.

## Game data (not in git)

Assets and episodes live outside the repo in `../TheXTech-data/`:
- TheXTech SMBX 1.3 asset pack (`thextech-smbx13-assets-full-v1.7.4.1.7z`).
- SMATRS episode zip (Nintendo-derived fan content; never commit it).
- `smbx2/SMBX2 + SMATRS/data/scripts` and `.../data/graphics`: SMBX2's built-in
  Lua libraries and graphics, extracted from the SMATRS "Download All" bundle
  (`downloads/smatrs-smbx2-full.zip`). Never run anything from that bundle.

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
