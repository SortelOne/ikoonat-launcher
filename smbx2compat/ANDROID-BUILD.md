# Building the Android APK (Jake's Windows PC)

Build inside **WSL2 (Ubuntu)**, not in Windows itself: LuaJIT's Android
build compiles two helper programs (`minilua`, `buildvm`) for the computer
doing the build, and its CMake looks for a Unix `gcc`, which WSL has and
Windows doesn't. Line endings and paths also behave there as on the Mac.
Keep the clone in WSL's own disk (`~/...`), not under `/mnt/c` (slow, and
file permissions get lost).

The APK has no game content: the asset pack, SMBX2's data and SMATRS are
imported on the Odin from the app (Import), as in
`android-project/thextech/src/main/kotlin/io/github/sortelone/ikoonat/README.md`.

## 1. WSL and tools (once)

In PowerShell as administrator, then restart Windows:

```powershell
wsl --install -d Ubuntu
```

In Ubuntu:

```bash
sudo apt update
sudo apt install -y git gh build-essential cmake ninja-build openjdk-17-jdk unzip python3
```

Android's SDK, command-line tools only (no Android Studio needed): from
https://developer.android.com/studio#command-line-tools-only take the
Linux zip, then:

```bash
mkdir -p ~/Android/Sdk/cmdline-tools
unzip ~/commandlinetools-linux-*.zip -d ~/Android/Sdk/cmdline-tools
mv ~/Android/Sdk/cmdline-tools/cmdline-tools ~/Android/Sdk/cmdline-tools/latest
echo 'export ANDROID_HOME=$HOME/Android/Sdk' >> ~/.bashrc
echo 'export PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH' >> ~/.bashrc
source ~/.bashrc
yes | sdkmanager --licenses
sdkmanager "platform-tools" "platforms;android-36" "build-tools;36.0.0" "ndk;23.2.8568313" "cmake;3.22.1"
```

(Gradle installs anything else it wants itself once the licenses are
accepted. NDK 23 is TheXTech's choice; `thextech/build.gradle` names it.)

## 2. The repo (once)

Sign in to GitHub first (the repo is public, but pushing needs it):

```bash
gh auth login
gh repo clone SortelOne/ikoonat-launcher ~/TheXTech-Lua -- --recurse-submodules
cd ~/TheXTech-Lua
git config user.name "Jake Bales"
git config user.email 19493690+SortelOne@users.noreply.github.com
```

(The no-reply address: GitHub refuses pushes that would expose the
account's own email.) Our edits inside two submodules (LuaJIT,
PGE_File_Formats) are applied by CMake on the first build
(`cmake/smbx2compat_patches.cmake`; the build output says
`smbx2compat: applied ...`), so there's nothing to do by hand.

## 3. Build

```bash
cd ~/TheXTech-Lua/android-project
./gradlew assembleApkRelease
```

The first build takes a while (Gradle, every C/C++ library, for arm64-v8a
and x86_64). Release, not Debug: Debug's native code is unoptimised and
far too slow for SMATRS. The Release APK is signed with this computer's
debug key, so it installs straight away:
`thextech/build/outputs/apk/apk/release/thextech-apk-release.apk`.

Before the next build, keep that build's native library with debug info,
so crash logs from the Odin can be read (raw addresses -> file:line):

```bash
mkdir -p ~/ikoonat-symbols
find thextech/build/intermediates/merged_native_libs -path '*arm64-v8a*' -name libthextech.so \
  -exec cp {} ~/ikoonat-symbols/libthextech-$(git rev-parse --short HEAD).so \; -quit
```

## 4. Install on the Odin

Copy the APK to the Odin (USB, cloud drive, ...) and open it there
(allow installs from that app when asked). Or over USB/Wi-Fi debugging
with `adb install -r <apk>` from WSL (`platform-tools` above). An APK
built on another computer is signed with another key: uninstall first
(the `Ikoonat/` folder with imports and saves stays).

## 5. When something fails

- Build errors: the Kotlin library has never been compiled before this
  machine; expect a few rounds. Paste the first error (Gradle prints the
  file and line) to Claude.
- Problems on the Odin: the app's Settings > Share the latest log; put it
  in the Mac's `../TheXTech-data/odin-logs/` or paste it.

## Claude Code on this PC

Install it inside WSL and start it in `~/TheXTech-Lua`; it reads
`CLAUDE.md` (project notes) and `smbx2compat/ROADMAP.md` (the plan). What
it can't do there: run the game on the desktop (`run-level.sh` and the
test sweeps need the Mac's `../TheXTech-data`). Both computers push to
`main`: `git pull` before starting and push when done, so the Mac's
sessions see the fixes.
