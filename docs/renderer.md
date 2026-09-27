# Renderer plan

Pipeline (unchanged from PC port): game N64 DLs → **emu64** (game's own
N64→GX translator, `src/static/libforest/emu64/`) → GX calls → `pc_gx*`
(batching, state dedup, strip→tri, whole-batch AABB cull, texture decode +
cache) → backend. Only the backend changes.

## What transfers from `pc/`

Pure C, keep: batch buffer, cull (60–80% of batches offscreen — the big win),
state dedup, 10 GC texture decoders + content-hash cache, TLUT `is_be` handling.
Dies: `pc_gx_tev.c` (TEV → GLSL), shader disk cache, GL 3.3 VBO/FBO code,
`pc_texture_pack.c`, `pc_model_viewer.c`.

## M3 — bring-up on pbgl (fixed-function)

- GL 1.x immediate-ish / client arrays via pbgl.
- TEV: reuse the DC sibling's **101-config table** (`docs/ref/dc/tev-map-table.md`)
  — every TEV config seen in a full playthrough mapped to fixed-function. On
  Xbox, `GL_ARB_texture_env_combine`-style multi-stage (if pbgl exposes it)
  beats the DC's 1-TMU approximations.
- Lighting: NV2A fixed-function T&L can do GC's 8 lights (verify attenuation
  model vs `pc/shaders/default.vert`); CPU fallback exists in DC's `dc_gx.c`.
- EFB copies (`GXCopyTex`): per-callsite strategy, render-to-texture on NV2A.

## M6 — xgu + register combiners

- Generate combiner programs from TEV state at runtime (≤3 stages → ≤8
  combiner stages), cache by TEV hash (same key PC uses for shader variants).
- Vertex programs for GC lighting + texgen if fixed-function falls short.
- Textures: swizzled on NV2A; DXT for large static textures if RAM needs it.

## Open questions

- Does pbgl expose multitexture combine + enough state for the 101 configs?
- Alpha compare (TEV alpha test with two refs + logic op) → NV2A alpha test
  covers one ref; the DC `tev-map-alpha.md` workarounds apply.
