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
- **Audio: silent in xemu (user-confirmed 2026-09-27).** `xbox_audio.c` polls
  the AC97 (IRQ never fires in xemu) and calls `XAudioProvideSamples`; the
  producer ring fills, but nothing is heard. Suspects, in order: pump never
  advances past the first buffers (CIV poll at `0xFEC00114` wrong/stale),
  `s_playing` stays 0 (AIStartDMA not reached), buffers are silence (check
  sample peaks in the log), XAudio descriptor/format setup. Debug with a
  440 Hz tone in `fill_48k` first to split device vs game path.

## Next action

0. Fix silent audio (see above).
1. Drive input past the title (controller path in `xbox_main.c`) → town.
2. Renderer fidelity: swap tables, indirect textures, EFB copies, NES path.
3. Perf pass (DC opt lists, docs/perf.md), then real hardware.

Open risk (review 2026-09-27): `xbox_aram.c` `s_map[]` has no lock; `add_map`
(DVD/audio thread) can race `find_map` (game thread). Add a critical section
before audio streaming work.

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

`pc/` edit: `pc/include/pc_gx_internal.h` — `PC_GX_MAX_VERTS` is `#ifndef`-guarded
so the Xbox build can pass 16384 (vertex batch 6 MB → 1.5 MB).

## Closed (do not re-propose)

- 64-bit build — decomp assumes 32-bit pointers everywhere; Xbox is 32-bit anyway.
- GLSL/shader-cache path from `pc_gx_tev.c` — no GLSL compiler on nxdk.
- pbgl / fixed-function bring-up — replaced by the GL-shim-over-pbkit backend.
- Cg for the vertex program — cgc does not run in the arm64 image; `gx.vsh` is NV2A asm (`nv2a-vsh`).
