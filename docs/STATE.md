# State

**Short by design.** What is true now + next action.

## Where the port is (2026-09-27)

- M0 done: PC port v0.9.3 (`4099d246`) + ac-decomp head (`09ca8e8b`).
- **M1 done:** every TU compiles under nxdk (clang 21), links, boots in xemu.
- **M2 done:** disc parse, ARAM mounts, audio, saves scan, main loop.
- **M3 first light:** the title demo (town, trees, logo, villager, fog) renders
  correctly on the NV2A backend (`xbox_nv2a.c` + `gx.vsh` + `xbox_tev_rc.c`)
  in xemu at 64 MB, ~180–220 draws/frame, 10–15 combiner programs, 0
  approximated. ~6 MB free at runtime (image 25.3 MB).
- **Audio: correct on the guest, inaudible in macOS xemu** (2026-09-27).
  `AIInit` unmutes the AC97 codec (xemu boots it muted); wavcapture of xemu's
  audio backend shows the title music. But the macOS xemu build links no
  CoreAudio and xemu disables QEMU's SDL driver, so the AC97 (QEMU ac97) goes
  to the `none` backend. xemu only plays the **MCPX APU** (its own SDL3 path).
  Real hardware plays AC97 (nxdk's audio sample path). → to hear it in xemu,
  add an APU voice output path (next action 0).
- **Random full hang fixed:** `SDL_Atomic*` spinlock priority-inversion livelock
  between game thread and AC97 pump (traps.md). Soak after fix: 9300 frames /
  4 min clean (before: hung at frame 180–1500 in 3 of 5 runs).
- `xbox_watchdog.c` dumps all thread stacks to COM1 if frames stop for 6 s;
  symbolize with `tools/xbox/sym.py < serial.log`.
- User report: a rare single black frame at the title demo, otherwise stable.
  Not investigated yet (suspect: a swap with no draws, or pbkit flip timing).

## Next action

0. Audible in xemu: APU output path (VP voice streaming the 48 kHz ring into
   mixbins 0/1; xemu `hw/xbox/mcpx/apu/vp/vp.c`), AC97 kept for hardware.
   Investigate the rare black frame.
   Profile hot spots at the title (EIP sampling via monitor `info registers`):
   pdclib byte-loop `memset/memcpy/memmove/memcmp` (~45%) and `pb_cache_flush`
   (~20%) — replace with `rep movsd/stosd` versions, cut flushes.
1. Drive input past the title (controller path in `xbox_main.c`) → town.
2. Renderer fidelity: swap tables, indirect textures, EFB copies, NES path.
3. Perf pass (DC opt lists, docs/perf.md), then real hardware.

Threading (fixed 2026-09-27): `xbox_aram.c` map table guarded at DPC level,
its LRU disc cache by a critical section; `pc_disc_read` (fseek+fread pair on
one FILE*) by an SDL mutex.

Resume recipe: `xbox/build.sh` then
`OCX_ISO=<iso> harness/xbox/run.sh 150 "NV2A. frame 1200"` with
`XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=-DXBOX_FBDUMP_EVERY=300'"` →
`tools/xbox/fbdump_to_png.py ~/xemu/run/serial.log <out>`. Knobs: docs/renderer.md.

## `src/` + `include/` TARGET_XBOX branch ledger

All are clang strictness (no `-fpermissive`), behaviour-identical:

| file | symbol | why |
|---|---|---|
| `include/JSystem/JSupport/JSUIosBase.h` | `setState(int)`, `clrState(int)` | callers pass stdio `EOF` (int) |
| `include/JSystem/JSupport/JSURandomInputStream.h` | `seek(s32,int)`, `seekPos(s32,int)` ×2 | callers pass stdio `SEEK_*` |
| `src/static/JSystem/JKernel/JKRHeap.cpp` | `operator new/new[]` | must take `size_t`; 2-arg form must match header's `s32` |
| `src/static/JSystem/JKernel/JKRDvdRipper.cpp` | `isErrorRetry == false` | fn-vs-bool compare; same always-false test |
| `src/static/Famicom/famicom.cpp` | `SetupResBanner` call | `u32*` → `size_t*` |
| `src/static/libjsys/jsyswrapper_main.cpp` | `JC__JKRGetResourceEntry_byName` | `void*` → `CSDIFileEntry*` cast (unguarded, valid everywhere) |

Memory branches (behaviour change, kill switch `-DXBOX_ARAM_FLAT=1`):

| file | symbol | why |
|---|---|---|
| `src/static/jaudio_NES/internal/dvdthread.c` | `DVDT_LoadtoARAM_Main` | `xbox_aram_map_file`: audiorom.img served from disc, not copied (8.3 MB) |
| `src/static/JSystem/JKernel/JKRAramArchive.cpp` | `JKRAramArchive::open` | `xbox_aram_map_entry`: uncompressed RARC data served from disc (~6.5 MB) |

`pc/` edits: `pc/src/pc_disc.c` — `pc_disc_read` takes a mutex (fseek+fread
race between DVD/audio/game threads; upstreamable bug fix, not Xbox-specific).
`pc/include/pc_gx_internal.h` — `PC_GX_MAX_VERTS` is `#ifndef`-guarded
so the Xbox build can pass 16384 (vertex batch 6 MB → 1.5 MB).

## Closed (do not re-propose)

- 64-bit build — decomp assumes 32-bit pointers everywhere; Xbox is 32-bit anyway.
- GLSL/shader-cache path from `pc_gx_tev.c` — no GLSL compiler on nxdk.
- pbgl / fixed-function bring-up — replaced by the GL-shim-over-pbkit backend.
- Cg for the vertex program — cgc does not run in the arm64 image; `gx.vsh` is NV2A asm (`nv2a-vsh`).
