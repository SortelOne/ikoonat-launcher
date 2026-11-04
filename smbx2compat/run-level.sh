#!/bin/sh
# Run a level for testing: sound off (-s), window moved to AeroSpace workspace 1
# (if AeroSpace is running), then stop after N seconds and report whether it survived.
#
#   smbx2compat/run-level.sh <level.lvlx> [seconds=40]
#
# Data dir defaults to ../TheXTech-data (override with XTECH_DATA). The newest log
# is printed at the end. Set XTECH_WORKSPACE to use another workspace, and
# XTECH_BUILD to run another build folder (e.g. build-release).

set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
D=${XTECH_DATA:-$REPO/../TheXTech-data}
LEVEL=$(cd "$(dirname "$1")" && pwd)/$(basename "$1") # absolute, as LunaLua's paths are
SECS=${2:-40}
WS=${XTECH_WORKSPACE:-1}
BIN=$REPO/${XTECH_BUILD:-build}/bin/TheXTech.app/Contents/MacOS/TheXTech

rm -f "$D"/userdir/logs/*.txt
"$BIN" -s -c "$D/assets-smbx13" -u "$D/userdir" -l "$LEVEL" >/dev/null 2>&1 &
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
