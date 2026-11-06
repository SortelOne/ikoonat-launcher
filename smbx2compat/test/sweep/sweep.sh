#!/bin/sh
# sweep.sh <outdir> <seconds> <level.lvlx>...
# Runs each level as a -l level test (Release build) with sweep.lua: hold
# right, jump now and then, a screenshot every 400 frames. Keeps each
# level's log and screenshots in <outdir>/<level>/ and prints its error count.
# SWEEP_DEBUG=<file.lua> runs another debug file instead.
# SWEEP_QUALITY=1|2|3 plays SMATRS at Low/Medium/High graphics (sweep.lua).
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
D=${XTECH_DATA:-$REPO/../TheXTech-data}
OUT=$1; SECS=$2; shift 2
mkdir -p "$OUT"
for L in "$@"; do
    N=$(basename "$L" .lvlx | tr ' ' '_')
    mkdir -p "$OUT/$N"
    LOG=$(XTECH_BUILD=${XTECH_BUILD:-build-release} SMBX2COMPAT_DEBUG=${SWEEP_DEBUG:-$HERE/sweep.lua} "$REPO/smbx2compat/run-level.sh" "$L" "$SECS" | tail -1)
    cp "$LOG" "$OUT/$N/log.txt"
    cp "$D"/userdir/logs/smbx2shot-*.png "$OUT/$N/" 2>/dev/null
    echo "$N: $(grep -c '==>' "$OUT/$N/log.txt") errors"
done
echo SWEEP DONE
