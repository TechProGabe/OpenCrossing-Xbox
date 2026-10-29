#!/usr/bin/env bash
# Regenerate xbox/ac.profdata (docs/pgo.md): build the instrumented image,
# play the training scenarios in xemu, turn each run's [PGOC] dump into a
# raw profile, merge, and write xbox/ac.profdata.txt (where it came from).
# From Melee-X's pgo_train.sh. Run from the checkout:
#
#   OCX_ISO=<GAFE01 image> OCX_CARD=<folder with a .gci town> tools/xbox/pgo_train.sh [scenario...]
#
# plus harness/xbox/run.sh's environment (OCX_XEMU, OCX_XISO on Windows,
# OCX_XEMU_ARGS with a 64 MB xemu config). OCX_CARD's .gci is a town to walk
# around in (never committed); without it the "town" scenario is skipped.
# Scenarios (autopad scripts written here, xbox/src/xbox_autopad.c):
#   demo     no input: the title screen's attract demos (towns, villagers)
#   newgame  A / START every 30 pad reads: a new town, the train, Rover, Nook
#   town     the save: into town, then walking in circles with A presses
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
scen=("$@")
[ ${#scen[@]} -gt 0 ] || scen=(demo newgame town)
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) build=tools/xbox/msys/build.sh; py=python ;; *) build=xbox/build.sh; py=python3 ;; esac
[ -n "${OCX_ISO:-}" ] || { echo "set OCX_ISO to your GAFE01 disc image"; exit 1; }

# one instrumented image for every scenario: scripted pads, saves from D:\,
# its own empty E:\UDATA folder, the full disc check skipped
XBOX_PGO=gen XBOX_CMAKE_ARGS="-DXBOX_AUTOPAD=script '-DCMAKE_C_FLAGS=-DXBOX_DBG_SAVE_FROM_D -DXBOX_DBG_FRESH_UDATA -DXBOX_DISC_CHECK=0'" $build
bdir=build-xbox-pgogen
work="$root/$bdir/pgo-train"; rm -rf "$work"; mkdir -p "$work"
cp $bdir/xbe/default.xbe $bdir/ac_xbox.exe $bdir/ac_xbox.map "$work/"

presses() {   # $1..$2: A every 30 calls, START every 8th press (xbox_autopad.c's auto mode)
  local c n=0
  for ((c=$1; c<$2; c+=30)); do
    if (( n++ % 8 == 7 )); then echo "$c START"; else echo "$c A"; fi
  done
}
walk() {      # $1..$2: the stick round a square, 150 calls a side, A between sides
  local c=$1
  while (( c < $2 )); do
    echo "$c STICK 0 100 150"; echo "$((c + 150)) STICK 100 0 150"
    echo "$((c + 300)) STICK 0 -100 150"; echo "$((c + 450)) STICK -100 0 150"
    echo "$((c + 600)) A"; c=$((c + 630))
  done
}

ran=()
for s in "${scen[@]}"; do
  st="$work/stage-$s"; mkdir -p "$st"
  case "$s" in
    demo) echo "# no input: the attract demos" > "$st/autopad.txt" ;;
    newgame) { echo "100 LOG newgame"; presses 600 12000; } > "$st/autopad.txt" ;;
    town)
      [ -n "${OCX_CARD:-}" ] || { echo "town: no OCX_CARD, skipped"; continue; }
      mkdir -p "$st/save/card_a"; cp "$OCX_CARD"/*.gci "$st/save/card_a/"
      { echo "100 LOG town"; presses 600 6500; walk 7000 16000; } > "$st/autopad.txt" ;;
    *) echo "unknown scenario $s"; exit 1 ;;
  esac
  # four dumps (7200 frames) is plenty; the last complete one is used
  OCX_XBE="$work/default.xbe" OCX_RUN="$work/run" OCX_STAGE_EXTRA="$st" \
    harness/xbox/run.sh 900 '\[PGOC\] end 4:' > "$work/$s.out" 2>&1 || true
  tr -d '\r' < "$work/run/serial.log" > "$work/$s.log"
  $py tools/xbox/pgo_raw.py --exe "$work/ac_xbox.exe" --map "$work/ac_xbox.map" "$work/$s.log" -o "$work/$s.profraw"
  ran+=("$s")
done
prof=(); for s in "${ran[@]}"; do prof+=("$work/$s.profraw"); done
pd=llvm-profdata; command -v $pd >/dev/null 2>&1 || pd=/c/msys64/mingw64/bin/llvm-profdata
"$pd" merge -o xbox/ac.profdata "${prof[@]}"
{
  echo "xbox/ac.profdata: PGO profile for XBOX_PGO=xbox/ac.profdata builds (docs/pgo.md)"
  echo "trained on: $(git rev-parse --short HEAD)$(git diff --quiet || echo ' + local changes') ($(git log -1 --format=%cs))"
  echo "scenarios: ${ran[*]} (xemu, autopad)"
  echo "llvm: $("$pd" --version 2>/dev/null | grep -io 'llvm version [0-9.]*' | head -1)"
  "$pd" show xbox/ac.profdata 2>/dev/null | grep -E '^(Total functions|Maximum function count|Total count)'
} > xbox/ac.profdata.txt
cat xbox/ac.profdata.txt
