#!/usr/bin/env bash
# Build OpenCrossing-Xbox inside the SDK image. Output: build-xbox/xbe/default.xbe
#   XBOX_TARGET=objs  compile every TU, no link (triage)
#   XBOX_CMAKE_ARGS   extra cmake args; quote multi-flag values for the inner shell:
#                     XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=-DA -DB'"
# Every option is reset to its default on each run (CMake caches -D values),
# so a knob from an earlier test build never leaks into the next one.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
bdir="build-xbox${XBOX_TARGET:+-$XBOX_TARGET}"
objs=OFF; [ "${XBOX_TARGET:-}" = objs ] && objs=ON
docker run --rm -v "$root":/src -w /src opencrossing-xbox:sdk bash -c "
  set -e
  eval \$(/usr/src/nxdk/bin/activate -s)
  cmake -S xbox -B $bdir -G Ninja -DCMAKE_TOOLCHAIN_FILE=/usr/src/nxdk/share/toolchain-nxdk.cmake -DXBOX_OBJS_ONLY=$objs -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS= -DXBOX_AUTOPAD= -DXBOX_WIDESCREEN=ON -DXBOX_TITLE_MENU=ON ${XBOX_CMAKE_ARGS:-} >/dev/null
  ninja -C $bdir -k 0 ${XBOX_NINJA_ARGS:-}
  # the container runs as root; on a Linux host the output would stay root-owned
  chown -R $(id -u):$(id -g) $bdir 2>/dev/null || true"
# Dashboard icon: $$XTIMAGE section + default.tbn (host python3 + Pillow).
xbe="$root/$bdir/xbe/default.xbe"
if [ -z "${XBOX_NO_ICON:-}" ] && [ -f "$xbe" ] && python3 -c "import PIL" 2>/dev/null; then
  python3 "$root/tools/xbox/xbe_title_image.py" "$xbe" "$root/xbox/assets/logo.png" "$root/$bdir/xbe/default.tbn"
fi
