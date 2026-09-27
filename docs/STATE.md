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
- **Audio works in xemu (user-confirmed by ear, 2026-09-27).** macOS xemu
  can't play AC97 (no CoreAudio, QEMU SDL driver disabled → `none`), so under
  xemu (AC97 codec vendor ID 0x8384 = QEMU SigmaTel) `xbox_audio.c` streams
  through one looping MCPX APU VP voice (heard via xemu's MON_VP path). Real
  hardware (WM9709, 0x574D) keeps the AC97 pump — unverified until M7.
  Kill switch `-DXBOX_AUDIO_APU=0`.
- **Random full hang fixed:** `SDL_Atomic*` spinlock priority-inversion livelock
  between game thread and AC97 pump (traps.md). Soak after fix: 9300 frames /
  4 min clean (before: hung at frame 180–1500 in 3 of 5 runs).
- `xbox_watchdog.c` dumps all thread stacks to COM1 if frames stop for 6 s;
  symbolize with `tools/xbox/sym.py < serial.log`.
- User report: a rare single black frame at the title demo, otherwise stable.
  Not investigated yet (suspect: a swap with no draws, or pbkit flip timing).

## Next action

0. Perf pass 1 done (2026-09-27): word-at-a-time `mem*` (`xbox_mem.c`, pdclib's
   are byte loops, were ~45% of CPU) and one pushbuffer block across draws
   (`XBOX_PB_KICK`, was 2 `pb_cache_flush` per draw). Title demo: 4620 → 5400
   frames / 2 min; drawn frames > 40 ms: 3 → 2 (startup + first frame of a new
   scene). `[HITCH]` lines on COM1 (frame > 40 ms or < 3 draws) now say where
   a slow frame went (cpu / gpu+flip / tex uploads / fread). Black frame: none
   outside boot + the game's own 2.3 s cleared screen between demo scenes; if
   the user sees one, its `[HITCH] ... draws 0` line is the evidence.
   Next perf lever: present waits for GPU idle every frame (`wait_idle`) — no
   CPU/GPU overlap; top of the profile is now `pb_busy`.
1. ✅ In-game (2026-09-27): title → K.K. intro → train with Rover → name
   entry → phone call, driven by `-DXBOX_AUTOPAD=<call>` (scripted START/A,
   `xbox_autopad.c`; cadence mirrors the DC port's DC_AUTOSTART). User plays
   it by hand in xemu: "running well", high fps, rare hitch + rare black frame.
   Visuals: user says 100% (train window included).
2. Renderer fidelity: swap tables, indirect textures, EFB copies, NES path.
3. Perf pass (DC opt lists, docs/perf.md), then real hardware.

Threading (fixed 2026-09-27): `xbox_aram.c` map table guarded at DPC level,
its LRU disc cache by a critical section; `pc_disc_read` (fseek+fread pair on
one FILE*) by an SDL mutex.

**Hardware test build (2026-09-27):** `AC-Xbox-20260927a.iso` (35 MB XISO,
trimmed CISO inside, CD-R or DVD-R) + `AC-Xbox-20260927a-hdd/default.xbe` on
the user's Jupiter share `/Volumes/Gabe/AC-Xbox/` (commit in the `.src.json`).
Build: release `xbox/build.sh` → `tools/make-xiso build-xbox/xbe/default.xbe
<disc> ~/xemu/release/<name>.iso` → boot it in xemu → copy. M7 = user reports
from the real box (AC97 audio, DVD/CD read speed, 64 MB, saves on E:).
**Runs on real hardware (2026-09-27, builds d–g, FTP to F:\Applications):**
boots, plays, sound (polled AC97), saves under E:\UDATA\4f430001\save.
perf.log min 1: 56.6 fps avg (60 cap), cpu 9.5 ms avg. Fixed on HW: freeze at
XAudioPlay (nxdk IRQ handler → own polled ACI driver), rolling black bar
(scanout wait), worn-stick drift/snap-back (37% radial deadzone, rescaled, +
snap-back suppression; L3 dumps a 10 s stick trace to stickN.log; build h:
43% radial, pc_pad per-axis deadzone bypassed, picked by replaying 4 traces).
Leaf icon (user-confirmed in dashboard)
($$XTIMAGE + default.tbn).
Open: per-controller deadzone (a good pad also gets 37% now).

**HW test 1 result (CD-R, build a):** our splash shows, then black forever.
No serial on hardware, so build b adds: COM1 probe (an absent UART could make
every logged byte spin ~0.1 s), `E:\UDATA\4f430001\boot.log` (flushed per
line until frame 600), and a boot-time watchdog (no first frame in 90 s, or
frames stop 6 s) that writes `hang.log` and prints the log tail + thread
stacks on screen. Checked: binary has no SSE2 (pentium3 target). HW test 2 is
over FTP: deploy, then pull boot.log/hang.log.

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
