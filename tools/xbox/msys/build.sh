#!/usr/bin/env bash
# Build default.xbe natively on Windows with MSYS2 (no Docker, no WSL).
# Same steps as xbox/build.sh's container, same knobs:
#   tools/xbox/msys/build.sh                        # -> build-xbox/xbe/default.xbe
#   XBOX_BUILD_DIR=build-xbox-foo tools/xbox/msys/build.sh
#   XBOX_TARGET=objs tools/xbox/msys/build.sh       # compile every TU, no link
#   XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=-DA -DB'" tools/xbox/msys/build.sh
#   XBOX_LTO=1 XBOX_PGO=xbox/ac.profdata tools/xbox/msys/build.sh  # -> build-xbox-lto-pgo
#   XBOX_PGO=gen: the instrumented build (docs/pgo.md)
# One-time setup (docs/toolchain.md "Windows"), shared with Prime-X / Melee-X:
#   MSYS2 at C:\msys64 with
#     pacman -S make git bison flex cmake ninja mingw-w64-x86_64-clang \
#       mingw-w64-x86_64-lld mingw-w64-x86_64-llvm mingw-w64-x86_64-python \
#       mingw-w64-x86_64-python-pillow mingw-w64-x86_64-gcc
#   (not mingw-w64-x86_64-cmake/ninja: they can't run nxdk's shell wrappers)
#   nxdk at the Dockerfile's NXDK_SHA in $NXDK_DIR (default /c/xdev/nxdk),
#   built with `make tools` and `make NXDK_ONLY=y NXDK_SDL=y NXDK_CXX=y`.
# MSYS2's clang is LLVM 21, the docker image's version.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
msys="${MSYS2_ROOT:-/c/msys64}"
[ -x "$msys/usr/bin/bash" ] || { echo "no MSYS2 at $msys"; exit 1; }
# Git Bash's environment doesn't reach MSYS2's bash: hand the knobs over as arguments
knobs=()
for v in XBOX_LTO XBOX_PGO XBOX_BUILD_DIR XBOX_TARGET XBOX_CMAKE_ARGS XBOX_NINJA_ARGS XBOX_NO_ICON NXDK_DIR; do
  [ -n "${!v:-}" ] && knobs+=("$v=${!v}")
done
exec "$msys/usr/bin/bash" -lc '
  set -euo pipefail
  export PATH="$1/tools/xbox/msys:/usr/bin:/mingw64/bin:$PATH"
  export NXDK_DIR="${NXDK_DIR:-/c/xdev/nxdk}" PYTHONUTF8=1
  root="$1"; shift; cd "$root"
  for kv in "$@"; do export "$kv"; done
  kind="${XBOX_LTO:+-lto}"; case "${XBOX_PGO:-}" in "") ;; gen) kind="$kind-pgogen" ;; *) kind="$kind-pgo" ;; esac
  bdir="${XBOX_BUILD_DIR:-build-xbox$kind${XBOX_TARGET:+-$XBOX_TARGET}}"
  lto=OFF; [ -n "${XBOX_LTO:-}" ] && lto=ON
  objs=OFF; [ "${XBOX_TARGET:-}" = objs ] && objs=ON
  eval "$("$NXDK_DIR/bin/activate" -s)"
  eval cmake -S xbox -B "$bdir" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" \
    -DXBOX_OBJS_ONLY=$objs -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS= -DXBOX_AUTOPAD= -DXBOX_WIDESCREEN=ON \
    -DXBOX_TITLE_MENU=ON -DXBOX_DIAG_ISSUE3=OFF -DXBOX_RTC_SHIM=OFF     -DXBOX_LTO=$lto -DXBOX_PGO="${XBOX_PGO:-}" ${XBOX_CMAKE_ARGS:-} >/dev/null
  ninja -C "$bdir" -k 0 ${XBOX_NINJA_ARGS:-}
  xbe="$bdir/xbe/default.xbe"
  if [ -z "${XBOX_NO_ICON:-}" ] && [ -f "$xbe" ]; then
    python3 tools/xbox/xbe_title_image.py "$xbe" xbox/assets/logo.png "$bdir/xbe/default.tbn"
  fi
  [ -f "$xbe" ] && python3 tools/xbox/xbe_payload.py "$xbe"   # boot screen art
  true' _ "$root" "${knobs[@]}"
