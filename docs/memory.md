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
