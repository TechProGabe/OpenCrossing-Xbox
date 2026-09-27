# Toolchain

Status: M1 ✅ (2026-09-27).

```sh
xbox/build-image.sh                      # SDK image opencrossing-xbox:sdk (~10 min cold)
xbox/build.sh                            # -> build-xbox/xbe/default.xbe
XBOX_TARGET=objs xbox/build.sh           # compile only, no link
OCX_ISO=/path/to/AC.iso harness/xbox/run.sh 120 "stop-regex"   # xemu + COM1 log
tools/xbox/fbdump_to_png.py ~/xemu/run/serial.log out          # [FBDUMP] -> PNG
```

## Build

- **nxdk** — https://github.com/XboxDev/nxdk pinned `58427c07` in
  `xbox/docker/Dockerfile` (arm64-native Debian trixie + LLVM 21; upstream
  images are amd64/386 only). CMake via `share/toolchain-nxdk.cmake`.
- **pbgl** — https://github.com/fgsfdsfgs/pbgl (OpenGL 1.x subset on pbkit).
  Bring-up renderer, M3.
- **xgu** (in nxdk) — low-level NV2A push-buffer helpers for the M6 renderer.
- Host: Apple M4, Docker via **colima** (not running by default: `colima start`).
  Sibling DC repo notes: no BuildKit on this host → `DOCKER_BUILDKIT=0`, no
  `--progress`, use `bash -c` not `bash -lc` inside images.
- Flags carried from the PC port: `-O2 -fno-strict-aliasing -fwrapv`,
  `-DTARGET_PC -DTARGET_XBOX`, i386, 32-bit.

## Run

- **xemu** 0.8.136 (`brew install --cask xemu`). Firmware (user-supplied, never
  committed) staged in `~/xemu/`: `mcpx.bin` (MCPX 1.0), `bios.bin` (Complex
  4627 v1.03), `hdd.qcow2` (xemu dashboard image). Config
  `~/Library/Application Support/xemu/xemu/xemu.toml`: 64 MB, skip boot anim.
- Serial/debug output: xemu serial port → host file/terminal
  (https://xemu.app/docs/serial-port/).
- Deploy loop (xemu): copy XBE + ISO into the HDD image (`E:\Games\OpenCrossing\`).

## Real hardware (M7)

- Modded Xbox, FTP via `xbox-turbo-ftp` (GabeConway/xbox-turbo-ftp).
- Layout: `E:\Games\OpenCrossing\default.xbe` + user disc image in the same folder.
- Or burn: `tools/make-xiso` → XISO (xdvdfs) → DVD-R. Also xemu "Load Disc".
- Saves: `E:\UDATA\<TitleID>\` in every launch mode.
