#!/bin/sh
# Play: opens the game at its main menu with sound on, using the test data
# (asset pack and user folder in ../TheXTech-data). Episodes in
# userdir/worlds/smbx/ are listed; SMATRS is linked there:
#   ln -s "<data>/smatrs/SMATRS Chapter 1+2" "<data>/userdir/worlds/smbx/"
#
#   smbx2compat/play.sh [extra TheXTech options]
#
# Release build by default (XTECH_BUILD=build for Debug); XTECH_DATA as in run-level.sh.

set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
D=${XTECH_DATA:-$REPO/../TheXTech-data}
BIN="$REPO/${XTECH_BUILD:-build-release}/bin/Ikoonat Launcher.app/Contents/MacOS/Ikoonat Launcher"

exec "$BIN" -c "$D/assets-smbx13" -u "$D/userdir" "$@"
