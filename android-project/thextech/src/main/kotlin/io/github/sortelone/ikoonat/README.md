# Ikoonat Launcher's Android library

The Android app's first screen: a native episode library (Kotlin, Jetpack
Compose) that starts the game with the chosen episode. Decided with Jake on
2026-11-06 (smbx2compat/ROADMAP.md, Phase 7, "Native Android library"); the
Mac keeps the library drawn by the engine (`src/main/screen_library.cpp`).

## How it fits together

- `LibraryActivity` is the launcher activity. The game is upstream's
  `ru.wohlsoft.thextech.thextechActivity` (SDL), now in its own process
  (`android:process=":game"` in the manifest). The library starts it with
  the extra `ikoonat-args`, which `thextechActivity.getArguments()` appends
  to its own (TheXTech's settings: renderer, frame skip, ...):
  `-u <Ikoonat>/ -c <Ikoonat>/assets/ <world file> --save-slot N -n P -1 cC`
  (`GameLauncher.episodeArgs`). A world started that way quits when the
  player leaves it (`game_main.cpp`: `setup.testLevelMode && GameMenu`);
  thextechActivity's `onDestroy` exits its process and Android shows the
  library again, which refreshes in `onResume`.
- If the player leaves the game with Home instead of through its menu, the
  `:game` process stays alive with that episode (SDL can't start a second
  one in it): picking any episode, a shortcut or Classic menu then brings
  the running game back, with a note to leave its episode first
  (`GameLauncher.isGameRunning`).
- `--classic-menu` (engine) opens TheXTech's own menu instead of the
  in-engine library: Settings > Classic menu (battle mode, the editor).
- Files shared with the engine, in the visible `Ikoonat/` folder
  (`Paths.kt`; it is the game's user folder):
  - `settings/library.ini`: one group per episode, named like the engine's
    `Library_key` (the folder relative to `Ikoonat/`): mode, detected,
    last-played, last-slot, character, players, and `slotN-progress/stars/
    max-stars/time`, which the engine writes when an episode starts, saves
    and ends (`SaveSlots_recordInLibrary`), so this app never reads save
    files. `[library]`: theme, accent, background, swap-ab.
  - `settings/library/<key with [^A-Za-z0-9_-] bytes as _>.png`: the
    picture the engine takes ~45 s into play; `.cover.jpg`: the player's own
    picture (Episode page > Card picture), which wins.
  - `settings/gamesaves/<asset pack id>/<episode folder>/<world file>-save1.savx`
    and friends: the engine's saves (Erase and Back up saves use them, as
    `DeleteSave` does); SMBX2's `save1-ext.dat` lives in the episode folder.
  - `smbx2core/`: core mode's scripts, packed into the APK by
    `build.gradle` (`IkoonatCoreAssets`) and copied out by `CoreScripts`
    when the app is installed or updated.
- `Importer.kt` mirrors the Mac importer (`src/main/library_import.cpp`):
  zips through `java.util.zip`, folders through the system folder picker;
  SMBX2's data -> `smbx2/`, the asset pack -> `assets/`, episodes ->
  `worlds/<name>`.
- `PlayActivity` starts an episode straight away for home-screen shortcuts
  and frontends: extra `episode` (world file or folder path) or a
  `file://` URI, optional extra `slot`.

## Controller

The d-pad and left stick move focus (Android turns the stick into d-pad
presses); `LibraryActivity.dispatchKeyEvent` maps A to the d-pad's center
key (Compose's `clickable` takes it) and B to Back, swapped by Settings >
Confirm button. L1/R1 switch the filter, Start opens Settings. Everything
selectable is a `Pressable` (`ui/Widgets.kt`): it grows and gets an
accent ring while focused.

## Building

Built on Jake's machine (there is no Android SDK on the Mac this was
written on, so nothing here has been compiled before the first build).
New dependencies Gradle downloads: Kotlin's Gradle plugins (2.2.21),
Compose (BOM 2025.10.01), activity-compose, core-ktx,
lifecycle-viewmodel-compose. The app ID is `io.github.sortelone.ikoonat`
(`.debug` for debug builds), so it installs beside TheXTech. The native
build has Lua on and builds arm64-v8a and x86_64 only.

ES-DE/Daijisho: start `io.github.sortelone.ikoonat/io.github.sortelone.ikoonat.PlayActivity`
with the extra `episode` set to the world file's path.
