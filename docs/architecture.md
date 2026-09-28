# Architecture

How the port is put together and why. Subsystem detail lives in
`renderer.md`, `memory.md` and `perf.md`; gotchas in `traps.md`.

## Base

- Game code: the ACreTeam decompilation (`src/`, `include/`), merged at
  decomp head `09ca8e8b`.
- Platform layer: flyngmt/ACGC-PC-Port v0.9.3 (`pc/`), used unmodified except
  for the fixes in `patches.md`.
- Xbox layer: `xbox/`, built with nxdk (`toolchain.md`).
- Target: retail Xbox with the stock 64 MB. 128 MB is never required.

The Xbox is a 32-bit little-endian x86 machine, the same ABI the PC port
already targets. Game logic, emu64, culling, texture decoding, the disc
reader and the save code compile unchanged. Only the platform seams are new:
video, audio, input, memory, file paths and the TEV-to-combiner translation.

## Hardware compared

| | GameCube | PC port | Xbox |
|---|---|---|---|
| CPU | Gekko PPC 485 MHz | x86-32 | Pentium III-class 733 MHz, 128 KB L2, SSE1 |
| RAM | 24 MB + 16 MB ARAM | plenty | 64 MB shared by CPU and GPU |
| GPU | Flipper, TEV (≤16 stages) | GL 3.3 shaders | NV2A: vertex programs, 4 textures per pass, 8 general + final register combiners |
| Audio | DSP + ARAM | SDL + rspsim | AC97 (MCPX APU under xemu) |
| Storage | 1.46 GB disc | file | HDD (FATX) or DVD/CD at `D:\` |
| Save | memory card | GCI file | GCI file on the HDD |

A TEV stage (`d + (1-c)·a + c·b`, bias, scale) maps onto one or two NV2A
combiner stages (`A·B + C·D` with input mappings). The game uses at most 3
TEV stages, well under the 8 combiner stages.

## Frame path

```
disc image (.iso/.gcm/.ciso) -> pc_disc.c (FST, Yaz0, DOL/REL assets)
decomp C -> N64 display lists -> emu64 -> GX calls
  -> pc_gx*.c (batching, state dedup, AABB cull, texture decode + cache)
  -> GL 3.3 subset -> xbox_nv2a.c (GL shim over pbkit) -> NV2A
```

`pc_gx.c` loads GL through glad; `xbox_gl_nv2a_load()` fills the glad
pointers with the shim, so `pc/` needs no Xbox branches. Only `pc_gx_tev.c`
(GLSL) is swapped for `xbox_gx_tev.c`.

## Platform seams (`xbox/src/`)

| file | job |
|---|---|
| `xbox_main.c` | entry, finds the disc image, splash, error cards, frame counter |
| `xbox_io.c` | path mapping, logging, file flushing, `boot.log` |
| `xbox_nv2a.c`, `xbox_tev_rc.c`, `shaders/gx.vsh` | renderer (`renderer.md`) |
| `xbox_aram.c` | sparse ARAM with disc-backed regions (`memory.md`) |
| `xbox_audio.c` | own polled AC97 driver on hardware, APU voice under xemu |
| `xbox_pad_axis.c` | left-stick shaping for worn controllers, stick trace |
| `xbox_watchdog.c` | hang reporter (screen + `hang.log`), rolling `last.log` |
| `xbox_crash.c` | CPU exception reporter (screen + `crash.log`) |
| `xbox_mem.c` | word-at-a-time `mem*` (pdclib's are byte loops) |

## Files on the console

The XBE's own directory is mounted as `D:\`, whether it runs from the HDD
or from a disc. `xbox_main.c` scans it for the first `.iso`, `.gcm` or
`.ciso` whose header says GAFE01, so the image can have any name.

Everything written goes to `E:\UDATA\4f430001\` in every launch mode (a disc
is read-only): `settings.ini`, `keybindings.ini`, `save/card_a/*.gci` and
the logs (`boot.log`, `last.log`, `crash.log`, `hang.log`, `perf.log`,
`input.log`, `stickN.log`).
Saves use the GameCube `.gci` format, so they move between this port,
Dolphin, the PC port and the other OpenCrossing ports.

Saves are flushed to disk on close, and the volume is flushed on rename.
FATX caches directory entries, and Mr. Resetti's "quit without saving" check
is a save written at load time, so an unflushed save would get the player
lectured after a clean power-off.

## Distribution

Users never compile. A release is a prebuilt `default.xbe` plus
`default.tbn` in one folder; the user adds their own disc image next to it.
For a disc, `tools/make-xiso` packs the XBE and the image into an XISO. By
default it trims the image to the ~28 MB the game reads
(`tools/gc_trim_ciso.py`), so the XISO fits a CD-R. We ship the script,
never an XISO.

## Decided against

- 64-bit build: the decomp assumes 32-bit pointers, and the Xbox is 32-bit.
- The PC port's GLSL shader path: nxdk has no GLSL compiler.
- pbgl for the renderer: replaced by the GL shim over pbkit.
- Cg for the vertex program: `cgc` does not run in the arm64 SDK image;
  `gx.vsh` is NV2A assembly.
- nxdk's `hal/audio`: its interrupt handler froze real hardware
  (`traps.md`).
