#!/bin/bash
# In-engine check of the refraction and journal work, in one command.
#
# Builds the game module, then plays three short sessions that shoot by themselves
# and quit, and reads their logs. It uses Unreal (build tool and engine), so only
# run it when no other project needs the editor.
#
#   solver    -hlautoshoot                aim solved by simulating the arrow (the old test)
#   image     -hlautoshoot -hlaimimage    aim straight at the fish's image, as a player would
#   straight  ... -hlstraightarrow        same, with an arrow that does not bend
#
# Expect: solver lands some; image lands most short shots (the arrow bends like the
# light, so the image is where to aim); straight lands fewer than image (aiming at
# the image goes high). Every hit should leave a "landed" line in that run's journal.
#
# Usage: game/Tests/play_test.sh [seconds per run, default 60]
set -euo pipefail

UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
game="$(cd "$(dirname "$0")/.." && pwd)"
proj="$game/HangingLake.uproject"
editor="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
secs="${1:-60}"
out="$game/Saved/PlayTest/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out"

echo "== build"
"$UE/Engine/Build/BatchFiles/Mac/Build.sh" HangingLakeEditor Mac Development -Project="$proj" \
  > "$out/build.log" 2>&1 || { tail -30 "$out/build.log"; echo "build failed: $out/build.log"; exit 1; }

play() {
  local name=$1; shift
  echo "== $name ($secs s)"
  "$editor" "$proj" -game -windowed -ResX=960 -ResY=540 -nosound -unattended \
    -hljournal="$out/$name-journal.json" -hlquit="$secs" -hlshootat=25 \
    -abslog="$out/$name.log" "$@" > /dev/null 2>&1 || true
}

play solver   -hlautoshoot
play image    -hlautoshoot -hlaimimage
play straight -hlautoshoot -hlaimimage -hlstraightarrow

echo
printf '%-9s %5s %6s %8s %7s\n' run hits misses journal errors
for name in solver image straight; do
  log="$out/$name.log"
  [ -f "$log" ] || { echo "$name: no log (did the game start?)"; continue; }
  hits=$(grep -c 'HLSHOOT HIT' "$log" || true)
  misses=$(grep -c 'HLSHOOT MISS' "$log" || true)
  journal=$(grep -c 'HLJOURNAL' "$log" || true)
  errors=$(grep -cE 'Error:|Ensure condition failed|Assertion failed' "$log" || true)
  printf '%-9s %5s %6s %8s %7s\n' "$name" "$hits" "$misses" "$journal" "$errors"
done
echo
grep -h 'HLSHOOT aiming\|HLSHOOT target\|HLJOURNAL' "$out"/*.log | sed 's/^.*LogTemp: Warning: //' | head -40
echo
echo "logs and journals: $out"
