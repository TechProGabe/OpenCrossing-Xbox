# Performance + compiler optimization

## Where it stands (hardware, 2026-10-03, v10)

60 fps in a quiet town and 58-59 fps a minute in normal play at 720p; NES
games at ~59 fps; busy spots (340 draws) were CPU-bound at ~51 fps in v4,
before the texture cache index and the inline shim compares, and haven't
been judged on their own since. Method: profile on the console
(`-DXBOX_PROF=1`), change one thing behind a kill switch, compare
`perf.log` and a screenshot pair. xemu numbers say nothing about speed.

## Carried from the DC port (`../OpenCrossing-Dreamcast/dc/opt-lists.mk`)

The DC sibling measured this on the same tree. It transfers as a **method**; its
numbers are SH-4 numbers.

- **Profiles, not one flag.** Whole tree at a base level, a reviewed **hot list at
  `-O3`**, a **quarantine list at `-O0`** for TUs *measured* to miscompile, and a
  throwaway env knob for bisecting. Each profile must stay a byte-identical revert.
- **`.text` is RAM.** DC: `-Os` saved 2.8 MB `.text` *and* was faster (11.6 → 18.5
  FPS; hot list → 20.0). On 64 MB unified RAM the same trade applies: `-Os` for
  the cold bulk (`src/data/`, actors), `-O2/-O3` for the hot path.
- **The hot path is one TU.** Town frame ≈ 58% emu64 dispatch (`emu64.c`, which
  textually includes `emu64_utility.c` + `emu64_print.cpp`). DC hot list:
  `emu64.c`, `sys_matrix.c`, `sys_math.c`, `sys_math3d.c`, `m_skin_matrix.c`,
  `m_lights.c`, `m_actor.c`, `m_play.c`, `ac_field_draw.c`, `m_field_info.c`,
  `m_lib.c`, `gfxalloc.c`, `graph.c`, `game.c`, and audio `rspsim.c`,
  `driver.c`, `system.c`, `aictrl.c`.
- **UB guards stay on at every level:** `-fno-strict-aliasing -fwrapv`. `emu64.c`
  is compiled as C++, where falling off a non-void function is UB that the
  optimizer deletes; watch it first if an `-O3` build misrenders.
- **Hard-error on a list entry that matches no TU** (DC lost two sessions to an
  inert entry).
- Every optimization gets a kill switch; default = the good build; judge with a
  screenshot pair, not counters alone.

## Xbox differences

- x86 has no alignment traps (DC's unaligned-u32 class is harmless here).
- PIII: 16 KB L1 I/D, 128 KB L2: i-cache locality work (DC's section ordering)
  may matter; measure on hardware, xemu doesn't model caches.
- `-march=pentium3` is set by `nxdk-cc`; SSE1 available for `pc_mtx.c` hot paths.
- Baseline: upstream PC port builds everything at `-O2` on x86 and is correct there.

## Measured

Whole tree at `-O2`; no per-TU profiles yet (not needed so far).

- Pass 1 (xemu, title demo, 2 min): 4620 → 5400 frames. Two changes:
  word-at-a-time `mem*` in `xbox_mem.c` (pdclib's byte loops were ~45% of
  CPU; kill switch `XBOX_FAST_MEM`) and one pushbuffer block across draws
  (`XBOX_PB_KICK`, was two `pb_cache_flush` per draw).
- Real hardware (retail Xbox, `perf.log`, 10 min in town, 2026-09-28):
  57-60 fps average at the 60 fps cap, a handful of frames over 33 ms per
  minute (scene loads). Its "CPU 15-16 ms" included `pc_vi.c`'s pacing
  spin (see `traps.md` on the cpu column).
- Hardware, 2026-09-29, 7 min (title, town, NES, a demolish): town 56-60
  fps a minute, with stretches at 18-20 ms of CPU per frame around 290
  draws; judged smooth enough by eye. NES: see `known-issues.md`.
- CPU/GPU overlap (2026-09-28; on by default since the 2026-10-03 backport,
  `gpu_overlap = 0` in `settings.ini` turns it off): present no longer waits
  for the GPU; the drain moves to the next frame's first GL call, so game
  logic overlaps the GPU (`renderer.md`). On in every hardware round since
  v1; never A/B'd against `gpu_overlap = 0` on the console.
- Vblank pacing (2026-09-29, `xbox_nv2a.c` `vbl_pace`, kill switch
  `-DXBOX_VBL_PACE=0`). `pc_vi.c` paced each frame 16.667 ms after the end
  of the previous one: an overrun was never made up (the average period is
  the mean of max(frame, 16.667), over the 16.683 ms vblank as soon as
  frames jitter around the budget), the 60.00 Hz timer beat against the
  59.94 Hz vblank, and the last 2 ms of every frame were a busy spin. Now
  each frame is due one vblank after the last: an early frame sleeps on the
  vblank event (2 ms timed slices, `traps.md`), a late one lets the next
  start at once, and a frame more than 2 vblanks behind resyncs. Applies at
  `max_fps = 60` (the default) and during NES play, and hands back to the
  timer while the GPU interrupt is masked (`xbox_vi_pace_policy` in
  `xbox_settings.c`). Hardware: town fine, NES better. It was meant for the
  cold-boot title chug and did not fix it: that theory came from reading
  code, and the next log pull showed a different cause (below).
- Direct disc image reads (2026-09-29, `xbox_io.c`, kill switch
  `-DXBOX_DISC_DIRECT=0`). Hardware `boot.log` of the first title demo
  after a cold boot: 66-83 ms frames for ~15 s, almost no GPU time, one
  32 KB disc read taking 80-186 ms, 648 KB read in 264 ms. The demo's
  music misses `xbox_aram.c`'s block cache, and each 32 KB miss was 32
  pdclib refills of 1 KB (`traps.md`); the audio producer fell behind and
  took the CPU back from the game thread. Once the cache is warm (a later
  title visit) it never chugged. `pc_disc.c` now reads the disc image with
  one `ReadFile` per request, straight into the caller's buffer. xemu
  (loaded host): 320 KB in 10 ms, was 83 ms. On hardware in every build
  since; the first title demo after a cold boot hasn't been timed again.

## Melee-X backport (2026-10-03, `backport.md`)

From Melee-X, each with a kill switch and most with a `settings.ini` key:
memcpy & co. as builtins (`xbox_prelude.h`; `pc_gx.o` 81 → 18 calls), CPU/GPU
overlap on by default, per-draw skips and native texture formats
(`renderer.md`), 32 KB kicks. xemu title demo, same build with and without
native textures and draw skip: shim 1.3 → 0.8 ms a frame, texture pool 440
KB smaller of 864.

Hardware, v4 at 720p (2026-10-03, first console profile): quiet town 60 fps
(game+emu64 ~3.5 ms, shim 0.6); busy town (340 draws) 51 fps, CPU-bound at
~19 ms (game+emu64 14.3, shim 4.9); NES 52 fps (fixNES 13.3 ms, upload 3,
gpu wait 2). Busy town's top functions: `GXLoadTexObj` 8.6% (a linear scan
of the 2048-entry texture cache per bind: `PC_TEX_CACHE_INDEX` chains it by
pointer), `GXPosition3f32` 8.3%, `draw` 6.8%, `memcmp` 6.4% (the shim's
per-draw compares: `words_eq`). NES: `ppuCycle` 24%, the screen upload 14%
(two texels a word now), `apuCycle` 11%, `cpuCycle` 8%.

v5-v7 from that profile: the texture cache index, inline compares, the NES
upload two texels a word (NES 52 → 56 fps), and v7 dropping the GameCube
quad that drew the NES picture a second time (→ 59.4 fps). The audio
producer runs above the game thread (`audio_priority`): the v4 chug was it
waiting for CPU-bound frames, and v7 logged no starvation after boot.

xemu beta-3 vs this release (2026-10-03, same conditions): CPU ms per frame
roughly halved (5-9 vs 8-15), save load ~500 vs ~555 ms. The release logs
many more 40-59 ms `[HITCH]` frames there at the same average fps
(`known-issues.md`): xemu's slow GPU against vblank pacing; check
`perf.log`'s `>33ms` on hardware.

## Profiling on the console

`-DXBOX_PROF=1` (`xbox_prof.c`, from Melee-X): a time-critical thread reads
the game thread's interrupted EIP about 1000 times a second; every 20 s the
hottest 64-byte buckets go to the log as `[PROF]` lines, with callers one
frame up (`[PROFC]`) and the callers of memcpy & co. (`[PROFL]`). Fold them
with the map of the same build:

```sh
tools/xbox/prof_report.py boot.log boot2.log --map ~/xemu/ochw/ac_xbox.vNN.map
```

Static functions come from `<map>.statics` (`static_syms.py`, made by
`console.py stage`); without it a static's samples go to the public function
before it. `while waiting` counts samples where the game thread wasn't
preempted (pacing, GPU waits). xemu's profile is skewed (slow GPU: `wait_idle`
on top; SSE through softfloat): trust the console's.

## What the logs give (`toolchain.md` has where they live)

- `[FRAME]` every 5 s: fps and ms per frame split into game + emu64 (the
  rest), the shim's draw work, texture uploads, GPU waits, pacing and file
  reads, plus draws and how often draw_skip saved work.
- `[HITCH]`: a frame over 40 ms (`XBOX_HITCH_MS`), split into cpu, GPU +
  flip, texture uploads and file reads; a run of frames with < 3 draws (a
  fade or load) is one line plus its length.
- `[PACE]`: a 5 s window in which frames missed a vblank 6 or more times a
  second (17-33 ms frames never reach `[HITCH]`); `-DXBOX_PACE_MISSES=0`.
- `[NES]`: fixNES's own ms per NES frame, every 300 frames.
- `[BEAT]` every 5 s: vblanks, frames presented, free KB, and audio
  starvation when there was any.
- `perf.log`: one line a minute plus that minute's `[HITCH]` ≥ 100 ms,
  `[PACE]` and `[NES]` lines, so one pull covers the whole session.
- Lesson from the title chug: pull `boot.log` / `perf.log` before
  theorising. Two builds went to the console on a pacing theory; one log
  pull pointed at the disc reads.
