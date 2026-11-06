# smbx2compat: SMBX2 episodes on TheXTech-Lua

This folder makes TheXTech-Lua play SMBX2 (LunaLua) episodes, starting with
*Super Mario and the Rainbow Stars* (SMATRS, Chapters 1 and 2), the way they
play in SMBX2 on Windows, without editing the episode. macOS (Apple Silicon)
works now; Android (AYN Odin 3) comes next.

The plan, the measured size of SMATRS and the history of every fix are in
[`ROADMAP.md`](ROADMAP.md). Engine-level notes for working on the code are in
the repo's [`CLAUDE.md`](../CLAUDE.md).

## How it works

SMBX2 is SMBX 1.3 (a Windows program) plus LunaLua, a DLL that patches the
game and runs Lua scripts. About 175k lines of SMBX2's own Lua (its engine
scripts and ~50 libraries) and SMATRS's ~270k lines sit on top of LunaLua's
C++ API.

Rather than rewrite those libraries, **SMBX2 core mode** runs them
unmodified on an emulated LunaLua host:

- `core/` plays the part of LunaLua's C++: the host tables (`Misc`, `Text`,
  `Audio`, `Level`, ...), its classes, and a Lua stand-in for `LunaDll.dll`
  that SMBX2's 17 `ffi_*.lua` files call through LuaJIT's FFI.
- The engine (TheXTech, C++) gained what those calls need underneath: SMBX
  memory emulation for `mem()`, SMBX2's events sent from where LunaLua hooks
  SMBX, a draw queue with render priorities, GLSL shaders, capture buffers
  and 3D depth, LunaLua's audio API on the engine's mixer, loading screens,
  collision groups, extended object fields and SMBX2's higher object ID limits.
- Each level gets a fresh Lua state, as in LunaLua.

Core mode turns on by itself when SMBX2's data folder is installed (below).
Without it the old v0.1 shim (`scripts/smbx2compat.lua`, a hand-written
subset of SMBX2's API) runs instead; `SMBX2COMPAT_MODE=shim` forces the shim.

## What works

- SMATRS plays from the main menu: the prologue, the 3D world map (SMATRS's
  own `lib3d` map, camera zones, fast travel, the Lakitu drop-off), every
  Chapter 1 and 2 level, bonus, boss and special levels, saving and loading.
- Every SMATRS level has been entered and played for a while by script; the
  last random sample (both chapters, all three map sections) logged no
  script errors.
- Loading screens are SMATRS's own animated ones.
- Audio, shaders, lighting (`darkness`), parallax backgrounds, textplus text,
  custom NPC/block AI, P-switches, warps and the pause and message boxes.

Known gaps and what's left are tracked in `ROADMAP.md` (Phase 6 for the
episode, Phase 7 for Android).

## What we changed

Grouped by area; the details for each are in `CLAUDE.md`.

- **Build fixes** for macOS arm64: LuaJIT's debug library name, no
  `-pagezero_size` on Apple Silicon, range-checked array math in the bindings.
- **Submodule patches** (kept as patch files, since we can't push to those
  repos): `pge-file-library.patch` (extra level-file fields and `npc-N.txt`
  traits) and `luajit.patch` (Lua 5.2 syntax as LunaLua allows, a larger trace
  penalty cache).
- **Lua host** (`script/src/`): the level's Lua state and hooks
  (`xtech_lua_main.cpp`), the `mem()` bridge and LunaLua's object fields
  (`xtech_lua_bindings.cpp`), file formats, audio, the draw queue, loading
  screens; `luabind_num_t.h` so Lua can read positions and speeds.
- **Engine** (`src/`, marked `// smbx2compat:`): SMBX2's events at LunaLua's
  hook points, collision groups, P-switch slot reuse, sprite overrides,
  SMBX2's object limits (TheXTech's own clashing IDs moved above them), the
  960x540 screen SMATRS asks for, the renderer's script draws
  (`render_gl_script.cpp`), and many small fields SMBX2 scripts read through
  `mem()`.
- **Performance**: block/BGO lookups through the engine's quadtree, `mem()`
  reads through the FFI so LuaJIT can compile them, cached file reads and
  folder listings, shaders and their translations kept across levels, LuaJIT
  options for arm64. 1-1 runs at about 6 ms a frame on a Release build.
- **Fixes to episode crashes and errors**: `onLoop` Lua errors no longer
  crash the game; each distinct error is logged once with a traceback.
- **Its own app**: the macOS app is called *Ikoonat Launcher* ("tanooki"
  backwards), with its own bundle ID and Jake's leaf as its icon
  (`resources/ikoonat/`; `make_icons.py` makes every size from `leaf.png`).
  The build makes `bin/Ikoonat Launcher.app`.

## Setting up

Everything game-related lives outside the repo, in `../TheXTech-data/`
(never commit any of it):

| Path | What |
|---|---|
| `assets-smbx13/` | TheXTech's SMBX 1.3 asset pack |
| `assets-smbx13/smbx2` | symlink to SMBX2's `data` folder (`scripts/`, `graphics/`, `sound/`, `music/`, `costumes/`, `config/`) |
| `assets-smbx13/scripts/smbx2core` | symlink to this repo's `smbx2compat/core` |
| `userdir/` | the user folder: settings, saves, logs |
| `userdir/worlds/smbx/SMATRS Chapter 1+2/` | the episode (the main menu lists episodes from here) |
| `smoke/` | small test levels (see `CLAUDE.md`) |

The two symlinks are what turn core mode on:

```bash
D=../TheXTech-data
ln -s "$D/smbx2/SMBX2 + SMATRS/data" "$D/assets-smbx13/smbx2"
ln -s "$PWD/smbx2compat/core" "$D/assets-smbx13/scripts/smbx2core"
```

After a fresh clone, apply the submodule patches once:

```bash
git submodule update --init --recursive
git -C 3rdparty/PGE_File_Formats apply ../../smbx2compat/pge-file-library.patch
git -C 3rdparty/LuaJIT apply ../../smbx2compat/luajit.patch
```

## Building

Homebrew's cmake and ninja. Use Release for playing:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DTHEXTECH_ENABLE_LUA=ON
cmake --build build-release
```

A Debug build goes in `build/` (`-B build -DCMAKE_BUILD_TYPE=Debug`); it is
much slower and is used for the regression tests.

## Playing

```bash
smbx2compat/play.sh
```

Opens the game at its main menu with sound on (Release build, the data folder
above). Pick SMATRS and a save slot; slots 1 and 2 are for playing, slot 3 is
used by the automated tests. `XTECH_BUILD=build` plays the Debug build.

Logs go to `../TheXTech-data/userdir/logs/`. TheXTech prunes old files there,
so copy a log out soon if something went wrong. Script errors in it start
with `[smbx2core] ==>`.

## Testing

| Command | What |
|---|---|
| `smbx2compat/run-level.sh <level.lvlx \| World.wld> [seconds]` | one test run: muted, window moved to AeroSpace workspace 1, stops after N seconds and prints the log path. A `.wld` plays the episode in save slot 3. |
| `test/sweep/reg.sh <outdir>` | the regression set (smoke levels, SMATRS 1-1 and 2-1) with expected results |
| `test/sweep/sweep.sh <outdir> <seconds> <levels>` | many levels as level tests |
| `test/sweep/episode.sh <outdir> <seconds> <levels.txt>` | levels entered from SMATRS's map, as in real play |
| `cd test && luajit run.lua` | the v0.1 shim's mock harness |

Environment switches for a run (screenshots, profiling, debug files,
turning caches off) are listed in `CLAUDE.md` under "SMBX2 core mode".

## Files here

| File | What it is |
|---|---|
| `core/` | SMBX2 core mode: the emulated LunaLua host (`boot.lua` explains the start-up) |
| `scripts/smbx2compat.lua` | the v0.1 shim (fallback without SMBX2's data) |
| `smbx2compat-engine.patch` | the first round of engine changes, kept for reference (already applied) |
| `pge-file-library.patch`, `luajit.patch` | our edits inside the two submodules |
| `play.sh`, `run-level.sh` | play from the menu; one test run |
| `test/` | mock harness and the sweep/regression tools (`test/sweep/README.md`) |
| `ROADMAP.md` | the plan and the log of what was found and fixed |

## Licensing and AI

SMBX2's libraries and SMATRS (Nintendo-derived fan content) are never
committed or redistributed; users supply them. This repo is a fork of
[vkdfkc/TheXTech-Lua](https://github.com/vkdfkc/TheXTech-Lua) and is not
meant for upstream contributions. TheXTech is GPLv3, so anyone given a build
is entitled to its source.

This layer was written with Claude, an AI coding assistant. TheXTech's
`AGENTS.md` asks for no AI-generated code in TheXTech itself, so nothing
here is ever offered upstream; see the repository README for the note to
TheXTech's developers.
