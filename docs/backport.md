# Melee-X backport plan

Melee-X (the Melee port) started from this repo's renderer, audio driver
and crash reporter and went about 45 console builds further. Most of what
it fixed lives in code the two ports share. This is the plan for bringing
it back, written 2026-10-03 after reading Melee-X's `docs/` and checking
each lesson against this tree. Update the status column as items land;
move settled facts into the matching doc and delete this file once done.

## Where the two stand

- OpenCrossing: town 56-60 fps on hardware, stretches at 18-20 ms of CPU
  a frame (~290 draws); NES frames 17-18 ms (miss the vblank); a cold-boot
  title chug. No sampling profiler on hardware; `boot.log` stops at frame
  120, `last.log` is 4 KB.
- Melee-X: the CPU is the limit in matches; its gains came from the
  console profiler, builtins, CPU/GPU overlap, caching and per-draw state
  skips. Its GPU stalls and audio bugs were fixed in code this repo shares.

## Gaps confirmed in this tree

| Melee-X lesson | OpenCrossing before the backport |
|---|---|
| `wait_idle` waits until the pusher caught up, CACHE1 is empty, the pusher stopped and PGRAPH is idle, seen twice; a stall is reported after 2 s | spins on `pb_busy()` (can pass early), no timeout |
| `BREAK_VERTEX_BUFFER_CACHE` at each pushbuffer batch start | missing |
| the window clip's maximum is inclusive (`x + w - 1`) | `x + w`: one pixel of bleed |
| AC97: bus masters reset and buffers queued before the run bit; stuck-CIV recovery; bus masters reset at shutdown | `aci_run(0)` only at shutdown |
| memcpy & co. as compiler builtins (~18% of a Melee frame) | `-ffreestanding` keeps them calls: 81 in `pc_gx.o`, 9 in `pc_gx_texture.o` |
| CPU/GPU overlap on by default (3.6 ms a frame on the console) | off by default, not measured on hardware |
| pushbuffer kick every 32 KB | every 16 KB |
| native texture formats (palette, AY8, A8Y8, R5G6B5, ...) | everything A8R8G8B8: 2-8x the pool |
| sampling profiler, `[PERF]` buckets every 5 s, rotating logs, `[BEAT]`, PGRAPH at the first fault, `console.py` | none |

## Phases

### 0. Instruments first

Melee-X's rule, and our own title-chug lesson: measure on the console
before acting on a theory.

1. Sampling profiler (`xbox_prof.c` from `xhw_prof.c`) + `prof_report.py`:
   game-thread EIP ~1000x/s with the caller, folded with the build's map.
2. `[PERF]` every 5 s with per-bucket ms.
3. Logs for a whole session: `boot.log` up to 4 MB, then `boot2.log` /
   `boot3.log` in turn; `[BEAT]` heartbeat (retrace vs presented, free).
4. `tools/xbox/console.py stage|deploy|pull vNN` with a map per build.
5. First GPU fault: PGRAPH registers and the pushbuffer around GET.

### 1. Stability

1. Strict `wait_idle` with a stall report (the overlap relies on it).
2. `BREAK_VERTEX_BUFFER_CACHE` per batch.
3. Inclusive window clip.
4. AC97 bundle: start order, stuck-CIV recovery, shutdown reset.
5. `settings.ini` written atomically (temp file, read back, rename).
6. Later: hold the RAM above 64 MB on 128 MB consoles (all of Melee-X's
   mid-match GPU stalls came from one).

### 2. Frame time (kill switch on each, profile-driven)

1. `xbox_builtin.h`: memcpy/memset/memmove/memcmp builtins, inline sqrtf.
2. CPU/GPU overlap on by default (A/B with `gpu_overlap`).
3. Pushbuffer kick 32 KB.
4. Per-draw skips in the shim: unchanged texture units, unchanged
   combiner constants.
5. Native texture formats through a hook in `pc_gx_texture.c` (a `pc/`
   edit, `patches.md`), with a host test.
6. NES screen: RGB565 native, uploaded in place (`known-issues.md`).
7. Only if the profile shows them: sampled rechecks of stable textures,
   EFB copies on the GPU, SSE `C_MTXConcat` (bit-identical), prefetches in
   hot list walks, per-TU `-O3`/`-Os` (`perf.md`).

### 3. Correctness and process

- TEV swap tables and indirect textures (`known-issues.md` "Not ported"),
  `test_rc.py` as a host test for `xbox_tev_rc.c`.
- Host tests in CI; fixed autopad scenarios (title demo, town walk) as the
  standard perf run with before/after shots as the merge gate; an audio
  risk check before every console build; pull logs before a relaunch; a
  2-hour burn-in (Melee-X has an open 192 KB-step leak and a long-uptime
  freeze, possibly in shared code).
- Not porting: LED effects (modchip conflict), HSD rewrites, the
  display-list cache (emu64 rebuilds its lists every frame), aurora.

## Console rounds

- Round A: phase 0, phase 1 items 1-4, builtins, kick size, overlap A/B.
- Round B: texture formats, NES, what Round A's profile shows.
- Round C: combiners, the long burn-in.

Rounds A and B go to the console as one combo build. Every change has a
compile-time kill switch; the risky ones also have an `[Xbox]` key in
`settings.ini`, so a regression is reversed on the console without a
rebuild (`architecture.md`, Files on the console).

## Status

Rounds A and B, 2026-10-03, one combo build for the console. `settings.ini`
key (`[Xbox]`, 0 = old behaviour) or compile switch for each:

| item | where | switch | state |
|---|---|---|---|
| sampling profiler, `prof_report.py`, `static_syms.py` | `xbox_prof.c`, `tools/xbox/` | `-DXBOX_PROF=1` | works in xemu |
| `[FRAME]` every 5 s | `xbox_nv2a.c` | `XBOX_FRAME_LOG` | works in xemu |
| session-long `boot.log` (+ `boot2/3.log`), `[BEAT]` | `xbox_io.c`, `xbox_watchdog.c` | `XBOX_LOG_SESSION`, `XBOX_HEARTBEAT_SECS` | built; hardware pending |
| `console.py` stage/deploy/pull/rollback | `tools/xbox/` | — | written; console was off |
| first-fault PGRAPH + pushbuffer dump, stall report | `xbox_nv2a.c` | — | built |
| strict `wait_idle` | `xbox_nv2a.c` | `strict_gpu_wait` | xemu ok |
| vertex cache break per batch | `xbox_nv2a.c` | `vertex_cache_break` | xemu ok |
| inclusive window clip | `xbox_nv2a.c` | `XBOX_CLIP_INCLUSIVE` | xemu ok |
| AC97 start order, stuck/halt recovery, shutdown reset | `xbox_audio.c` | `audio_fix` | hardware only (xemu uses the APU) |
| memcpy & co. builtins | `xbox_prelude.h` | `XBOX_BUILTIN_MEM` | `pc_gx.o` 81 → 18 calls |
| GPU overlap on by default (+ migration) | `xbox_settings.c` | `gpu_overlap` | xemu ok |
| 32 KB kicks | `xbox_nv2a.c` | `pushbuffer_kick_kb` | xemu ok |
| per-draw skips | `xbox_nv2a.c` | `draw_skip` | xemu: shim 1.3 → 0.8 ms |
| native texture formats | `xbox_nv2a.c` | `native_textures` | xemu: 440 of 864 KB saved, shots match |
| texture reuse (NES screen in place) | `xbox_nv2a.c` | `texture_reuse` | built; NES not exercised in xemu |

Not done in this round: atomic `settings.ini` writes, the 128 MB hold,
sampled texture rechecks, EFB copies on the GPU, SSE matrices, prefetches,
per-TU levels (all wait for the console profile), round C.
