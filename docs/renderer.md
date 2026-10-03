# Renderer

Pipeline: game N64 DLs → **emu64** (`src/static/libforest/emu64/`) → GX calls
→ `pc/src/pc_gx*.c` (unmodified: batching, state dedup, AABB cull, texture
decode + cache) → **GL 3.3 subset** → `xbox/src/xbox_nv2a.c` → pbkit
pushbuffer → NV2A.

`pc_gx.c` loads GL through glad; `xbox_gl_nv2a_load()` fills those glad
pointers with the shim, so `pc/` has no Xbox branches. Only `pc_gx_tev.c`
(GLSL) is replaced, by `xbox_gx_tev.c` (one program id).

## Pieces

| file | job |
|---|---|
| `xbox/src/xbox_nv2a.c` | GL shim: uniform table, textures, vertex ring, state → NV097 methods, present |
| `xbox/shaders/gx.vsh` | the one vertex program (NV2A asm → `gx_vsh.inl` via `tools/xbox/build_shaders.sh`) |
| `xbox/src/xbox_tev_rc.c` | TEV config → register-combiner program (cached per config) |
| `xbox/include/xbox_nv2a.h` | shared types, constant-reference tags |
| `tools/xbox/patch_pbkit.py` | builds nxdk's pbkit with GPU errors recorded instead of halting the console |

## Vertex program (`gx.vsh`)

- Constants from c96: projection with the viewport folded in (96–99), MV rows
  (100–102), normal rows (103–105), k=(0,1,.5,0) (106), material/ambient,
  flags, fog (start, 1/(end−start)), 8 light dirs + colours, 3 texgen stages.
- Per-vertex GC channel-0 lighting (the GameCube lights per vertex too).
- Fog factor → `oSpecular.w` (V1.a), read by the final combiner.
- Texgen per TEV stage: tc0 or normal × tex matrix, × NPOT pad scale.

## Combiners (`xbox_tev_rc.c`)

- PREV = R0; REG0–2 allocated from R1/T3/V1.rgb; T0–T2 = stage textures;
  V0 = rasterised colour.
- A TEV stage is one NV2A stage when its lerp collapses (modulate/replace/
  decal), else two. Constants: C0/C1 per stage, resolved per draw.
- Final combiner: `lerp(fog, PREV, fog colour)`, alpha = PREV.a.
- Unsupported configs set `approximated` (counted in the frame log).

## Textures

RGBA8 from `pc_gx_texture.c`, **swizzled**, NPOT padded to POT by edge
replication, texcoords rescaled in the vertex program. 8 MB contiguous pool,
first-fit + coalesce, frees deferred until the frame's GPU work is done.

Native formats (`classify` in `xbox_nv2a.c`, from Melee-X): each upload is
stored in the smallest format that holds every texel: Y8 / AY8 (grey, I4/I8),
A8Y8 (grey with alpha, IA), R5G6B5, A1R5G5B5 (most RGB5A3 and CI), A4R4G4B4,
else A8R8G8B8. Grey and alpha are exact. A 5/6-bit channel is accepted when
it is `x * 255 / 31` (the decoder) or bit replication (a framebuffer read
back) of some x; the NV2A expands by replication, as the GameCube does, so
such a texel can be 1/255 off the decoder's value. The NES screen is stored
as R5G6B5. xemu title demo: 440 of 864 KB saved, screenshots identical but
for ±1 on 16-bit texels. A re-upload of the same size and format rewrites
the texture in place (`texture_reuse`) when the GPU can't be reading it.
`perf.log`'s minute line counts uploads by stored size and the KB saved.

## Per-draw work

`pc_gx.c` sets every uniform before every draw, mostly to the value it
already has. Setters compare and mark dirty groups (`k_ugroup`); `draw()`
rebuilds only the TEV config and combiner program (D_TEV, or a texture
binding/upload: `s_tex_epoch`), the combiner constants (D_TEVK) and the
vertex-constant blocks that changed (projection, modelview, material,
lights, texgen); unchanged rows are not sent (`XBOX_VC_DELTA`). xemu title
demo: ~40% of draws reuse the config, ~20% send no constants, shim CPU
1.3 → 0.8 ms a frame. `draw_skip = 0` rebuilds everything per draw.

`wait_idle` (Melee-X): idle means the pusher caught up, PFIFO's CACHE1 is
empty, the pusher stopped and PGRAPH is idle, seen twice; `pb_busy` alone
passes while methods sit in CACHE1. After 2 s it logs `[NV2A] GPU stalled`
with GET/PUT, the words around GET and PGRAPH's trap and surface registers.
The first GPU fault also logs PGRAPH 0x400700-0x4008FC and the pushbuffer
around GET as it was then.

Every pushbuffer batch starts with `BREAK_VERTEX_BUFFER_CACHE` (the NV2A's
vertex cache reads ahead past a draw's last vertex into ring memory the CPU
writes next; Melee-X saw wedges on hardware only). The window clip's maximum
is inclusive (`x + w - 1`). Batches are kicked every 32 KB.

## Frame pacing: CPU/GPU overlap

On by default since the Melee-X backport (Melee-X runs it on hardware since
its v33); `gpu_overlap = 0` in the `[Xbox]` section of `settings.ini` turns
it off (read once at GPU init). Settings files from before the backport say
`gpu_overlap = 0` because that was the default: without `opt_version` the
value is ignored once and the file rewritten. Then
`xbox_nv2a_present` kicks the pushbuffer and queues the flip without waiting
for the GPU. The next frame's game logic (`game_main`, before emu64 issues
any GL call) runs while the GPU still draws; the first GL call of the next
frame lands in `frame_open`, which drains the GPU, frees last frame's
textures and restarts the pushbuffer and vertex ring at their heads. That drain is
timed and counted as `gpu` in `[HITCH]` and `perf.log`, so the cpu figure
stays the CPU's own work. `-DXBOX_GPU_OVERLAP=0` compiles it out.

Independently, `xbox_nv2a_present` ends with `vbl_pace`: each frame is due
one vblank after the previous one, replacing `pc_vi.c`'s timer limiter at
`max_fps = 60` and during NES play (`perf.md`).

## Screen size, widescreen, 720p

The framebuffer is 640x480x32, or 1280x720x16 when the boot runs at 720p
(`video_select`: setting on, dashboard allows it on the AV pack, and at least
`XBOX_720P_MIN_FREE_KB` free). pc_gx.c's logical screen is `g_pc_window_w` x
`g_pc_window_h` (640x480 or 854x480). `gl_viewport` / `gl_scissor` scale
logical rectangles to framebuffer pixels (edges rounded, so abutting
rectangles stay abutting), `gl_read_pixels` samples the framebuffer pixel
under each logical pixel (EFB copies stay at logical size, so a 720p screen
grab is not a 2048-wide texture), and at 16-bit the clear value is packed to
R5G6B5 and dithering is on. 720p pairs R5G6B5 with a Z16 depth buffer (NV2x
wants colour and depth of the same width; pbkit's depth format is made
settable by `patch_pbkit.py`), cleared by the shim itself, since pbkit's
Z24S8 clear value would leave Z16 at 0.996. At 720p the texture pool is 5 MB
(`XBOX_TEX_POOL_720P_BYTES`). If 720p can't start (pb_init or the pool/ring
allocations fail), init falls back to 480. The texture-pool recovery also
drops pc_gx.c's full-res EFB captures (up to 4, 2 MB each for a screen grab).

## Register values that bit us (see traps.md)

- `TEXTURE_FORMAT` bit 3 = 1 (border from colour).
- `SPECULAR_ENABLE` 1 + `LIGHT_CONTROL` `ALPHA_FROM_MATERIAL_SPECULAR`.
- `FRONT_FACE` CCW.

## Debug knobs (compile-time, `XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=…'"`)

| knob | effect |
|---|---|
| `XBOX_FBDUMP_EVERY=N` | screenshot every N frames over COM1 + frame stats |
| `XBOX_DBG_TEVLOG` | log every new TEV config and its combiner words |
| `XBOX_DBG_DRAWLOG=N` | log every draw of frame N (state, texture, first vertex) |
| `XBOX_DBG_RC_TEX` | every draw outputs raw T0 (isolates TEV from geometry) |
| `XBOX_DBG_NOFOG` / `XBOX_DBG_NOCULL` | force fog / culling off |
| `-DXBOX_AUTOPAD=N` (CMake var, not a C flag) | scripted START/A presses from PADRead call N (`xbox_autopad.c`) |
| `XBOX_DBG_AUDIO` | audio DMA/APU cursor + peak every 2 s |
| `XBOX_DBG_WEATHER=N` | force the weather (1 rain, 2 snow) |
| `XBOX_DBG_CRASH_FRAME=N` | fault on purpose at frame N (tests `xbox_crash.c`) |
| `XBOX_DBG_NES_TEST=N` | draw RGB565 colour bars through the NES screen path from frame N (120 frames) |
| `XBOX_HITCH_MS=N` | `[HITCH]` threshold (40 ms; 1 logs every frame) |
| `XBOX_PACE_MISSES=N` | `[PACE]` threshold, missed vblanks a second (6; 0 = off) |

| `-DXBOX_AUTOPAD=script` (CMake var) | plays `D:\autopad.txt`: timed pad buttons, SDL controller events (pause menu, rebinding), one-shot screenshots, log marks, `@480`/`@720` lines (`xbox_autopad.c` header) |

Backport kill switches (default on; compile-time 0, or the `settings.ini`
key where there is one): `XBOX_NATIVE_TEX` (`native_textures`),
`XBOX_TEX_REUSE` (`texture_reuse`), `XBOX_DRAW_SKIP` (`draw_skip`),
`XBOX_VB_CACHE_BREAK` (`vertex_cache_break`), `XBOX_STRICT_IDLE`
(`strict_gpu_wait`), `XBOX_PB_KICK` (`pushbuffer_kick_kb`, 16 = the old size),
`XBOX_AUDIO_FIX` (`audio_fix`), `XBOX_CLIP_INCLUSIVE`, `XBOX_BUILTIN_MEM`
(prelude), `XBOX_LOG_SESSION`, `XBOX_FRAME_LOG`, `XBOX_HEARTBEAT_SECS=0`.
`-DXBOX_PROF=1` adds the sampling profiler (`perf.md`).

Kill switches (default on): `XBOX_PB_GUARD=0` (no mid-frame pushbuffer
restart), `XBOX_VC_DELTA=0` (upload all 41 vertex-constant rows per draw
instead of the changed ones), `XBOX_CRASH_GUARD=0`, `XBOX_LASTLOG_SECS=0`,
`XBOX_GPU_OVERLAP=0` (C flag; the overlap is also off at runtime unless
`gpu_overlap = 1`), `XBOX_VBL_PACE=0` (`pc_vi.c`'s timer limiter),
`XBOX_DISC_DIRECT=0` (disc image through pdclib), and the CMake options `-DXBOX_WIDESCREEN=OFF`
(no 16:9 / 720p, pc_gx.c etc. without `PC_ENHANCEMENTS`) and
`-DXBOX_TITLE_MENU=OFF` (plain "Press Start").

## The NES screen

`pc_nes_fixnes.c` uploads the NES frame (256×224 RGB565, red in the low
bits) with `glTexImage2D` and draws it with its own GLSL program. The shim
converts RGB565 to A8R8G8B8 on upload and draws any non-GX program as
`blit_draw`: a viewport-sized quad sampling texture unit 0 through the GX
vertex program (identity matrices) and a one-stage "output T0" combiner.

## Not yet

See `known-issues.md`: TEV swap tables, indirect textures.
