# TEV map — the configs that do not map cleanly

The 17 two-texture configs (10 of them droppable N64 mip-LOD), coefficients
that leave [0,1] and the inverted-texture trick, the 3-config A3 alpha
approximation, the single `GX_TEV_SUB`, the two blend modes PVR cannot
express (§6), and — added 2026-08-06 — **the whole of class P3, which this
document has always described as easy and which `dc_pvr.c` does not implement at
all (§6.6)**. Read when the table sends you here. Part of `kb/tev-map.md`, whose stub maps every § to its file.

> ## 🔴 [2026-08-06] THE HARDEST CASE IN PRACTICE IS NOT ON THIS PAGE — IT IS §6.6
>
> This file was written around the configs whose *maths* does not fit the PVR.
> The one costing visible pixels today is a config whose maths fits perfectly
> (`MODULATE` + `oargb`, exactly as `kb/tev-map-table.md` prescribes) and which
> **the renderer simply never implemented**: `pv.oargb` is a hardcoded `0` at
> `dc_pvr.c:1988`. That is **27 of the 101 configs**, and it is why the
> name-entry keyboard renders black. See §6.6.

**Marking convention used throughout:**
`[F]` = decoded fact (from the seed file or read directly out of the source).
`[I]` = inference. `[?]` = not determinable from available data, needs work.

## 6. The genuinely problematic configs

### 6.1 Two-texture configs — 17 of 101

These reference two texmaps. Ground truth from `emu64::combine_manual()`
except #004 and #099.

| # | texmaps | folded RGB | folded A | N64 combiner (from `combine_manual`) | strategy |
|---|---|---|---|---|---|
| 007 | T0,T1 ✓ | `C1` (no texture) | `A1*T0a*T1a` | `…TEXEL0, 0, TEXEL1, 0…COMBINED, 0, PRIMITIVE, 0` | **bake**: alpha-only product of two masks |
| 020 | T0,T1 ✓ | `C1*RASC + T0*T1a` | `A1 + A0*T0a*T1a` | `TEXEL0, 0, TEXEL1_ALPHA, 0 … PRIM_LOD_FRAC` | **drop T1** (LOD frac) → `C1*RASC + T0*T0a` → bake premultiplied texture |
| 023 | T0,T1 ✓ | `T0 + C1*RASC + T0*T1` | `T0a*T1a` | `TEXEL1, 0, TEXEL0, TEXEL0 …` | detail-texture modulation; **drop T1** → `T0*(1+T1̄)` with T1̄ folded into base |
| 030 | T0,T1 ✓ | P3 form | `T1a + T0a*T1a` | `…TEXEL0, 0, TEXEL1, TEXEL1 … PRIM_LOD_FRAC` | **drop T1** (LOD frac) |
| 042 | T0,T1 ✓ | `C1` | `A0*T0a*T1a` | `…TEXEL0, 0, TEXEL1, 0 … PRIM_LOD_FRAC` | **drop T1** |
| 050 | T0,T1 ✓ | no texture | `A1*lerp(T0a, T1a, A0)` | `…TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0 …` | **pure mip-LOD lerp — drop T1**, use `A1*T0a` + PVR trilinear |
| 067 | T0,T1 ✓ | no texture | `A1*lerp(T0a, T1a, A0)` | same family | same |
| 068 | T0,T1 ✓ | P3 form | `A1*lerp(T0a, T1a, A0)` | same family | same |
| 069 | T0,T1 ✓ | `C2 + (C1-C2)*T0*T1a` | `A1*T0a*T1a` | `TEXEL0, 0, TEXEL1_ALPHA, 0 …` | genuine **mask decal**: T1.a masks T0 — second pass, or bake the pair |
| 075 | T0,T1 ✓ | no texture | `A1*lerp(T0a, T1a, A0)` | same LOD family | **drop T1** |
| 077 | T0,T1 ✓ | P3 form | `T0a*T1a` | `…TEXEL0, 0, TEXEL1, 0 … PRIM_LOD_FRAC` | **drop T1** |
| 078 | T0,T1 ✓ | `T1 + C1*RASC + RASC*T0` | `A0*T0a*T1a` | `…TEXEL1, 0, TEXEL0, 0 … PRIM_LOD_FRAC` | **drop T1** → affine in T0 → P3 |
| 079 | T0,T1 ✓ | P3 form | `T1a + T0a*T1a` | `…TEXEL0, 0, TEXEL1, TEXEL1 … PRIM_LOD_FRAC` | **drop T1** |
| 080 | T0,T1 ✓ | P3 form | `T0a*T1a` | `…TEXEL0, 0, TEXEL1, 0 …` | **drop T1** |
| 090 | T0,T1 ✓ | P3 form | `A0*T0a*T1a` | `TEXEL0, 0, ENV_ALPHA, 0 … PRIM_LOD_FRAC` | **drop T1** |
| 004 | T0,T1 ? | `T1 + C2*RASC + (C1-C2)*RASC*T0` | `A1 + A0*T0a + A0*T1a` | (no `combine_manual` match — from `combine_auto`) | if same texmap → collapses to P3; verify with the re-harvest |
| 099 | T0,T1 ? | `C1` (no texture) | `T0a*T1a` | (no match) | alpha-only product; if same texmap → `T0a²`, bake |

**Ten of the seventeen carry the N64 `PRIM_LOD_FRAC` idiom** — TEXEL0 and
TEXEL1 are the *two adjacent mip levels of the same texture* and the combiner
lerps between them by the LOD fraction. That is the N64's software trilinear
filter. PVR does trilinear in hardware (`PVR_FILTER_TRILINEAR1/2`) and
bilinear-with-mipmaps is visually adequate. **The correct DC action is to
delete the second tile entirely and let the texture unit filter.** Cost: zero.
Risk: zero. This alone takes 17 problem configs down to ~7.

The residual genuine multitexture is the **`TEXEL1_ALPHA` mask family**
(#069, #020, and the alpha-product configs #007, #042, #099): one texture's
alpha masks another texture's colour. Two options:
* **Bake the pair offline.** The texture cache is already content-hash keyed
  (`pc_gx_texture.c`); extend the offline converter to emit a premultiplied
  `T0.rgb·T1.a` / `T0.a·T1.a` variant keyed by the *pair* hash. Costs VRAM
  only for the pairs that actually occur.
* **Second pass into the TR list** with `MODULATE` and
  `src=DESTCOLOR, dst=ZERO`, which is exactly what GLdc's
  `GL_ARB_multitexture` already does. Costs a second submission of the
  geometry.
Prefer baking; fall back to the second pass where the pair is dynamic.

### 6.2 Base/offset coefficients that leave [0,1] `[F]` structural, `[I]` impact

`pvr_vertex_t.argb` and `.oargb` are 8-bit per channel, i.e. clamped to
[0,1]. The folded coefficients are not always in range. The table below
covers the **56 configs in classes P2 and P3** (the only ones with a base
coefficient); the other 45 have no coefficient to overflow.

| risk (structural, over the 56 P2/P3 configs) | count | configs |
|---|---|---|
| `base` can go **negative** | 18 | 8, 10, 21, 22, 25, 27, 30, 35, 37, 43, 51, 52, 55, 63, 68, 74, 86, 96 |
| `base` can exceed **1** | 4 | 24, 38, 41, 80 |
| `offset` out of range | 8 | 8, 38, 41, 58, 77, 79, 86, 100 |
| **any** coefficient risk (union) | **26** | the above |
| in range by construction | **30** | all other P2/P3 |

"Structural" means the polynomial's *shape* permits it (a negative
coefficient, or a sum that can exceed 1) — whether it actually happens
depends on the runtime values of `C1`, `C2` and `RASC`, which the seed does
not record. `[I]`

The negative-base cases are all the same shape:
`RGB = C2·RASC + (C1 − C2)·RASC·T`, i.e. `lerp(C2, C1, T)` scaled by the lit
vertex colour — the N64 `PRIMITIVE / ENVIRONMENT / TEXEL0` idiom that AC uses
for ground/terrain tinting and day-night colouring. When `C1 < C2` the base
goes negative and PVR clamps it to 0, which flattens the surface to `C2`.

**Because `C1`, `C2` and `RASC` are all CPU-known at submit time, the choice
of formulation can be made per draw call, not per config.** `[I]` The
submission path should be:

```c
/* base = (C1 - C2)*RASC, offset = C2*RASC  -- per component */
if (all components of base in [0,1] && offset in [0,1])
    MODULATE + oargb;                     /* exact */
else if (C1 < C2 componentwise)
    /* lerp(C2,C1,T) == lerp(C1,C2,1-T): use the offline-inverted texture */
    MODULATE(base = C2-C1) + oargb(C1) with texture T' = 1-T;
else
    clamp and accept the error;           /* or split into two passes */
```

The inverted-texture variant is a cheap offline addition (same twiddle/VQ
pipeline, keyed by content hash + "inverted" flag). The `base > 1` cases
(#024, #038, #041, #080) are of the form `base = 1 + RASC`; halve the base and
double the texture, or take the clamp — 4 configs, low stakes.

### 6.3 The A3 alpha form — 3 configs `[F]`

| # | folded alpha |
|---|---|
| 076 | `A1 + A0*T0a` |
| 097 | `A1 + A0*T0a` |
| 089 | `A0*KA + A1*T0a` (KA resolves to 1.0, so `A0 + A1*T0a`) |

PVR alpha can be `Af`, `At`, or `Af·At` — never `Af·At + off`. Approximation:
**drop the constant offset** and use `MODULATEALPHA` with `Af = A0`. The
offset is an alpha floor (the surface never becomes fully transparent);
losing it makes the surface slightly more transparent at the thin end. Three
configs, low stakes. If it shows, clamp `Af` upward instead:
`Af' = A0 + A1` and accept saturation.

### 6.4 The single `GX_TEV_SUB` — config #086 `[F]`

Matches `combine_manual` case 1, N64 combiner
`NOISE, TEXEL0, PRIMITIVE, ENVIRONMENT, …`:

```
stage0 : PREV.rgb = 0.5 − TEXC          (GX_TEV_SUB)   texmap 0
stage1 : PREV.rgb = C2 + PREV·C1                        no texture
stage2 : PREV.rgb = PREV·RASC                           no texture
folded : RGB = (C2 + C1·(0.5 − T))·RASC
             = (C2 + 0.5·C1)·RASC  −  C1·RASC·T   [verified by the parser]
```

Affine in `T` with a **negative** base — the inverted-texture trick in §6.2
handles it exactly (`T' = 1 − T`, base `= C1·RASC`, offset
`= (C2 − 0.5·C1)·RASC`). One config, one baked texture variant. The `NOISE`
input is already dropped by emu64 itself, so nothing is lost relative to the
ARM port.

### 6.5 Blend modes with no PVR equivalent `[F]` the modes exist, `[I]` the severity

Two of emu64's four blend modes have no clean PVR mapping:

* **B1 — alpha-only write** (`GXSetColorUpdate(FALSE)` + `GXSetAlphaUpdate(TRUE)`,
  blend `ONE/ZERO`). PVR has no colour write mask. This is the N64 "write
  coverage into the framebuffer alpha, then use it as a mask" trick.
* **B2 — destination-alpha blend** (`DSTALPHA / INVDSTALPHA`). PVR does have
  `PVR_BLEND_DESTALPHA` / `INVDESTALPHA` factors, but the value they read is
  the on-chip tile accumulation alpha, and B1 (the pass that is supposed to
  *write* that alpha) cannot run — so the pair is broken as a unit.

Both are gated on `ZMODE_DEC && (geometry_mode & G_DECAL_SPECIAL)` — the N64
**decal** path (ground decals, footprints/shadows projected onto terrain,
puddle overlays). Recommended handling, in order:

1. **Render the decal as a normal PT or TR poly with a small depth bias.**
   PVR's per-poly depth compare plus a z-offset in the SH-4 transform gives
   coplanar decals without the alpha-mask trick. This is what every other DC
   port does.
2. If a decal must be masked by another surface's coverage, bake the mask into
   the decal texture's alpha (the mask geometry is static terrain).
3. Failing both, drop the decal (documented DC-edition cut).

**Unquantified** `[?]`: how many draws per frame take B1/B2. Add
`blend_mode/src/dst` + `color_update`/`alpha_update` to `ShaderKey` in the
re-harvest and count. This is the largest remaining unknown in the renderer
plan — bigger than any TEV combiner question.

### 6.6 🔴 CLASS P3 IS NOT IMPLEMENTED — 27 configs, and it is VISIBLE `[F]` (2026-08-06)

**Diagnosed from a human report that the name-entry keyboard renders BLACK.**
This is the first entry on this page that is an *implementation* gap rather than
a hardware one: `kb/tev-map-table.md` §3 says P3 is `base·T + offset` and maps to
`MODULATE` + `oargb`, which is exact. **`dc_pvr.c` implements no part of it.**

**Ruled out first, so nobody re-walks it.** Not a stub: the widget draws at
`m_editor_ovl.c:2221-2258` (`mED_KeyDraw`), 26 display lists, and its assets
`kai_sousa.c` / `kai_sousa2.c` / `lat_sp.c` are on **both** keep lists and in the
generated loader. It is also ruled out *by argument* — alpha here is `TEXEL0`, so
a zeroed texture would be **invisible, not black** (`kb/traps.md`, "a stubbed
asset looks like a renderer bug").

**The config.** 18 of the 26 DLs — every structural piece: `mojibanT`,
`controllerT`, `controller2T`, `shitaT`, `controllpadT`, `cursorT`, `3DT` and
the button caps — use

```c
gsDPSetCombineLERP(PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT,
                   0, 0, 0, TEXEL0)
```

= **TEV config #037, class P3/A1**, `kb/tev-map-table.md` §4 row 37:
`RGB = C2 + (C1 − C2)·T0`, `A = T0a`.

**Why nothing in the renderer catches it** `[F]`:

| path | where it gives up |
|---|---|
| `tev_const_color()` | fails its **first** test (`dc_pvr.c:1000`) — it needs `b`/`c` == ZERO; here they are ENVIRONMENT and TEXEL0 |
| `tev_fold_color()` | `tev_carg_affine()` falls through at `dc_pvr.c:1162` for `GX_CC_TEXC` — so **`-DDC_PVR_TEVFOLD` does not fix it**, and that is a free falsification test |
| `pv.oargb` | **hardcoded `0` at `dc_pvr.c:1988`. PVR offset colour has never been wired.** |

**So what is drawn.** The DL clears `G_LIGHTING`, so the material source is VTX —
the same mechanism as `mFont_SetVertex_dol` (`dc_pvr.c:1209-1216`) — and the
result is `rgb = vtx.rgb · T0.rgb`: **both PRIM and ENV are lost.** 21 of the 26
DLs are wrong. The 5 that survive use the flat `(0,0,0,PRIMITIVE)` shape that
`tev_const_color()` *does* recognise.

**The fix is native and needs no second pass** `[I]`:

```
vertex base colour = PRIM − ENV
per-vertex oargb   = ENV
env                = PVR_TXRENV_MODULATE + the offset-colour bit
```

`pv.oargb` is already stored in the vertex, so the **per-vertex cost is ~zero**.
⚠️ It needs its own kill switch and a **screenshot pair** — widenings in this
family have regressed before (`dc_pvr.c:1080-1098`), and §6.2's out-of-range
coefficient analysis applies the moment `oargb` goes live (18 of the 56 P2/P3
configs can drive `base` negative; #037 is on that list).

**Falsifiable prediction, worth checking before writing any code:** the panel is
black but the 40 key caps are **correctly coloured** (`m_editor_ovl.c:2092` uses
the recognised shape). **If the caps are black too, this diagnosis is wrong.**

⚠️ **Relationship to the open #007 bug (`kb/tev-map-alpha.md` §5.6): #007 loses
both ALPHA factors, #037 loses both COLOUR constants. Same family, opposite
halves** — and both are "the folded polynomial was decoded correctly years ago
and the recogniser is narrower than the table".

---

### 6.6a 🔴 `-DDC_PVR_TEVP3` WAS RUN FOR THE FIRST TIME (2026-08-09) AND IT IS
### **TOO WIDE**, NOT TOO NARROW. DO NOT SHIP IT AS WRITTEN.

The implementation above sat "compile-verified, never run" from session 7 until
2026-08-09, when it was switched on in a town build. It ran, and the gate is
damning:

```
[DC/PVR] tevp3 batches=20305 clamped=6941 of 1587516
```

**Two findings, and the second is the one that matters.**

1. **`clamped=6941` — 34 % of the batches it touched drove `PRIM − ENV`
   NEGATIVE.** §6.2's out-of-range warning above predicted exactly this ("18 of
   the 56 P2/P3 configs can drive `base` negative; #037 is on that list") and
   the prediction is now measured rather than inferred. A clamp is not a
   rounding error here: it turns the subtraction into a different colour, and
   the human report was *"visual regressions"* — a purple/pink cast on the town
   ground, tree trunks and mailboxes.

2. 🔴 **20,305 BATCHES IS THE WRONG ORDER OF MAGNITUDE.** §6.6 is about **26
   display lists** in one widget that is on screen for a few seconds of a
   name-entry screen. A predicate that fires 20,305 times in a 900 s town run —
   in which the keyboard was **never opened** — is not recognising config #037.
   It is recognising a SHAPE that #037 shares with something common, and
   repainting the whole town with it. **The bug is in the predicate, not in the
   `PRIM − ENV`/`oargb` maths**, which §6.6 derives correctly.

**So the next step is NOT "fix the clamp".** It is: instrument which configs
`tevp3` is actually matching (log the combine words for the first N distinct
matches), confirm #037 is among them, and then narrow the predicate until the
count is plausible for the widget. Only then is a screenshot pair meaningful.

⚠️ **It also cost ~10 % of the frame**: `us/v` 2.83 with it on versus 2.57 with
it off, same build otherwise. That is a second reason the predicate is too wide
— a change scoped to one widget cannot cost 10 % of a town frame.

⚠️ **The §6.6 falsifiable prediction is STILL UNTESTED.** Nothing in this run
reached the keyboard (`DC_AUTOSTART` presses index 0 and the keyboard is not on
that path — `kb/RESUME.md` §8). "Is the panel black while the 40 caps are
correctly coloured" remains the cheap first check, and it needs a run that
actually opens the name-entry screen.

**Default stays OFF (`-DDC_PVR_TEVP3` is opt-in).**

---
