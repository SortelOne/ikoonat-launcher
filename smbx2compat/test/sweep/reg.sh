#!/bin/sh
# reg.sh <outdir>: the regression set on the Debug build (XTECH_BUILD to
# change): the smoke levels, then SMATRS 1-1 and 2-1. Prints each level's
# OK/WRONG checks, script errors and unknown mem() addresses. Expected:
# collisiontest 3 OK, actiontest 12, eventtest 24, pausetest 12; errtest 1
# error and bonus1 2 (on purpose); 1-1 and 2-1 none.
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
D=$REPO/../TheXTech-data
OUT=$1
mkdir -p "$OUT"
EP="$D/userdir/worlds/smbx/SMATRS Chapter 1+2"
run() { # name level secs
    LOG=$("$REPO/smbx2compat/run-level.sh" "$2" "$3" | tail -1)
    cp "$LOG" "$OUT/$1.txt"
    echo "$1: OK $(grep -c -- '-> OK' "$OUT/$1.txt") WRONG $(grep -c -- '-> WRONG' "$OUT/$1.txt") errors $(grep -c '==>' "$OUT/$1.txt") memgaps $(grep -c 'outside emulated memory' "$OUT/$1.txt")"
}
for t in collisiontest actiontest eventtest pausetest spritetest errtest bonus1; do run $t "$D/smoke/$t.lvlx" 30; done
run 1-1 "$EP/1-1 - Crash Landing.lvlx" 40
run 2-1 "$EP/2-1 - Warped Woods.lvlx" 40
echo REG DONE
