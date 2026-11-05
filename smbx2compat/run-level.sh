#!/bin/sh
# Run a level for testing: sound off (-s), window moved to AeroSpace workspace 1
# (if AeroSpace is running), then stop after N seconds and report whether it survived.
#
#   smbx2compat/run-level.sh <level.lvlx | world.wld> [seconds=40]
#
# A world file is played as an episode (as if picked from the menu) in save slot
# XTECH_SAVE_SLOT (default 3, left for tests; 1-2 are for playing); a level test
# is slot 0, which SMBX2 treats as the editor.
#
# Data dir defaults to ../TheXTech-data (override with XTECH_DATA). The newest log
# is printed at the end. Set XTECH_WORKSPACE to use another workspace, and
# XTECH_BUILD to run another build folder (e.g. build-release). XTECH_AUDIO=on plays
# sound at the test userdir's volumes (settings/thextech.ini [audio], kept low);
# XTECH_AUDIO=dummy sends it to SDL's silent dummy device (tests the mixer quietly).
# XTECH_ARGS adds options (e.g. -x: no frame limit, so long cutscenes pass sooner).

set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
D=${XTECH_DATA:-$REPO/../TheXTech-data}
LEVEL=$(cd "$(dirname "$1")" && pwd)/$(basename "$1") # absolute, as LunaLua's paths are
SECS=${2:-40}
WS=${XTECH_WORKSPACE:-1}
BIN=$REPO/${XTECH_BUILD:-build}/bin/TheXTech.app/Contents/MacOS/TheXTech
SLOT=
case "$LEVEL" in
    *.wld|*.wldx) SLOT="--save-slot ${XTECH_SAVE_SLOT:-3}" ;;
esac

rm -f "$D"/userdir/logs/*.txt "$D"/userdir/logs/smbx2shot-*.png "$D"/userdir/screenshots/*.png
if [ "${XTECH_AUDIO:-}" = "dummy" ]; then
    SDL_AUDIODRIVER=dummy "$BIN" -c "$D/assets-smbx13" -u "$D/userdir" -l "$LEVEL" $SLOT ${XTECH_ARGS:-} >/dev/null 2>&1 &
elif [ "${XTECH_AUDIO:-}" = "on" ]; then
    "$BIN" -c "$D/assets-smbx13" -u "$D/userdir" -l "$LEVEL" $SLOT ${XTECH_ARGS:-} >/dev/null 2>&1 &
else
    "$BIN" -s -c "$D/assets-smbx13" -u "$D/userdir" -l "$LEVEL" $SLOT ${XTECH_ARGS:-} >/dev/null 2>&1 &
fi
PID=$!

# Move the game window out of the way as soon as it appears.
if command -v aerospace >/dev/null 2>&1; then
    i=0
    while [ $i -lt 100 ] && kill -0 $PID 2>/dev/null; do
        WID=$(aerospace list-windows --monitor all --pid $PID --format '%{window-id}' 2>/dev/null | head -1)
        if [ -n "$WID" ]; then
            aerospace move-node-to-workspace --window-id "$WID" "$WS" 2>/dev/null
            break
        fi
        sleep 0.1
        i=$((i + 1))
    done
fi

sleep "$SECS"
if kill -0 $PID 2>/dev/null; then
    echo "ALIVE after ${SECS}s"
    kill $PID
    # A crashed game can sit in its crash dialog and ignore SIGTERM
    i=0
    while [ $i -lt 50 ] && kill -0 $PID 2>/dev/null; do
        sleep 0.1
        i=$((i + 1))
    done
    if kill -0 $PID 2>/dev/null; then
        echo "(didn't stop; killed. Check the log for a crash.)"
        kill -9 $PID
    fi
    wait $PID 2>/dev/null
else
    wait $PID
    echo "EXITED early with status $?"
fi
ls -t "$D"/userdir/logs/*.txt 2>/dev/null | head -1
