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

- Constants from c96: projection with the viewport folded in (96-99), MV rows
  (100-102), normal rows (103-105), k=(0,1,.5,0) (106), material/ambient,
  flags, fog (start, 1/(end−start)), 8 light dirs + colours, 3 texgen stages.
- Per-vertex GC channel-0 lighting (the GameCube lights per vertex too).
- Fog factor → `oSpecular.w` (V1.a), read by the final combiner.
- Texgen per TEV stage: tc0 or normal × tex matrix, × NPOT pad scale.

## Combiners (`xbox_tev_rc.c`)

- PREV = R0; REG0-2 allocated from R1/T3/V1.rgb; T0-T2 = stage textures;
  V0 = rasterised colour.
- A TEV stage is one NV2A stage when its lerp collapses (modulate/replace/
  decal), else two. Constants: C0/C1 per stage, resolved per draw.
- Final combiner: `lerp(fog, PREV, fog colour)`, alpha = PREV.a.
- Unsupported configs set `approximated` (counted in the frame log).

## Textures

RGBA8 from `pc_gx_texture.c`, **swizzled**, NPOT padded to POT by edge
replication, texcoords rescaled in the vertex program. One contiguous pool
(8 MB at 480, 5 MB at 720p), first-fit + coalesce, frees deferred until the
frame's GPU work is done. `pc_gx_texture.c` finds cached textures through a
hash index (`PC_TEX_CACHE_INDEX`, `patches.md`).

Native formats (`classify` in `xbox_nv2a.c`, from Melee-X): each upload is
stored in the smallest format that holds every texel: Y8 / AY8 (grey, I4/I8),
A8Y8 (grey with alpha, IA), R5G6B5, A1R5G5B5 (most RGB5A3 and CI), A4R4G4B4,
else A8R8G8B8. Grey and alpha are exact. A 5/6-bit channel is accepted when
it is `x * 255 / 31` (the decoder) or bit replication (a framebuffer read
back) of some x; the NV2A expands by replication, as the GameCube does, so
such a texel can be 1/255 off the decoder's value. The NES screen is stored
as R5G6B5, two texels a word with red and blue swapped in place (256 wide:
no column padding, and swizzled x, x + 1 are neighbours); the per-texel
loop took 3 ms of a 19 ms NES frame on the console. xemu title demo: 440 of
864 KB saved, screenshots identical but for ±1 on 16-bit texels. A re-upload of the same size and format rewrites
the texture in place (`texture_reuse`) when the GPU can't be reading it.
`perf.log`'s minute line counts uploads by stored size and the KB saved.

## FPS counter

`fps_overlay` (from Melee-X): frames presented over the last half second,
drawn at present as clear-rect colour fills (one pushbuffer block) of the
lit runs of 5x7 digits, yellow on black, 2x (3x at 720p), inside the
TV-safe area. `fps_counter` in
`settings.ini` / Options > Video, live. `-DXBOX_FPS_DEFAULT=1` makes it the
default for test builds. It sets the window clip to the whole screen first
(the NES picture leaves a pillarboxed one) and makes the next GX draw send
its own again.

## Screenshots

`screenshots = 1` (Options > Video, default off): a right stick click
(`pc_platform_poll_events`; BACK is the pause menu here, unlike Melee-X)
asks `xbox_nv2a_shot`, and the next present writes the frame, FPS counter
included, as a 24-bit BMP (`xbox_fbdump_bmp`, from Melee-X) to UDATA
`shotNN.bmp`. The first shot of a boot takes the first free number, so
older shots stay; after 99 it wraps. ~0.27 s at 720p in xemu (rows written
8 at a time; one write a row took 1.2 s). `console.py pull` fetches them and
never deletes them. `-DXBOX_NES_SHOT=N` writes `nes_shot.bmp` the same way
at the Nth frame of each NES game.

Clear colours go to `pb_fill` as A8R8G8B8: pbkit converts them to the
surface's format itself. Converting them to R5G6B5 first made every clear
colour near black at 720p (fixed in round C; Melee-X had the same bug).

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
value is ignored once and the file rewritten. With it on,
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
(`video_select`: `video_720p` on, the default Auto, dashboard allows it on
the AV pack, and at least `XBOX_720P_MIN_FREE_KB` free; how the mode and
aspect are picked: `architecture.md`, Video output). pc_gx.c's logical
screen is `g_pc_window_w` x `g_pc_window_h` (640x480 or 854x480).
`gl_viewport` / `gl_scissor` scale
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

## Depth

emu64 hands GX projections whose clip z/w runs from -1 (near) to 0 (far),
and pc_gx.c passes them through with `glDepthRange` (0..1 from the N64
viewport). `mat4_rows_mul` folds the GL viewport transform into the
projection rows (`ZMAX * ((df - dn) / 2 * z/w + (df - dn) / 2 + dn)`, ZMAX
2^24-1 for Z24S8, 65535 for Z16 at 720p), and `gx.vsh` writes the divided
screen z: the NV2A runs as a z-buffer, compare `GL_LEQUAL` etc. straight
from `GXSetZMode`. Visible depth uses [dn, (dn + df) / 2], half the buffer
(GL's -1..1 convention over GX's -1..0), as on the PC port.

**CONTROL0 must keep `Z_PERSPECTIVE_ENABLE` off.** pbkit's
`pb_target_back_buffer` sets CONTROL0 to `0x00110001` ("We use W") every
time it is called, and `frame_open` calls it every frame; `setup_state`'s
z-buffer CONTROL0 was only sent once at init. So from the second frame on
the NV2A stored the interpolated w, the eye distance, as an integer: one
depth step per world unit, at Z24S8 and Z16 alike. The villager's shirt hem
lies within a unit of the trousers (comb-shaped red/blue z-fighting, hardware
shots v11 `shot09`-`shot13`), and the pockets-menu glove is drawn about 3
units in front of a near = 1 camera (`mHD_hand_shape_draw`), so all of it
fell on two or three depth values. `frame_open` now sends CONTROL0 again
after `pb_target_back_buffer` (`CONTROL0_ZBUF`); the init log line says
`z-buffer 1`. Kill switch: `-DXBOX_ZBUFFER=0`. Melee-X hit the same pbkit
default (its `renderer.md` "Depth"). xemu emulates the w-buffer, and showed
the hem and the glove exactly as the console did; with the fix both are
clean at 720p and 480, and snow, the UI and the pockets menu are unchanged.
Depth is now culled before the near plane (`ZMIN_MAX_CONTROL`
CULL_NEAR_FAR on z, not w), as the GameCube clips it.

z-buffer resolution, half range: step ≈ D² / (near × 2^23) at Z24, × 256 at
Z16. Outdoors (near 145-200, far 1600, `Camera2`) at D ≈ 500 that is ~0.05
units at Z16; indoors (near 20) at D ≈ 300 ~0.14; the glove (near 1, D ≈ 3)
~0.0003.

## Register values that bit us (see traps.md)

- `TEXTURE_CONTROL1` pitch: a multiple of 64 even for swizzled textures,
  which don't use it. A 4x4 AY8 gave 4 and a PGRAPH data error on hardware.
- `TEXTURE_FORMAT` bit 3 = 1 (border from colour).
- `SPECULAR_ENABLE` 1 + `LIGHT_CONTROL` `ALPHA_FROM_MATERIAL_SPECULAR`.
- `FRONT_FACE` CCW.

## Debug knobs (compile-time, `XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=…'"`)

| knob | effect |
|---|---|
| `XBOX_FBDUMP_EVERY=N` | screenshot every N frames over COM1 + frame stats (seconds per frame in xemu: shows up as a `[HITCH]`) |
| `XBOX_NES_SHOT=N` | `nes_shot.bmp` at the Nth frame of each NES game |
| `XBOX_FPS_DEFAULT=1` | FPS counter on by default (test builds) |
| `XBOX_PROF=1` | sampling profiler (`perf.md`) |
| `XBOX_DBG_TEVLOG` | log every new TEV config and its combiner words |
| `XBOX_DBG_DRAWLOG=N` | log every draw of frame N (state, texture, first vertex) |
| `XBOX_DBG_RC_TEX` | every draw outputs raw T0 (isolates TEV from geometry) |
| `XBOX_DBG_NOFOG` / `XBOX_DBG_NOCULL` | force fog / culling off |
| `XBOX_DBG_AUDIO` | audio DMA/APU cursor + peak every 2 s |
| `XBOX_DBG_WEATHER=N` | force the weather (1 rain, 2 snow) |
| `XBOX_DBG_CRASH_FRAME=N` | fault on purpose at frame N (tests `xbox_crash.c`) |
| `XBOX_DBG_NES_TEST=N` | draw RGB565 colour bars through the NES screen path from frame N (120 frames) |
| `XBOX_DBG_SAVE_FROM_D` | saves (and the save-folder scan) and `settings.ini` read from `D:\` first (stage them with `OCX_STAGE_EXTRA`) |
| `XBOX_DBG_FRESH_UDATA` | an empty `E:\UDATA\4f43ff01`: a first boot |
| `XBOX_HITCH_MS=N` | `[HITCH]` threshold (40 ms; 1 logs every frame) |
| `XBOX_PACE_MISSES=N` | `[PACE]` threshold, missed vblanks a second (6; 0 = off) |
| `-DXBOX_AUTOPAD=N` (CMake var) | scripted START/A presses from PADRead call N (`xbox_autopad.c`) |
| `-DXBOX_AUTOPAD=script` (CMake var) | plays `D:\autopad.txt` (up to 1024 steps): pad buttons, SDL controller events (pause menu, rebinding, `RS` for screenshots), stick holds, one-shot screenshots, log marks, `@480`/`@720` lines (`xbox_autopad.c` header) |

## Kill switches

Every optimisation and behaviour change has one; the default is the good
build. Compile-time `-D…=0` unless noted; where there is a `settings.ini`
key it works without a rebuild (`architecture.md`).

| switch | `settings.ini` | turns off |
|---|---|---|
| `XBOX_NATIVE_TEX` | `native_textures` | native texture formats (all A8R8G8B8) |
| `XBOX_TEX_REUSE` | `texture_reuse` | in-place re-uploads |
| `XBOX_DRAW_SKIP` | `draw_skip` | per-draw dirty tracking |
| `XBOX_VB_CACHE_BREAK` | `vertex_cache_break` | the vertex cache break per batch |
| `XBOX_STRICT_IDLE` | `strict_gpu_wait` | the strict GPU idle check |
| `XBOX_PB_KICK` | `pushbuffer_kick_kb` (16 = old) | 32 KB kicks |
| `XBOX_GPU_OVERLAP` | `gpu_overlap` | CPU/GPU overlap |
| `XBOX_AUDIO_FIX` | `audio_fix` | AC97 start order, recovery, shutdown reset |
| `XBOX_AUDIO_PRIORITY` | `audio_priority` | the audio producer above the game thread |
| `XBOX_ZBUFFER` | | the z-buffer fix ("Depth"): pbkit's w-buffer |
| `XBOX_NES_FAST` | | the NES upload, two texels a word |
| `XBOX_VC_DELTA` | | changed vertex-constant rows only (sends all 41) |
| `XBOX_PB_GUARD` | | mid-frame pushbuffer restart |
| `XBOX_VBL_PACE` | | vblank pacing (`pc_vi.c`'s timer limiter) |
| `XBOX_CLIP_INCLUSIVE` | | inclusive window-clip maximum |
| `XBOX_DISC_DIRECT` | | direct disc image reads (pdclib) |
| `XBOX_BUILTIN_MEM` | | `mem*` builtins (prelude) |
| `XBOX_VIDEO_AUTO` | | video Auto: 480 and 4:3 defaults, no `opt_version` 4 move (a file it saves is at version 4 and never moves later) |
| `XBOX_CLOCK_CHECK` | | the CPU clock check at boot |
| `XBOX_RAM_LOCK64` | | holding a 128 MB console's upper 64 MB |
| `XBOX_CRASH_GUARD`, `XBOX_LASTLOG_SECS`, `XBOX_LOG_SESSION`, `XBOX_FRAME_LOG`, `XBOX_HEARTBEAT_SECS` | | crash reporter, rolling `last.log`, session-long `boot.log`, `[FRAME]`, `[BEAT]` |
| `PC_TEX_CACHE_INDEX` | | the texture cache's hash index (linear scan) |
| `-DXBOX_WIDESCREEN=OFF` (CMake) | | 16:9 and 720p (`pc_gx.c` etc. without `PC_ENHANCEMENTS`) |
| `-DXBOX_TITLE_MENU=OFF` (CMake) | | the title menu (plain "Press Start") |

## The NES screen

`pc_nes_fixnes.c` uploads the NES frame (256×224 RGB565, red in the low
bits) with `glTexImage2D` and draws it with its own GLSL program. The shim
stores it as R5G6B5 with red and blue swapped (Textures, above) and draws
any non-GX program as `blit_draw`: a quad sampling texture unit 0 through
the GX vertex program (identity matrices) and a one-stage "output T0"
combiner, 4:3 between black bars when `nes_aspect = 1` (the default) at
16:9. `famicom.cpp`'s GameCube quad is skipped on the Xbox: it drew the
picture a second time, full screen (`patches.md`).

## Not yet

See `known-issues.md`: TEV swap tables, indirect textures.
