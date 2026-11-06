# Sweep tools

Scripts for checking SMATRS (or any SMBX2 episode) in core mode many levels at
a time. They need the game data next to the repo (`../TheXTech-data`, see
`CLAUDE.md`) and the Release build (`build-release`) unless `XTECH_BUILD` says
otherwise; like `run-level.sh`, they run the game muted on AeroSpace
workspace 1, and from Claude's shell they need the sandbox off.

- `sweep.sh <outdir> <seconds> <level.lvlx>...` — each level as a `-l` level
  test with `sweep.lua` (hold right, jump now and then, a screenshot every 400
  frames). `-l` tests count as the editor, so episode-only scenes are skipped.
- `episode.sh <outdir> <seconds> <levels.txt>` — plays the episode from
  `World.wld` in save slot 3 and enters the listed levels from the map
  (`epsweep.lua`; the list format is in its header). Slot 3's SMBX2 save is
  put back afterwards. Example list: `levels-sample-2026-11-06.txt` (the
  random sample from ROADMAP.md Phase 6).
- `SWEEP_QUALITY=1|2|3` (with `sweep.sh`) plays SMATRS at its Low, Medium or
  High graphics setting (`SaveData.general.graphicalQuality`).
- `SMBX2COMPAT_CONFIG="draw-rate=2,show-frame-time=1"` (any of these
  scripts, or `run-level.sh`) sets TheXTech options for the run only, by
  their ini names (enums by number), without changing the user folder's
  settings file.
- `reg.sh <outdir>` — the regression set on the Debug build, with the
  expected results in its header.
- `blackrects.py <png>...` — finds solid black boxes in screenshots (the
  kind of drawing bug 2-A's barrels had). Dark artwork shows up too, so look
  at what it flags.
- `sheet.py <out.png> <cols> <png>...` — tiles screenshots into one image.
- `exittest.lua` — a `SMBX2COMPAT_DEBUG` file that calls `Misc.exitEngine()`
  (SMATRS's Quit) after `EXITTEST_FRAME` frames (default 300) in levels
  whose name contains `EXITTEST_LEVEL`. Episode restart loop through the
  library, for memory checks (Continue is slot 3 once slot 3 was played
  last; back up and restore `save3-ext.dat` as `episode.sh` does):
  `SMBX2COMPAT_DEBUG=$PWD/exittest.lua EXITTEST_FRAME=400 EXITTEST_LEVEL="World Map" SMBX2COMPAT_LIBKEYS="60:do,100:do" XTECH_BUILD=build-release run-level.sh menu 240`
  (the library's test keys start again each time it opens).
- `lsprobe.lua` — finds what keeps old loading screens alive in that
  state; needs a temporary hook in `core/loadscreen.lua` (in its header).

Reading a run: errors are the `==>` lines; `MemEmu: Unknown` and
`outside emulated memory` are fields scripts use that the engine doesn't
emulate yet; a log of thousands of lines means something logs every frame.
Logs and screenshots are copied out of the user folder right after each run,
since TheXTech prunes old files there.
