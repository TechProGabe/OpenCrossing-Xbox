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
- Real hardware (retail Xbox, `perf.log`, 10 min in town): 57–60 fps average
  at the 60 fps cap, CPU 15–16 ms per frame, a handful of frames over 33 ms
  per minute (scene loads).
- `[HITCH]` lines (frame over 40 ms, `XBOX_HITCH_MS`) say where a slow frame
  went: CPU, GPU + flip, texture uploads, file reads.

- CPU/GPU overlap (2026-09-28, `gpu_overlap = 1` in `settings.ini`, off by
  default): present no longer waits for the GPU; the drain moves to the next
  frame's first GL call, so game logic overlaps the GPU (`renderer.md`). Not
  measured on hardware yet.
