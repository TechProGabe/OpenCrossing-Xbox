#!/usr/bin/env bash
# Build OpenCrossing-Xbox inside the SDK image. Output: build-xbox/xbe/default.xbe
#   XBOX_TARGET=objs  compile every TU, no link (triage)
#   XBOX_CMAKE_ARGS   extra cmake args; quote multi-flag values for the inner shell:
#                     XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=-DA -DB'"
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
bdir="build-xbox${XBOX_TARGET:+-$XBOX_TARGET}"
objs=OFF; [ "${XBOX_TARGET:-}" = objs ] && objs=ON
docker run --rm -v "$root":/src -w /src opencrossing-xbox:sdk bash -c "
  set -e
  eval \$(/usr/src/nxdk/bin/activate -s)
  cmake -S xbox -B $bdir -G Ninja -DCMAKE_TOOLCHAIN_FILE=/usr/src/nxdk/share/toolchain-nxdk.cmake -DXBOX_OBJS_ONLY=$objs -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS= ${XBOX_CMAKE_ARGS:-} >/dev/null
  ninja -C $bdir -k 0 ${XBOX_NINJA_ARGS:-}"
