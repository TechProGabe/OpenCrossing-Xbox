# Memory (64 MB)

Budget: 64 MB unified, minus kernel + XBE image + GPU (push buffer, framebuffers,
textures). Assume ~50–55 MB for the game until measured on xemu at 64 MB.

## PC port allocations (from `docs/ref/dc/` survey of the same code, verify)

| allocation | size | source |
|---|---|---|
| main arena (MEM1) | 24 MB | `PC_MAIN_MEMORY_SIZE` |
| ARAM emu | 16 MB malloc | `pc_aram.c` |
| — soundAram | 8.44 MB (`audiorom.img` 8.3 MB preloaded) | `jsyswrap.cpp` |
| — graphAram | 6.96 MB (RARC archives) | `jsyswrap.cpp` |
| vertex batch buffer | 3.1 MB | `pc_gx_internal.h` |
| streaming VBO | 6 MB GPU | `pc_gx.c` |
| binary | ~11 MB (bulk = compiled-in `src/data/` tables) | armhf ELF |

Steady state ≈ 43–45 MB + 6 MB GPU → does not fit as-is with a framebuffer and
textures on top.

## Levers (cheapest first)

1. Size ARAM emu to use, not 16 MB; stream `audiorom.img` from HDD instead of
   preloading 8.3 MB (DC did both: `DC_ARAM_WINDOW`).
2. Shrink streaming vertex buffer 6 MB → measured peak.
3. Texture cache budget in bytes, 16-bit formats where lossless enough.
4. Trim arena to measured high-water mark.
5. `-Os` for cold TUs (DC measured big `.text` savings).

Each lever gets a kill switch. Record measurements here.

## Measured (xemu, 64 MB, title demo, 2026-09-27)

| item | size | knob |
|---|---|---|
| XBE image (incl. BSS) | 25.3 MB | `PC_GX_MAX_VERTS=16384` cut `g_gx` by 4.5 MB |
| main arena | 6 MB | `XBOX_ARENA_BYTES` |
| ARAM | sparse 32 KB pages; audiorom + RARC data disc-mapped → ~0 resident | `XBOX_ARAM_FLAT=1` restores flat 16 MB |
| ARAM disc cache | 48 × 32 KB = 1.5 MB LRU (≈93% hits at title; cold after boot, so the first title demo misses: `perf.md`) | `XBOX_ARAM_CACHE_SLOTS` |
| NV2A texture pool | 8 MB contiguous (title uses ~0.8 MB) | `xbox_nv2a.c` |
| vertex ring / pushbuffer | 1 MB / 1 MB | `xbox_nv2a.c` |
| **free at runtime** | **~5.9 MB** | |

Order matters: `pc_assets_init` (REL + Yaz0 peak) runs before NV2A init.

## 720p (xemu, 64 MB, title demo, 2026-09-28)

| | 480 (640x480x32, Z24S8) | 720p (1280x720x16, Z16) |
|---|---|---|
| free before GPU init | 35.7 MB | 35.7 MB |
| free after GPU init | 20.6 MB | 20.7 MB |
| texture pool | 8 MB | 5 MB |
| free at the title demo | 5.5 MB | 5.6 MB |

Framebuffers: 3 colour + 1 depth. 720p at 32-bit colour would need ~9.8 MB
more than 480, so it runs at R5G6B5 with Z16 (+2.5 MB), plus 0.6 MB for the
bigger XVideo buffer, minus 3 MB of texture pool. `video_select`
(`xbox_nv2a.c`) only switches when `XBOX_720P_MIN_FREE_KB` (32 MB) is free
before GPU init, and falls back to 480 if the allocations fail. The smaller
pool is the risk: judge it on hardware (`perf.log` tex KB, `[NV2A] texture
pool full` lines) in town, houses and the museum.

On hardware (beta-3, 2026-09-28) 720p starts, looks right and runs at
60 fps; the pool in busy rooms has no measurement yet.

## 128 MB consoles (2026-10-03)

A console upgraded to 128 MB runs as a stock 64 MB one. cxbe marks every
nxdk XBE "limit to 64 MB" (XBE init flags `0x124`, bit `0x4`), and a 128
MB-aware BIOS honours it: Cerbios in xemu at `mem_limit = '128'` reports
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
