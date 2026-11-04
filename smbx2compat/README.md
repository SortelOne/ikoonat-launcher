# smbx2compat v0.1 for TheXTech-Lua

An SMBX2 (LunaLua) API compatibility layer for the
[vkdfkc/TheXTech-Lua](https://github.com/vkdfkc/TheXTech-Lua) fork.

## Contents

| File | What it is |
|---|---|
| `smbx2compat-engine.patch` | C++ changes to the fork (apply with `git apply`) |
| `scripts/smbx2compat.lua` | The Lua shim, copied to `<assets>/scripts/` or an episode folder |
| `test/` | Mock harness; run `luajit run.lua` from inside `test/` |
| `core/` | SMBX2 core mode: an emulated LunaLua host for SMBX2's own scripts |
| `luajit.patch` | Adds a Lua 5.2 compat option to the LuaJIT submodule |
| `run-level.sh` | Test run: sound off, window to AeroSpace workspace 1, report survival |

## SMBX2 core mode (in progress)

The long-term path, replacing the shim: SMBX2's real `scripts/base/engine/main.lua`
and built-in libraries run unmodified, and `core/` plays the part of LunaLua's C++
(host tables, a Lua stand-in for `LunaDll.dll`, file lookup that copes with
Windows-style paths). It needs SMBX2's data folder at `<assets>/smbx2/` and `core/`
at `<assets>/scripts/smbx2core/`; without them the v0.1 shim below is used. Status
and the list of gaps are in `ROADMAP.md` (Phase 1).

## Engine patch

1. **`num_t` converter** (`script/src/luabind_num_t.h`). The fork builds with fixed-point
   math by default, and without this, every Lua read or write of a position or speed
   (`npc.Location.X`, `SpeedX`, `xtech_npc_create(...)`) fails with
   "Trying to use unregistered class: num_t". This was reproduced and the fix verified
   against the fork's bundled LuaJIT and luabind.
2. **Loader changes** (`xtech_lua_main.cpp`):
   - Loads `luna.lua` (SMBX2's name) as well as `level.lua` and `lunadll.lua`.
   - Runs an episode-wide `luna.lua` in every level, like SMBX2.
   - Loads the shim from the episode folder or `<assets>/scripts/smbx2compat.lua`.
   - Sets `require` paths to the level and episode folders.

## Supported in v0.1

`Player(n)`/`player`/`player2` (position, speed, powerup, mount, keys with
`KEYS_PRESSED` edges), `NPC.get`/`spawn`/`iterate`/`getIntersecting`, `npc:kill`,
`npc:harm`, `npc.data`, `registerEvent`/`unregisterEvent`, `onStart`, `onTick`,
`onTickEnd`, `onDraw`, `onDrawEnd`, `onExit`, `onExitLevel`, the NPC/player kill and
harm events, `Text.print`, `Graphics.loadImage`/`drawImage`/`draw`, `SFX.play`,
`Audio.MusicChange`, `camera`, `Section(n).boundary`, `Layer.get` (speed/stop),
`triggerEvent`, `Misc.dialog`/`score`/`coins`, `lunatime`, `Routine`, `SaveData`,
`GameData`, and `math.clamp`/`lerp`/`sign`.

Anything else logs **one** `[smbx2compat] not implemented: X` warning and keeps
going. Call `smbx2compat.report()` for the full list. Set `SMBX2COMPAT_STRICT = true`
to make missing features raise errors instead.

## Known limits

- The fork's events fire after the fact, so `eventObj.cancelled = true` is ignored.
- Not available: `mem()`, `player:harm()`/`kill()`, image opacity, `Graphics.drawBox`,
  shaders, render priority, layer show/hide, `onEvent`, alt-jump/alt-run keys,
  and SMBX2's basegame libraries (`npcManager`, `npcutils`, etc.).
- Values marked `VERIFY` in the shim (key constants, harm types, font mapping,
  `isOnGround`) come from memory of SMBX2 and need checking in play.

## Fork issue to know about

The fork's `3rdparty/PGE_File_Formats` submodule points at commit `b2e1cc5`, which
does not exist upstream, so a fresh clone can't fetch it. Use upstream
`PGE-File-Library-STL` and apply `pge-file-library.patch` inside the submodule
(`git -C 3rdparty/PGE_File_Formats apply ../../smbx2compat/pge-file-library.patch`).
It adds the fields the fork's engine code expects: `name` and `extx`/`exty` on
level objects, and the fork's extended `npc-N.txt` traits.
