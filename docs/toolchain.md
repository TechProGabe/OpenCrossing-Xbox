# Toolchain, testing and releases

## Build

```sh
xbox/build-image.sh             # once: SDK image opencrossing-xbox:sdk (~10 min cold)
xbox/build.sh                   # -> build-xbox/xbe/default.xbe + default.tbn
XBOX_TARGET=objs xbox/build.sh  # compile every TU, no link (triage)
```

- nxdk (https://github.com/XboxDev/nxdk), pinned in `xbox/docker/Dockerfile`,
  built from source on Debian trixie with LLVM 21. Upstream images are
  amd64/386 only; ours builds natively on arm64 and amd64.
- Flags carried from the PC port: `-O2 -fno-strict-aliasing -fwrapv`,
  `-DTARGET_PC -DTARGET_XBOX`, i386.
- The dashboard icon (`$$XTIMAGE` section + `default.tbn`) is added after the
  link by `tools/xbox/xbe_title_image.py` from `xbox/assets/logo.png`
  (drawn by `tools/xbox/make_logo.py`). Needs host `python3` + Pillow;
  `XBOX_NO_ICON=1` skips it.
- Extra compile flags: `XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=-DA -DB'"` (quoted
  for the inner shell). Debug knobs are listed in `renderer.md`.
- macOS with colima: Docker only sees your home directory, so keep the
  checkout under `~`. No BuildKit on colima: `DOCKER_BUILDKIT=0`.

## Run in xemu

```sh
OCX_ISO=/path/to/AnimalCrossing.iso harness/xbox/run.sh 120 "stop-regex"
tools/xbox/fbdump_to_png.py ~/xemu/run/serial.log out   # [FBDUMP] -> PNG
```

`run.sh` packs the XBE and the image into an XISO, boots it with COM1
logged to `~/xemu/run/serial.log`, and stops at the regex or the timeout.
`OCX_STAGE_EXTRA=<dir>` adds files to the disc (for example a
`save/card_a/*.gci`; build with `-DXBOX_DBG_SAVE_FROM_D` to read saves from
`D:\`). xemu needs your own MCPX ROM, BIOS and HDD image, set to 64 MB.
xemu has no screenshot command; use `-DXBOX_FBDUMP_EVERY=N`.

xemu is not hardware: it never plays AC97 on macOS, has no CPU cache model,
and hides timing bugs. Judge performance and hangs on a real console. Its
timing also follows the host: on a busy Mac (check `uptime`) two runs of
the same build differed 2x, so A/B timings there need an idle host. An
`XBOX_FBDUMP_EVERY` frame takes seconds (the dump goes over serial) and
shows up as a `[HITCH]`.

Scripted menu tests: build with `XBOX_CMAKE_ARGS="-DXBOX_AUTOPAD=script"`
and stage an `autopad.txt` with `OCX_STAGE_EXTRA` (format in the header of
`xbox/src/xbox_autopad.c`; `SHOT` steps give `[FBDUMP]` screenshots). With a
save, add `'-DCMAKE_C_FLAGS=-DXBOX_DBG_SAVE_FROM_D'` and stage
`save/card_a/*.gci` (`tools/gcs_to_gci.py` converts a `.gcs`). Town is
reached after 4000-6000 PADRead calls, depending on the date's events.

720p in xemu: xemu's default AV pack is HDTV, but the EEPROM must allow
720p. Copy `eeprom.bin`, set the video flags at 0x94 and fix the checksum
(`traps.md`), point a copy of `xemu.toml` at it, and run with
`OCX_XEMU_ARGS="-config_path <copy>/xemu.toml"`.

## Test on a real Xbox

Deploy over FTP to any folder the dashboard lists (we use
`F:\Applications\OpenCrossing\`). Real hardware has no serial port, so the
game writes logs to `E:\UDATA\4f430001\`:

| file | written |
|---|---|
| `boot.log` | every log line: flushed per line until frame 120, then queued and written by the watchdog once a second. The first 4 MB; then `boot2.log` and `boot3.log` in turn, 2 MB each (all deleted at boot). `[BEAT]` every 5 s (vblank count, frames presented, free KB), `[FRAME]` every 5 s, `[PROF]` in profiler builds |
| `last.log` | rewritten by the watchdog within 3 s of anything being logged (and every 30 s): the last 4 KB of log plus a `[STATE]` line (renderer, pushbuffer, texture pool, GPU faults). After a hard freeze it holds the seconds before it |
| `crash.log` | written when a CPU exception (page fault, ...) hits a game thread: fault address, registers, `[STATE]`, stack words; the same report is drawn on screen |
| `perf.log` | once a minute a `min N` line: fps, CPU ms, frames over 17 (missed vblank) / 33 / 100 ms, pushbuffer peak, texture pool use, free RAM, GPU faults. Above each, indented, that minute's `[HITCH]` lines of 100 ms and over, `[PACE]` and `[NES]` lines (written every 15 s) |
| `hang.log` | by the watchdog when frames stop for 6 s (or none in 90 s after boot): log tail + every thread's stack words; the same report is drawn on screen |
| `input.log` | left-stick swings over 120° in one frame |
| `stickN.log` | last 10 s of stick readings, written when L3 is clicked |

`tools/xbox/console.py` does a round in three steps (`OCX_FTP_HOST=<ip>`):
`stage vNN` copies the build and its map (plus static functions) to
`~/xemu/ochw`, `deploy vNN` pulls and deletes the console's old logs,
keeps the replaced XBE as `default.xbe.prev` and uploads with a read-back
check, `pull vNN` fetches the logs and `settings.ini`. `rollback` swaps
`default.xbe` and `default.xbe.prev`.

From the Mac, curl does both directions (dashboard FTP login `xbox`/`xbox`):

```sh
X=ftp://<xbox-ip>
curl -u xbox:xbox -o perf.log $X/E/UDATA/4f430001/perf.log          # pull a log
curl -u xbox:xbox -l $X/E/UDATA/4f430001/save/card_a/                # list saves
curl -u xbox:xbox -T build-xbox/xbe/default.xbe $X/F/Applications/OpenCrossing/default.xbe
```

Re-download an uploaded XBE and compare `shasum` before asking for a test.
Copy the whole `save/card_a` folder off the console before touching a save.

Symbolize stack words with `tools/xbox/sym.py [map] < hang.log` (or
`crash.log`). The map must
come from the same build: `build-xbox/ac_xbox.map`, or the `ac_xbox.map`
attached to each GitHub release.

## Branches and releases

- `dev`: day-to-day work. Pushes run the CI build and keep the XBE as a
  workflow artifact.
- `main`: what users get. Every push to `main` (normally a merge from `dev`)
  builds the XBE and publishes a GitHub pre-release tagged `beta-<n>` (1, 2, 3... by count of earlier betas) with
  `OpenCrossing-Xbox-beta-<n>.zip` attached (`.github/workflows/build.yml`).

Release zip contents: `OpenCrossing/default.xbe`, `OpenCrossing/default.tbn`,
and `tools/` (`make-xiso`, `gc_trim_ciso.py`, `gcs_to_gci.py`). The link map
`ac_xbox.map` is attached separately for symbolizing `hang.log` reports.

To cut a beta: merge `dev` into `main` and push. Test the build from `dev` on
hardware first.
