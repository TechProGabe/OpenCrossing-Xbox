# Memory (64 MB)

The game runs on a stock 64 MB Xbox: kernel, the XBE image (code, data and
BSS), the game's arena and heap, and the GPU's framebuffers, texture pool,
vertex ring and pushbuffer all share it. The `[MEM]` lines in `boot.log`
and `free` in `[BEAT]` / `perf.log` are the numbers to watch.

## Where it goes

Measured in xemu at 64 MB (title demo 2026-09-27, town 2026-10-03):

| item | size | knob |
|---|---|---|
| XBE image (incl. BSS) | 25.6 MB | `PC_GX_MAX_VERTS=16384` cut `g_gx` by 4.5 MB |
| main arena | 6 MB | `XBOX_ARENA_BYTES` |
| ARAM | sparse 32 KB pages; `audiorom.img` and the RARC data are mapped from the disc image, ~0 resident | `XBOX_ARAM_FLAT=1` restores a flat 16 MB |
| ARAM disc cache | 48 × 32 KB = 1.5 MB LRU (~93% hits at the title; cold after boot, `perf.md`) | `XBOX_ARAM_CACHE_SLOTS` |
| NV2A texture pool | 8 MB at 480, 5 MB at 720p | `XBOX_TEX_POOL_BYTES`, `XBOX_TEX_POOL_720P_BYTES` |
| vertex ring / pushbuffer | 1 MB / 1 MB | `xbox_nv2a.c` |
| log queue | 2 × 64 KB | `PEND_BYTES` (`xbox_io.c`) |
| free in town | ~4.6 MB (480 and 720p) | |

Order matters: `pc_assets_init` (the REL and its Yaz0 peak) runs before
GPU init, so the peak and the framebuffers never overlap.

The release after beta-3 started with ~385 KB less free than beta-3 (the
image grew: a 2 × 128 KB log queue, the bigger texture table, the texture
cache index, code). The queue went to 2 × 64 KB (no hardware log has ever
dropped a line) and the screenshot buffer is allocated only while a shot is
written: 160 KB back.

## 720p

| | 480 (640x480x32, Z24S8) | 720p (1280x720x16, Z16) |
|---|---|---|
| free before GPU init | 35.7 MB | 35.7 MB |
| free after GPU init | 20.6 MB | 20.7 MB |
| texture pool | 8 MB | 5 MB |
| free at the title demo | 5.5 MB | 5.6 MB |

Three colour buffers and one depth buffer. At 32-bit colour 720p would need
~9.8 MB more than 480, so it runs at R5G6B5 with Z16 (+2.5 MB), plus 0.6 MB
for the bigger XVideo buffer, minus 3 MB of texture pool. `video_select`
(`xbox_nv2a.c`) only switches when `XBOX_720P_MIN_FREE_KB` (32 MB) is free
before GPU init and falls back to 480 if the allocations fail. On hardware
720p runs at 60 fps in town; the pool's peak in town is ~2.5 MB (the
beta-3 build overflowed it at 720p in xemu; this one doesn't). The busiest
rooms (museum, full houses) aren't measured yet: watch `perf.log`'s tex KB
and `[NV2A] texture pool full`.

## NES games

The emulator gets its own heap at start (`famicom_emu_init`): 4 MB on the
PC port, which fails at 720p, so the Xbox tries 3.5 / 3 / 2.75 MB
(`[NES] emulator heap` line). On hardware at 720p it gets 3 MB and leaves
~436 KB free while a game runs; at 480 the full 4 MB fits and leaves ~316 KB
(both before v14's 160 KB). Enough, but one big allocation from failing
(`known-issues.md`).

## 128 MB consoles

A console upgraded to 128 MB runs as a stock 64 MB one. cxbe marks every
nxdk XBE "limit to 64 MB" (XBE init flags `0x124`, bit `0x4`), and a
128 MB-aware BIOS honours it: Cerbios in xemu at `mem_limit = '128'` reports
`free 38184 KB of 131072 KB` at boot, as GitHub issue #2's console did
(`free 38644 KB of 131072 KB`). The stock (Complex 4627) BIOS in xemu sees
only 64 MB at `mem_limit = '128'`.

For a kernel that hands the upper 64 MB out anyway, `xbox_mem_lock64`
(`xbox_ramlock.c`, first thing in `main_body`) commits every free page in a
top-down reservation, keeps the ones at or above 64 MB, decommits the rest,
and repeats while a pass finds some. They are never given back. Melee-X's
way (`xhw_mem_hold_upper`: contiguous blocks between 64 and 128 MB) held
4 KB of the 64 MB in xemu with the flag cleared. Kill switch
`-DXBOX_RAM_LOCK64=0`. Why not use the RAM: `architecture.md`, "Decided
against".

xemu, 480, fresh UDATA, autopad new game through Rover's train into town
(frame ~12000), no crash in any:

| | 64 MB (Complex) | 128 MB, Cerbios, XBE as built | 128 MB, Cerbios, flag cleared |
|---|---|---|---|
| boot log | no `128 MB` line | nothing above 64 MB is free (60 ms) | upper 64 MB held: 65140 KB, 2 passes, 137 ms |
| free at boot | 38236 KB | 38184 KB | 38332 KB |
| after GPU init | 20236 KB | 20184 KB | 20332 KB |
| in town | 4584 KB | 4532 KB | 4676 KB |
| texture pool peak | 5240 KB | 5130 KB | 5130 KB |

## If memory gets tight

Cheapest first, each behind a kill switch: a smaller 720p texture pool
while an NES game runs; fixNES's 508 KB noise table shared or shrunk; the
texture pool sized to its measured peak; `-Os` for cold translation units
(the Dreamcast port measured big `.text` savings).
