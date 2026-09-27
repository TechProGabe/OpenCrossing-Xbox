# TEV map — alpha compare, blend modes, and PVR list assignment

The three alpha-compare settings the game actually uses, what PVR punch-through
gives you, emu64's four blend modes, the OP/PT/TR list-assignment rules, and
the global-`PT_ALPHA_REF`-vs-per-draw-reference problem (§5). Read when wiring
a config to a PVR list. Part of `kb/tev-map.md`, whose stub maps every § to its file.

**Marking convention used throughout:**
`[F]` = decoded fact (from the seed file or read directly out of the source).
`[I]` = inference. `[?]` = not determinable from available data, needs work.

## 5. Alpha compare → PVR list assignment

### 5.1 What the game actually asks for `[F]`

Only **three** alpha-compare settings occur in the whole 101:

| `(comp0, op, comp1)` | count | effective test |
|---|---|---|
| `(ALWAYS, AND, ALWAYS)` | **78** | no test |
| `(ALWAYS, AND, GEQUAL)` | **15** | `alpha >= ref1` |
| `(GEQUAL, AND, ALWAYS)` | **8** | `alpha >= ref0` |

Both tested forms are `AND`ed with `ALWAYS`, so **every alpha test in Animal
Crossing is a single `alpha >= ref`**. No `LESS`, no `EQUAL`, no `OR`/`XOR`
combination, never both halves active at once. `[F]`

The producer is `emu64::alpha_compare()` (`emu64.c:2299-2321`):

```c
comp0 = (othermode_low & G_AC_DITHER) == G_AC_THRESHOLD ? GX_GEQUAL : GX_ALWAYS;
        ref0 = blend_color.a;                       /* N64 gsDPSetBlendColor  */
comp1 = (AA_EN|CVG_X_ALPHA|ALPHA_CVG_SEL set && !(CVG_DST_SAVE|ZMODE_XLU))
        ? GX_GEQUAL : GX_ALWAYS;
        ref1 = tex_edge_alpha;                      /* default 144 (emu64.c:718),
                                                       overridable per-DL by
                                                       G_SETTEXEDGEALPHA        */
GXSetAlphaCompare(comp0, blend_alpha, GX_AOP_AND, comp1, tex_edge_alpha);
```

So `comp1` is the classic N64 **alpha-to-coverage cutout** (foliage, fences,
sprite edges) and `comp0` is the explicit `G_AC_THRESHOLD` cutout.

### 5.2 What PVR gives you `[F]`

* Punch-through list semantics are exactly `pass iff alpha >= PT_ALPHA_REF`
  (Flycast: `if (cp_AlphaTestValue > color.a) discard;` with
  `cp_AlphaTestValue = (PT_ALPHA_REF & 0xFF)/255`, `core/rend/gles/gles.cpp`
  line 1153 and shader line 345-350). Passing fragments then get `alpha = 1.0`.
* **`PT_ALPHA_REF` is a single global PVR register at `0xA05F811C`**
  (`flycast/core/hw/pvr/pvr_regs.h:79` — "Alpha value for Punch Through
  polygon comparison"). It is **not per-poly**; it is latched for the whole
  render. KOS does not even expose a wrapper for it.
* There is **no alpha test at all** outside the PT list.

### 5.3 The blend axis `[F]` from source, `[?]` in the seed

`ShaderKey` does not record `GXSetBlendMode`, so the *joint* distribution of
(alpha-test × blend mode) over the 101 is unknown. But the producer is a
4-way switch, `emu64::blend_mode()` (`emu64.c:2285-2297`):

| # | condition | GX blend | + `zmode()` side effect |
|---|---|---|---|
| **B1** | `ZMODE_DEC && G_DECAL == (GEQUAL\|SPECIAL)` | `BM_NONE (ONE, ZERO)` | `GXSetColorUpdate(FALSE)` + `GXSetAlphaUpdate(TRUE)` — **alpha-only write** |
| **B2** | `ZMODE_DEC && G_DECAL == SPECIAL` | `BM_BLEND (DSTALPHA, INVDSTALPHA)` | **destination-alpha blend** |
| **B3** | `(IM_RD \| FORCE_BL)` set | `BM_BLEND (SRCALPHA, INVSRCALPHA)` | standard translucency |
| **B4** | otherwise | `BM_NONE` | opaque |

`emu64::zmode()` maps `Z_CMP`/`Z_UPD`/`ZMODE_DEC` onto `GXSetZMode` with
`GX_LESS` normally and `GX_LEQUAL / GX_GEQUAL / GX_EQUAL / GX_ALWAYS` for the
decal modes — all of which have direct `PVR_DEPTHCMP_*` equivalents. `[F]`

### 5.4 List-assignment rules `[I]`, derived from 5.1–5.3

```
                 alpha test?      blend            ->  PVR list          blend_src / blend_dst
B4  opaque       none             none                 OP_POLY           ONE / ZERO
B4  cutout       alpha >= ref     none                 PT_POLY           ONE / ZERO      (+ PT_ALPHA_REF)
B3  translucent  none             SRCALPHA/INVSRC      TR_POLY           SRCALPHA / INVSRCALPHA
B3  cutout+blend alpha >= ref     SRCALPHA/INVSRC      PT_POLY preferred; TR_POLY if the
                                                       ref cannot be honoured (see below)
B1  alpha-mask   either           ONE/ZERO, colour-write off   *** no PVR equivalent — §6.5
B2  dst-alpha    either           DSTALPHA/INVDSTALPHA         *** no reliable PVR equivalent — §6.5
```

Additional rules that fall out of PVR's architecture `[I]`:

1. **Depth-sorted draw order stops mattering for TR.** PVR sorts translucent
   fragments per tile in hardware, so the game's back-to-front submission
   order in the XLU display lists becomes a no-op. Keep submitting in game
   order — it costs nothing and preserves behaviour if OIT is ever disabled.
2. **Do not put alpha-tested geometry in TR.** PT is cheaper and gives correct
   depth interaction with the opaque list. AC's cutouts (foliage, fences,
   the town gate lattice, tree canopies) are the single biggest PT population.
3. **Z-write disable maps directly** (`pvr_poly_cxt_t.depth.write`), needed
   for the B3 translucent path where `Z_UPD` is clear.
4. **`GXSetZCompLoc` is meaningless on PVR** (deferred rasteriser, depth is
   always resolved after the PT alpha test). Ignore it. The GX layer already
   stubs it.

### 5.5 The one genuinely hard alpha-compare problem `[F]` + `[I]`

**`PT_ALPHA_REF` is global; AC's references are per-draw.**
The seed proves two *different* reference registers are in use (`ref0` =
`blend_color.a`, varies per display list; `ref1` = `tex_edge_alpha`, default
144 ≈ 0.565, overridable per-DL) but does **not** record their values `[?]`.

Options, in the order to try them:

* **(a) Pin `PT_ALPHA_REF` to the dominant value and rebake alphas.** Set it
  once (start with 144) and, in the offline texture converter, remap the alpha
  channel of textures used by draws with a different reference so the cutoff
  lands at the pinned value. Alpha is already being requantised to
  ARGB1555 / ARGB4444 / paletted, so this is free. This is the primary plan.
* **(b) Route the outliers to TR.** A cutout rendered as a blended
  translucent poly looks nearly identical (soft rather than hard edge) and
  PVR's OIT keeps it correct. Costs TR fill rate; use for the minority.
* **(c) Two renders with different `PT_ALPHA_REF`.** Correct, and far too
  expensive. Do not.

**Measurement needed before committing** `[?]`: add `alpha_ref0`/`alpha_ref1`
to `ShaderKey` (§1.4) and count how many distinct references actually occur
and with what frequency. If the answer is "144 for 95 % of PT draws" — which
is the expectation, since `G_SETTEXEDGEALPHA` is a rarely-emitted DL command
— option (a) alone is sufficient.

### 5.6 🔴 CONFIG #007 LOSES BOTH ALPHA FACTORS — the black wedges `[F]` (diagnosed 2026-08-05)

Transcribed here 2026-08-06 from `kb/state-log.md` and `kb/RESUME.md` §4 item 6,
which were the only two files carrying it. **Still open, not fixed.**

The large flat dark quads a human reported in the town are `ef_shadow_out.c:34-35`,
which records as **two** TEV stages, not the three an earlier reading assumed:

```
stage0 alpha = (ZERO, TEXA, A1,    ZERO)
stage1 alpha = (ZERO, TEXA, APREV, ZERO)      emu64.c:1888-1897
```

= config **#007**, `kb/tev-map-table.md:120`, folded `A = A1·T0a·T1a`, listed in
`kb/tev-map-hard-cases.md` §6.1 as the alpha-only product of two masks.

**Both factors are dropped, for two independent reasons** `[F]`:

- `PRIM.a` sits on stage 0 in a **mirrored** spelling that `tev_const_alpha()`
  reaches and then throws away at its `konst != GX_CA_A0` narrowing;
- the last stage is `APREV · TEXEL1.a`, the **mirror** of the shape
  `tex1_alpha_active()` tests for.

So the batch draws at `vtx.a · T0.a`, and on these draws `vtx.a` is the
`G_RM_FOG_SHADE_A` **fog coefficient** — a flat dark quad, which is exactly the
reported symptom. This is the same mechanism as the punch-through
`MODULATEALPHA` problem in `kb/traps.md`: the vertex alpha byte on an N64 render
mode is not an opacity.

**The two real levers**, both **widenings**, and widenings in this family have
regressed before (`dc_pvr.c:1080-1098`) — so **both need a screenshot pair**:

1. widen `tev_const_alpha()`'s A1 arm to accept the mirrored spelling;
2. add the mirrored shape to `tex1_alpha_active()`.

⚠️ **`tev_const_alpha_last()` (in the tree, kill switch
`-DDC_PVR_NO_TEVALPHA_LAST`, counter `tevalpha_last batches=`) recognises a final
stage of shape `APREV · konst` and DOES NOT FIX THIS.** Do not re-try it.

⚠️ **See `kb/tev-map-hard-cases.md` §6.6 for the colour-side twin: config #037
loses both COLOUR constants, because class P3 is unimplemented and `pv.oargb` is
hardcoded to 0.** #007 and #037 are the same failure in opposite halves of the
combiner — the folded polynomials in `kb/tev-map-table.md` are right, and the
recognisers in `dc_pvr.c` are narrower than the table.

---
