# Performance + compiler optimization

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
  is compiled as C++ — falling off a non-void function is UB that the optimizer
  deletes; watch it first if an `-O3` build misrenders.
- **Hard-error on a list entry that matches no TU** (DC lost two sessions to an
  inert entry).
- Every optimization gets a kill switch; default = the good build; judge with a
  screenshot pair, not counters alone.

## Xbox differences

- x86 has no alignment traps (DC's unaligned-u32 class is harmless here).
- PIII: 16 KB L1 I/D, 128 KB L2 — i-cache locality work (DC's section ordering)
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
  57–60 fps average at the 60 fps cap, a handful of frames over 33 ms per
  minute (scene loads). Its "CPU 15–16 ms" included `pc_vi.c`'s pacing
  spin (see `traps.md` on the cpu column).
- Hardware, 2026-09-29, 7 min (title, town, NES, a demolish): town 56-60
  fps a minute, with stretches at 18-20 ms of CPU per frame around 290
  draws; judged smooth enough by eye. NES: see `known-issues.md`.
- CPU/GPU overlap (2026-09-28, `gpu_overlap = 1` in `settings.ini`, off by
  default): present no longer waits for the GPU; the drain moves to the next
  frame's first GL call, so game logic overlaps the GPU (`renderer.md`). Not
  measured on hardware yet.
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
  (loaded host): 320 KB in 10 ms, was 83 ms. Hardware: pending.

## What the logs give (`toolchain.md` has where they live)

- `[HITCH]`: a frame over 40 ms (`XBOX_HITCH_MS`), split into cpu, GPU +
  flip, texture uploads and file reads; a run of frames with < 3 draws (a
  fade or load) is one line plus its length.
- `[PACE]`: a 5 s window in which frames missed a vblank 6 or more times a
  second (17-33 ms frames never reach `[HITCH]`); `-DXBOX_PACE_MISSES=0`.
- `[NES]`: fixNES's own ms per NES frame, every 300 frames.
- `perf.log`: one line a minute plus that minute's `[HITCH]` ≥ 100 ms,
  `[PACE]` and `[NES]` lines, so one pull covers the whole session.
- Lesson from the title chug: pull `boot.log` / `perf.log` before
  theorising. Two builds went to the console on a pacing theory; one log
  pull pointed at the disc reads.
