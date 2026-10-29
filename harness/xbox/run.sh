#!/usr/bin/env bash
# Boot a build in xemu and capture COM1 (macOS, or Windows from Git Bash).
#   harness/xbox/run.sh [seconds] [stop-regex]
# Env:
#   OCX_ISO   GameCube disc image to pack next to default.xbe ("none" = omit,
#             to exercise the missing-disc screen). Never committed.
#   OCX_XBE   default: build-xbox/xbe/default.xbe
#   OCX_RUN   work dir (XISO + logs), default ~/xemu/run
#   OCX_GUI=1 leave xemu running (don't kill at timeout)
#   OCX_STAGE_EXTRA  dir whose contents are also packed onto the disc (e.g. a
#             save/card_a/*.gci: the XBE reads D:\ when E:\UDATA lacks it)
#   OCX_XEMU  the xemu binary (default: macOS's Xemu.app; on Windows
#             e.g. /c/xemu/xemu/xemu.exe)
#   OCX_XISO  a native extract-xiso instead of the docker image's (Windows
#             without Docker: /c/xdev/nxdk/tools/extract-xiso/build/extract-xiso.exe)
#   OCX_XEMU_ARGS  extra xemu arguments (-config_path <a 64 MB xemu.toml>)
# One xemu at a time: this refuses to start beside another (other projects
# share the PC; give each its own -config_path HDD copy too).
set -euo pipefail
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) export MSYS_NO_PATHCONV=1; win=1 ;; *) win=0 ;; esac
root="$(cd "$(dirname "$0")/../.." && pwd)"
secs="${1:-60}"; stop="${2:-__never__}"
xbe="${OCX_XBE:-$root/build-xbox/xbe/default.xbe}"
run="${OCX_RUN:-$HOME/xemu/run}"
iso="${OCX_ISO:-}"
if [ "$win" = 1 ]; then tasklist | grep -qi '^xemu.exe' && { echo "an xemu is running: wait for it"; exit 1; }
else pgrep -x xemu >/dev/null && { echo "an xemu is running: wait for it"; exit 1; }; fi
mkdir -p "$run/stage"
rm -rf "$run/stage/"*
cp "$xbe" "$run/stage/default.xbe"
[ -n "${OCX_STAGE_EXTRA:-}" ] && cp -R "$OCX_STAGE_EXTRA"/. "$run/stage/"
if [ -n "$iso" ] && [ "$iso" != none ]; then ln -f "$iso" "$run/stage/$(basename "$iso")" 2>/dev/null || cp "$iso" "$run/stage/"; fi
rm -f "$run/game.xiso"
if [ -n "${OCX_XISO:-}" ]; then
  (cd "$run" && "$OCX_XISO" -c stage game.xiso >/dev/null)
else
  docker run --rm -v "$run":/run opencrossing-xbox:sdk \
    /usr/src/nxdk/tools/extract-xiso/build/extract-xiso -c /run/stage /run/game.xiso >/dev/null
fi
log="$run/serial.log"; : > "$log"
xrun="$run"; [ "$win" = 1 ] && xrun="$(cd "$run" && pwd -W)"
"${OCX_XEMU:-/Applications/Xemu.app/Contents/MacOS/xemu}" -dvd_path "$xrun/game.xiso" \
  -device lpc47m157 -serial "file:$xrun/serial.log" ${OCX_XEMU_ARGS:-} > "$run/xemu.out" 2>&1 &
pid=$!
for ((i=0; i<secs; i++)); do
  sleep 1
  kill -0 $pid 2>/dev/null || break
  grep -qE "$stop" "$log" 2>/dev/null && break
done
[ "${OCX_GUI:-0}" = 1 ] || { kill $pid 2>/dev/null || true; wait $pid 2>/dev/null || true; }
echo "--- serial ($i s) ---"; tr -d '\r' < "$log"
