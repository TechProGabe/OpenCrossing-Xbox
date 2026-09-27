# TEV map — implementation order, the DC GX layer shape, and the verdict

Which classes are hot and should be built first (§7), the `PVRDrawMode` fold
the DC GX layer needs instead of a TEV interpreter (§8), the four questions
one re-harvest would answer (§9), and the feasibility verdict (§10). Read with
`kb/tev-map-table.md` when starting implementation. Part of `kb/tev-map.md`, whose stub maps every § to its file.

**Marking convention used throughout:**
`[F]` = decoded fact (from the seed file or read directly out of the source).
`[I]` = inference. `[?]` = not determinable from available data, needs work.

**Verdict up front:** the fixed-function renderer is feasible. **81 / 101
configs (80 %) map onto stock PVR poly modes with no approximation**, another
3 need a trivial approximation, and the residual **17 need a second texture**
— of which 10 are N64 mip-LOD interpolation that can be dropped outright.
Nothing in the set uses indirect textures, non-identity swap tables, texture
matrices, or EFB feedback in the game render path. `[F]`

---

## 7. Hot paths vs rare configs

**Caveat `[?]`:** the seed carries no draw counts. `shader_cache.bin` is
append-on-first-compile, so seed index ≈ first-encounter order over the
playthrough, which is a *weak* ordering signal, not a frequency measure. The
attributions below are `[I]`, grounded in (a) that ordering, (b) the N64
combiner labels recovered from `combine_manual`, and (c) the folded form.

| likely subsystem | configs | class | why |
|---|---|---|---|
| **Terrain / ground / acre geometry** | the P3 family — `lerp(C2, C1, T)·RASC` (8, 10, 21, 22, 25, 27, 35, 43, 51, 52, 55, 63, 74, 96 …) | P3/A1, P3/A2 | this is the N64 `PRIMITIVE/ENVIRONMENT/TEXEL0` tint idiom; AC recolours ground tiles by season and time of day, and these appear very early in the seed (indices 8–27) with lighting on and fog on |
| **Characters / models** | P2/A1 (0, 1, 16, 32, 33, 36, 39, 46, 56, 71, 72, 83, 84, 88, 98) | P2/A1 | plain `base·tex` with `tex.a`, lighting on, index 0–1 = first thing drawn |
| **Shadows / silhouettes / soft sprites** | P0/A2 (2, 9, 12, 14, 19, 26, 31, 34, 40, 45, 49, 61, 73, 85, 93, 95) — **16 configs** | P0/A2 | flat constant colour, alpha entirely from the texture: the classic blob-shadow / soft-mask draw |
| **UI, fades, letterbox, solid panels** | P0/A0 (3, 6, 15, 17, 18, 47, 53, 54, 59, 64, 70) | P0/A0 | no texture at all, constant alpha; #003 and #006 sit in the `chan_ctrl` "everything from register" group (the 7 unlit/mat=REG/alphaMat=REG configs: 3, 6, 10, 34, 44, 58, 73), which is the JSystem 2D / non-emu64 path |
| **Foliage / fences / cutout edges** | the 15 configs with `a-test = >=ref1` | mixed | `ref1 = tex_edge_alpha`; N64 alpha-to-coverage → **PVR PT list** |
| **Explicit threshold cutouts** | the 8 with `a-test = >=ref0` | mixed | `ref0 = blend_color.a`, `G_AC_THRESHOLD` |
| **Water / animated surfaces** | the `PRIM_LOD_FRAC` two-tile family (50, 67, 68, 75, 77, 79, 80, 90, 020, 030, 042) | P0/P3 + A4 | N64 mip-LOD blending, typical of scrolling water and large ground planes |
| **Rare / one-off** | #086 (`NOISE` combiner, the only `SUB`), #069 + #020 (`TEXEL1_ALPHA` masks), #003 + #089 (the only konst-alpha users) | – | ≤2 configs each |

Practical read `[I]`: the hot classes are **P2/A1, P3/A2, P0/A2 and P0/A0 —
together 62 of 101 configs and, by inspection, the overwhelming majority of
draws.** Every one of them is a stock PVR poly type. The problematic set is
concentrated in mip-LOD blending (droppable) and ground decals (§6.5).

---

## 8. Implementation shape for the DC GX layer

The renderer does **not** need a TEV interpreter. At `pc_gx_flush_vertices`
time, fold the current TEV state to `(base, offset, env_mode, list)` with the
CPU-known values in hand:

```c
typedef struct {
    uint8_t  env;        /* PVR_TXRENV_* */
    uint8_t  list;       /* PVR_LIST_OP_POLY / PT_POLY / TR_POLY */
    uint8_t  specular;   /* oargb enabled (P3 classes) */
    uint8_t  flags;      /* NEEDS_INVERTED_TEX | NEEDS_PREMUL_TEX | SECOND_PASS */
    float    base[4];    /* per-draw part of the base colour; RASC folds in per-vertex */
    float    offset[3];
} PVRDrawMode;
```

* Build a **101-entry lookup table keyed by the same `ShaderKey` hash the ARM
  port already computes** (`hash_key()` in `pc_gx_tev.c`), generated offline
  from this document. Unknown key → fall back to `MODULATE` + vertex colour
  and log it; that is the sm64-dc "wrong colours in places, ships anyway"
  posture, and with 101 known keys it should never fire.
* Per-vertex work in the SH-4 transform loop: compute `RASC` (already
  required — no hardware T&L), multiply into `base`, write `argb`; write
  `oargb` for the 27 P3 configs.
* Texture variants (`inverted`, `premultiplied`) are produced offline by the
  converter and keyed by content hash + variant tag, reusing the existing
  content-hash scheme.
* `PT_ALPHA_REF` is set once per render (§5.5).
* Fog: `k->fog_enabled` selects `PVR_FOG_TABLE` vs `PVR_FOG_DISABLE`
  per poly — a near-native fit, 43 of 101 configs want it.

---

## 9. Open items this document does not close

1. `[?]` **Blend-mode distribution.** How many draws/frame use B1 (alpha-only
   write) and B2 (dst-alpha)? Largest remaining renderer risk (§6.5).
2. `[?]` **Alpha-reference values and their frequency.** Decides whether a
   single global `PT_ALPHA_REF` is viable (§5.5).
3. `[?]` **texmap ground truth for #004 and #099.** Two configs, currently
   assumed.
4. `[?]` **Draw-call frequency per config.** §7 is inference; a counter keyed
   by `ShaderKey` hash on the ARM port would make it fact in one playthrough.
5. All four are answered by the same change: extend `ShaderKey` with
   `tex_map`, `tex_coord`, `alpha_ref0/1`, blend and z state, add a hit
   counter, bump `SDC_VERSION`, re-harvest. **Do this before writing the DC
   renderer.**

---

## 10. Feasibility verdict

**The fixed-function PVR renderer is feasible, and the TEV mismatch is not
the binding constraint on this port.**

* 80 % of configs are exact one-poly one-texture PVR draws.
* The remaining 20 % split into: mip-LOD blends that should simply be deleted
  (10 configs, zero visual cost, PVR filters in hardware), affine forms whose
  coefficients occasionally leave [0,1] and are fixed by an offline inverted
  texture (18 configs, chosen per-draw because the coefficients are
  CPU-known), a 3-config alpha approximation, and 4–5 genuine two-texture
  masks that get baked or a second pass.
* Zero use of the features PVR flatly lacks: indirect textures, swap tables,
  texture matrices, TEV comparison ops, TEV bias/scale, non-PREV output
  registers. One EFB copy in the whole game, used only for screen wipes.
* Alpha compare is a *better* fit than expected: every test in the game is
  `alpha >= ref`, which is precisely PVR punch-through. The only friction is
  that `PT_ALPHA_REF` is global, and that is an offline alpha-rebake problem,
  not an architecture problem.
* The real architectural risks the TEV work surfaced are **not** combiner
  maths: they are (a) emu64's N64 decal path (alpha-only write + destination
  alpha blend), which PVR cannot express and which needs a depth-bias
  substitute, and (b) that all of `RASC` — 8-light GC lighting for 44 of 101
  configs — moves onto the SH-4. Both are already tracked in `PLAN.md`
  §3.2/§3.3.
