# Plan

Decided 2026-09-27: stock 64 MB; base = flyngmt/ACGC-PC-Port v0.9.3 + latest
ACreTeam/ac-decomp; toolchain nxdk; renderer pbgl first, xgu + register
combiners later; xemu until stable, then real hardware. Prior art check
2026-09-27 (GitHub repos/code/forks + web): **no existing Xbox port.**

## Why this is the least-resistance path

The Xbox is a 32-bit little-endian x86 machine — the exact ABI the upstream PC
port already targets (`FATAL_ERROR` on 64-bit, `-O2` UB flags proven on x86).
The game, emu64, batching, culling, texture decode, disc reader, save code all
compile unchanged. Only the platform seams change: SDL2/GL3.3 window + shaders
→ nxdk SDL2 + NV2A.

## Milestones

| # | goal | gate |
|---|---|---|
| M0 | repo: PC-Port base + decomp head merged, docs, CLAUDE.md | ✅ pushed |
| M1 | nxdk toolchain in Docker; hello XBE boots in xemu; all objs compile against nxdk headers | `default.xbe` boots, prints to debug log |
| M2 | headless boot: `xbox_main/os/vi/dvd` from `pc_*`; ISO read from `D:\`; arena at fixed VA; no draw | scene log reaches town (`SCENE_MODE` sequence) |
| M3 | pixels: pbgl backend behind `pc_gx` batch/cull; TEV via DC's 101-config fixed-function table | town renders in xemu, screenshot |
| M4 | input (SDL gamecontroller), audio (SDL + rspsim), save (GCI on HDD) | walk, hear BGM, save/load round-trip |
| M5 | fit 64 MB | full town + interiors in stock-RAM xemu, no OOM over a 30 min run |
| M6 | fidelity/perf: xgu + generated register combiners, HW T&L lighting, 480p | stable 30 fps town; TEV screenshot diff vs PC |
| M7 | real hardware: FTP deploy, burn-in, release zip (XBE + make-xiso) | boots + saves on retail box from HDD **and** burned DVD-R |

## Distribution (user requirement, 2026-09-27)

**No compiling for users** — same model as OpenCrossing-Anbernic. Release zip =
prebuilt `default.xbe` + readme in one folder. User copies the folder to
`E:\Games\OpenCrossing\` (FTP or xemu HDD), drops their own disc image **next to
the XBE** (no `rom/` subfolder; user call 2026-09-27, keeps it clean), launches from
the dashboard. `xbox_main` scans the XBE's own directory (nxdk mounts it as `D:\` — verify at M1) for the
first `.iso/.gcm/.ciso` whose header is GAFE01 — PC port's `rom/` lookup is not used. Formats: `.iso` / `.gcm` / `.ciso` — `pc_disc.c`
already parses all three at runtime, no extraction step. (".cso" in chat =
CISO; PSP-CSO and RVZ are not supported — add only if asked.) CI builds the XBE on
`v*` tags only.

**Burnable disc (user requirement, 2026-09-27).** `tools/make-xiso` (host
script, macOS/Linux/Windows) packs `default.xbe` + the user's disc image into an
XISO via **xdvdfs** (https://github.com/antangelo/xdvdfs). ~1.5 GB → single-layer
DVD-R. Same XISO = xemu "Load Disc" (third launch mode, also the easiest xemu
setup). We ship the script, never an XISO. Consequences:
- Runtime must find the image whether the XBE runs from HDD or DVD (both are the
  XBE's own dir) — same scan as above.
- **Saves always go to `E:\UDATA\<TitleID>\`** (standard Xbox save location;
  DVD is read-only). One path for all three launch modes. Pick a TitleID at M1.
- DVD seek/read is far slower than HDD. PC port loads assets at startup from the
  image; any mid-game streaming (e.g. an `audiorom` lever from `docs/memory.md`)
  must be judged on a DVD boot, not just HDD.
- Burned discs need a modded box with a DVD-R-compatible drive (varies by drive
  model). Document in release readme.
- **CD-R works too (2026-09-27):** `make-xiso` trims the image first
  (`tools/gc_trim_ciso.py`: system area + DOL + FST files only → CISO,
  1.46 GB → 27.6 MB, boots to title in xemu). CD-R reading also varies by drive
  (Samsung good, Thomson poor). `--full` keeps the untrimmed image.

## Hard problems (ranked)

1. **Renderer.** No GLSL on nxdk. PC port's TEV→GLSL generator (`pc_gx_tev.c`)
   dies. Bring-up uses fixed-function; fidelity needs a TEV→combiner generator.
   See `docs/renderer.md`.
2. **RAM.** PC steady state ≈43–45 MB + GPU buffers, Xbox gives ~60 MB total,
   GPU included. See `docs/memory.md`.
3. **Arena address.** seg2k0 tells pointers from N64 segment addresses by range;
   PC mmaps the arena ≥ `0x10000000`. Xbox needs the same via
   `NtAllocateVirtualMemory` at a fixed base. See `docs/traps.md`.
4. **C++ on nxdk.** JSystem wrappers are C++. nxdk ships libc++; check
   exceptions/RTTI assumptions at M1.

## Risks

| risk | mitigation |
|---|---|
| pbgl missing a GL feature pc_gx needs | pbgl is small; patch it or drop to xgu early for that path |
| 64 MB overflow | DC levers: stream `audiorom.img`, ARAM window, lazy texture decode |
| PIII too slow at 30 fps | unlikely (A53 @1.5 GHz hit 56 fps at `-O0`); profile before optimizing |
| upstream-pc diverges | keep `pc/` pristine, cherry-pick by path (`docs/upstream.md`) |
