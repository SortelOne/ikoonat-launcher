#!/bin/sh
# episode.sh <outdir> <seconds> <levels.txt>
# Plays SMATRS from World.wld in save slot 3 (Release build) with epsweep.lua,
# visiting the levels in <levels.txt> (format: epsweep.lua's header). Slot 3's
# SMBX2 save (save3-ext.dat) is copied to <outdir> first and put back after,
# so runs start alike. Keeps the log and screenshots in <outdir> and prints
# the sweep's progress, errors and MemEmu warnings.
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
D=${XTECH_DATA:-$REPO/../TheXTech-data}
E="$D/userdir/worlds/smbx/SMATRS Chapter 1+2"
OUT=$1; SECS=$2; LIST=$3
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cp "$HERE/epsweep.lua" "$OUT/epsweep.lua"
cp "$LIST" "$OUT/levels.txt"
[ -f "$E/save3-ext.dat" ] && cp "$E/save3-ext.dat" "$OUT/save3-before.dat"
LOG=$(XTECH_BUILD=${XTECH_BUILD:-build-release} SMBX2COMPAT_DEBUG="$OUT/epsweep.lua" "$REPO/smbx2compat/run-level.sh" "$E/World.wld" "$SECS" | tail -1)
[ -f "$OUT/save3-before.dat" ] && cp "$OUT/save3-before.dat" "$E/save3-ext.dat"
cp "$LOG" "$OUT/log.txt"
cp "$D"/userdir/logs/smbx2shot-*.png "$OUT/" 2>/dev/null
grep "epsweep:\|==>\|MemEmu: \(Unknown\|Read type\|Write type\)" "$OUT/log.txt" | sed 's/.*smbx2core\] //;s/.*\]: //' | grep -v "frame [0-9]*00 "
echo "log: $(wc -l < "$OUT/log.txt") lines, $(ls "$OUT"/smbx2shot-*.png 2>/dev/null | wc -l) screenshots"
