# Melee-X backport

Melee-X (the Melee port) started from this repo's renderer, audio driver
and crash reporter and went about 45 console builds further. Most of what
it fixed lives in code the two ports share. This records what came back
(2026-10-03, console rounds v1 to v14) and what's left. Settled facts live
in the matching doc; this file is the index.

## What came back

Each item has a compile-time kill switch (`renderer.md`, Kill switches);
the risky ones also have an `[Xbox]` key in `settings.ini`, so a
regression is undone on the console without a rebuild.

| item | where | switch | on hardware |
|---|---|---|---|
| sampling profiler, `prof_report.py`, `static_syms.py` | `xbox_prof.c`, `tools/xbox/` | `-DXBOX_PROF=1` | first profile v4 (`perf.md`) |
| `[FRAME]` every 5 s | `xbox_nv2a.c` | `XBOX_FRAME_LOG` | yes |
| session-long `boot.log` (+ `boot2/3.log`), `[BEAT]`, `*_prev.log` | `xbox_io.c`, `xbox_watchdog.c` | `XBOX_LOG_SESSION`, `XBOX_HEARTBEAT_SECS` | yes |
| `console.py` stage/deploy/pull/rollback | `tools/xbox/` | | every round |
| first-fault PGRAPH and pushbuffer dump, stall report | `xbox_nv2a.c` | | caught the v4 pitch fault |
| strict `wait_idle` | `xbox_nv2a.c` | `strict_gpu_wait` | yes |
| vertex cache break per batch | `xbox_nv2a.c` | `vertex_cache_break` | yes |
| inclusive window clip | `xbox_nv2a.c` | `XBOX_CLIP_INCLUSIVE` | yes |
| AC97 start order, stuck/halt recovery, shutdown reset | `xbox_audio.c` | `audio_fix` | yes |
| audio producer above the game thread, `[BEAT] audio starved` | `xbox_audio.c` | `audio_priority` | no starvation after boot (v7) |
| memcpy & co. builtins, inline `words_eq` | `xbox_prelude.h`, `xbox_nv2a.c` | `XBOX_BUILTIN_MEM` | yes |
| CPU/GPU overlap on by default (+ migration) | `xbox_settings.c` | `gpu_overlap` | yes |
| 32 KB kicks | `xbox_nv2a.c` | `pushbuffer_kick_kb` | yes |
| per-draw skips | `xbox_nv2a.c` | `draw_skip` | yes |
| native texture formats | `xbox_nv2a.c` | `native_textures` | yes |
| texture reuse, NES upload two texels a word | `xbox_nv2a.c` | `texture_reuse`, `XBOX_NES_FAST` | NES 52 → 59 fps |
| FPS counter, screenshots | `xbox_nv2a.c`, `xbox_fbdump.c`, `xbox_main.c` | `fps_counter`, `screenshots` | yes |
| safe video (BACK at boot = 480i) | `xbox_settings.c` | | not tried yet |
| display auto-detection (Output and Widescreen Auto) | `xbox_settings.c`, `xbox_settings_menu.c` | `XBOX_VIDEO_AUTO` | v14 (480i console) |
| restart through the kernel's XBE path | `xbox_settings.c` | | v14 |
| the z-buffer kept on after `pb_target_back_buffer` | `xbox_nv2a.c` | `XBOX_ZBUFFER` | v14 |
| 128 MB consoles run as 64 MB | `xbox_ramlock.c` | `XBOX_RAM_LOCK64` | xemu (Cerbios) only |

Fixed on the way, from console logs: the clock (`osGetTime` overflow,
nxdk's failing `mktime`, a save's bad `time_delta`), the NES heap at 720p,
the NES picture drawn twice (`famicom_draw`), the texture pitch fault, a
watchdog wait that could last forever, Clu Clu Land D's crash. Ours only:
the texture cache index (`PC_TEX_CACHE_INDEX`), the CPU clock check, the
Options menu in the game's style.

## Left

Each waits for a console profile or a report that points at it:

- `settings.ini` written atomically (temp file, read back, rename).
- Sampled rechecks of stable textures, EFB copies on the GPU, SSE
  `C_MTXConcat` (bit-identical), prefetches in hot list walks, per-TU
  `-O3`/`-Os` (`perf.md`).
- TEV swap tables and indirect textures (`known-issues.md`, Not ported),
  with `test_rc.py` as a host test for `xbox_tev_rc.c`.
- Host tests in CI; fixed autopad scenarios (title demo, town walk) as the
  standard perf run with before/after shots.
- A 2-hour burn-in on hardware (Melee-X has an open 192 KB-step leak and a
  long-uptime freeze, possibly in shared code). xemu ran 32 minutes in town
  with flat memory (2026-10-03).

Not porting: LED effects (modchip conflict), HSD rewrites, the
display-list cache (emu64 rebuilds its lists every frame), aurora.
