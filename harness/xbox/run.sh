#!/usr/bin/env bash
# Boot a build in xemu and capture COM1.
#   harness/xbox/run.sh [seconds] [stop-regex]
# Env:
#   OCX_ISO   GameCube disc image to pack next to default.xbe ("none" = omit,
#             to exercise the missing-disc screen). Never committed.
#   OCX_XBE   default: build-xbox/xbe/default.xbe
#   OCX_RUN   work dir (XISO + logs), default ~/xemu/run
#   OCX_GUI=1 leave xemu running (don't kill at timeout)
#   OCX_STAGE_EXTRA  dir whose contents are also packed onto the disc (e.g. a
#             save/card_a/*.gci: the XBE reads D:\ when E:\UDATA lacks it)
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
secs="${1:-60}"; stop="${2:-__never__}"
xbe="${OCX_XBE:-$root/build-xbox/xbe/default.xbe}"
run="${OCX_RUN:-$HOME/xemu/run}"
iso="${OCX_ISO:-}"
mkdir -p "$run/stage"
rm -rf "$run/stage/"*
cp "$xbe" "$run/stage/default.xbe"
[ -n "${OCX_STAGE_EXTRA:-}" ] && cp -R "$OCX_STAGE_EXTRA"/. "$run/stage/"
if [ -n "$iso" ] && [ "$iso" != none ]; then ln -f "$iso" "$run/stage/$(basename "$iso")" 2>/dev/null || cp "$iso" "$run/stage/"; fi
rm -f "$run/game.xiso"
docker run --rm -v "$run":/run opencrossing-xbox:sdk \
  /usr/src/nxdk/tools/extract-xiso/build/extract-xiso -c /run/stage /run/game.xiso >/dev/null
log="$run/serial.log"; : > "$log"
/Applications/Xemu.app/Contents/MacOS/xemu -dvd_path "$run/game.xiso" \
  -device lpc47m157 -serial "file:$log" ${OCX_XEMU_ARGS:-} > "$run/xemu.out" 2>&1 &
pid=$!
for ((i=0; i<secs; i++)); do
  sleep 1
  kill -0 $pid 2>/dev/null || break
  grep -qE "$stop" "$log" 2>/dev/null && break
done
[ "${OCX_GUI:-0}" = 1 ] || { kill $pid 2>/dev/null || true; wait $pid 2>/dev/null || true; }
echo "--- serial ($i s) ---"; tr -d '\r' < "$log"
