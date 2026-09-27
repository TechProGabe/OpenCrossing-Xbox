# Toolchain

Status: planned (M1). Verify each item when it lands, then mark ✅.

## Build

- **nxdk** — https://github.com/XboxDev/nxdk (clang + lld, PE → XBE via
  `cxbe`, pdclib, libc++, pbkit, nxdk-sdl SDL2). Pin a commit in the Dockerfile.
- **pbgl** — https://github.com/fgsfdsfgs/pbgl (OpenGL 1.x subset on pbkit).
  Bring-up renderer, M3.
- **xgu** (in nxdk) — low-level NV2A push-buffer helpers for the M6 renderer.
- Host: Apple M4, Docker via **colima** (not running by default: `colima start`).
  Sibling DC repo notes: no BuildKit on this host → `DOCKER_BUILDKIT=0`, no
  `--progress`, use `bash -c` not `bash -lc` inside images.
- Flags carried from the PC port: `-O2 -fno-strict-aliasing -fwrapv`,
  `-DTARGET_PC -DTARGET_XBOX`, i386, 32-bit.

## Run

- **xemu** (macOS arm64 build available) — https://xemu.app. Needs MCPX boot
  ROM, flash BIOS and an HDD image (user-supplied, never committed). Set RAM to
  64 MB to enforce the budget.
- Serial/debug output: xemu serial port → host file/terminal
  (https://xemu.app/docs/serial-port/).
- Deploy loop (xemu): copy XBE + ISO into the HDD image (`E:\Games\OpenCrossing\`).

## Real hardware (M7)

- Modded Xbox, FTP via `xbox-turbo-ftp` (GabeConway/xbox-turbo-ftp).
- Layout: `E:\Games\OpenCrossing\default.xbe` + user disc image in the same folder.
- Or burn: `tools/make-xiso` → XISO (xdvdfs) → DVD-R. Also xemu "Load Disc".
- Saves: `E:\UDATA\<TitleID>\` in every launch mode.
