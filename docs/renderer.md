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

RGBA8 from `pc_gx_texture.c` → A8R8G8B8 **swizzled**, NPOT padded to POT by
edge replication, texcoords rescaled in the vertex program. 8 MB contiguous
pool, first-fit + coalesce, frees deferred until the frame's GPU work is done.

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

## Not yet

See `known-issues.md`: the NES emulator's GL path (skipped, logged once),
TEV swap tables, indirect textures.
