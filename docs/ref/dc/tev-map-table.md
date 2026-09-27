# TEV map — the reference table: 101 configs, folded and classified

Global facts about the set (§2), the RGB/alpha class definitions and their
counts (§3), and the full 101-row classification table with folded polynomial,
class and PVR strategy per config (§4). **This is the lookup table** — load
this file when implementing configs. Part of `kb/tev-map.md`, whose stub maps every § to its file.

**Marking convention used throughout:**
`[F]` = decoded fact (from the seed file or read directly out of the source).
`[I]` = inference. `[?]` = not determinable from available data, needs work.

## 2. Global facts about the set (all `[F]`, decoded)

| property | value |
|---|---|
| configs | **101** |
| TEV stages | 1 stage: **48**, 2 stages: **45**, 3 stages: **8** (never >3) |
| colour channels | `num_chans == 1` in **all 101** |
| lighting | on in **44**, off in **57** |
| channel control | 3 distinct: (lit, mat=REG, amb=REG, alphaMat=VTX) ×44; (unlit, mat=VTX, alphaMat=VTX) ×50; (unlit, mat=REG, alphaMat=REG) ×7 |
| alpha-channel lighting | **off in all 101** (`chan_ctrl_enable[1] == 0`) |
| fog | on in **43**, off in **58** |
| TEV bias | `GX_TB_ZERO` in **every stage of every config** |
| TEV scale | `GX_CS_SCALE_1` in **every stage of every config** |
| clamp | on in **every stage of every config** |
| output register | `GX_TEVPREV` in **every stage of every config** (R0/R1/R2 are read-only inputs — they are only ever written by `GXSetTevColor`) |
| combiner op | `GX_TEV_ADD` everywhere except **one** `GX_TEV_SUB` (config #086 stage 0) |
| TEV comparison ops (`GX_TEV_COMP_*`) | **never used** |
| `GX_CC_KONST` (colour) | **never used** |
| `GX_CA_KONST` (alpha) | used in **2** configs (#003, #089), and in both the selector resolves to the constant **1.0** — so `GXSetTevKColor` is effectively dead in AC |
| alpha compare | only **3** distinct settings — see §5 |

That the bias/scale/clamp/output-register axes are *completely degenerate*
across a full playthrough is the single most important structural result
here: it collapses the TEV state space from "16 stages × 16 inputs × bias ×
scale × 4 output registers" down to "≤3 chained `d + lerp(a,b,c)` terms into
PREV". `[F]`

### Features PVR lacks — are they used? `[F]`, from call-site enumeration

| GX feature | used in the game render path? | evidence |
|---|---|---|
| **Indirect textures** | **No.** Only `src/static/Famicom/*` (the NES emulator) calls `GXSetNumIndStages(>0)` / `GXSetTevIndirect` / `GXSetIndTexOrder`. The NES core is an explicit non-goal. | `grep GXSetTevIndirect\|GXSetNumIndStages src/` — all non-zero uses are under `src/static/Famicom/`; `pc_gx.c` line 1230 already notes "Indirect textures stripped — shader doesn't use them" and the ARM port renders correctly |
| **TEV swap tables** | **No.** Every non-Famicom call is `GXSetTevSwapMode(stage, GX_TEV_SWAP0, GX_TEV_SWAP0)` and SWAP0 is the identity table `(R,G,B,A)`. | `emu64.c:595-601`, `GXInit.c:324-329`; the only non-identity selections are `ks_nes_draw.cpp:1339-1342` |
| **Texture matrices / texgen** | **No.** emu64 sets `GX_TG_MTX2x4` with `GX_IDENTITY` for every texcoord, and in the 2-texgen path generates **both** TEXCOORD0 and TEXCOORD1 from `GX_TG_TEX0`. | `emu64.c:522-529`, `emu64.c:651-653` |
| **EFB feedback (`GXCopyTex`)** | **One call site in the whole game**: `copy_efb_to_texture()` in `src/game/m_play.c:657-667`, called once from `makeBumpTexture()` (line 787) for the screen-wipe / fade transitions. Full-screen RGB565 copy at 2× res. Not per-frame. | `grep GXCopyTex src/` |

This resolves `PLAN.md` §11 open question 3 in the negative: **no indirect
textures, no EFB feedback in any hot path.** One transition effect needs PVR
render-to-texture, or can be replaced with a plain fullscreen fade.

Corollary `[I]`: because texgen is identity and both texcoords derive from
vertex TEX0, **one UV pair per vertex is enough** — which is exactly what the
32-byte `pvr_vertex_t` provides. No vertex-format pressure from texgen.

---

## 3. Per-class counts

RGB classes (what the folded RGB expression is, in one texture `T`):

| class | meaning | PVR mechanism | count |
|---|---|---|---|
| **P0** | no texture term at all | untextured poly, per-vertex ARGB | **35** |
| **P1** | `= T` | `PVR_TXRENV_REPLACE` | **5** |
| **P2** | `= base·T`, base CPU-known | `PVR_TXRENV_MODULATE` / `MODULATEALPHA` | **29** |
| **P3** | `= base·T + offset`, both CPU-known | `MODULATE` + `oargb` (specular/offset colour enabled) | **27** |
| **P4** | references two texmaps | second pass / bake — §6 | **5** |

Alpha classes:

| class | meaning | PVR mechanism | count |
|---|---|---|---|
| **A0** | constant (per-draw / per-vertex) | poly alpha; env `MODULATE` or `DECAL` (tex alpha ignored) | **15** |
| **A1** | `= T.a` | `REPLACE`, or `MODULATEALPHA` with poly alpha 1.0 | **30** |
| **A2** | `= Af·T.a` | `PVR_TXRENV_MODULATEALPHA` | **36** |
| **A3** | `= Af·T.a + off` | *no native form* — approximate (§6.3) | **3** |
| **A4** | two texmaps or non-affine | second pass / bake / drop — §6 | **17** |

Joint distribution (the actual poly types you have to build):

| RGB \ A | A0 | A1 | A2 | A3 | A4 | total |
|---|---|---|---|---|---|---|
| **P0** | 11 | 2 | 16 | – | 6 | 35 |
| **P1** | – | 5 | – | – | – | 5 |
| **P2** | 2 | 15 | 9 | 3 | – | 29 |
| **P3** | 2 | 8 | 11 | – | 6 | 27 |
| **P4** | – | – | – | – | 5 | 5 |
| **total** | 15 | 30 | 36 | 3 | 17 | **101** |

**Headline numbers**

* **81 / 101 (80.2 %) are natively expressible** — RGB in {P0,P1,P2,P3} and
  alpha in {A0,A1,A2}. One PVR poly, one texture, no approximation.
* **3 / 101** need a one-line approximation (A3).
* **17 / 101 (16.8 %) reference a second texture.** 15 of those 17 have
  ground-truth texmaps `[F]`; 2 are inferred `[?]`.
* **0 / 101** use indirect textures, swap tables, texture matrices, TEV
  comparison ops, bias, scale, or non-PREV output registers.

---

## 4. Full classification table

Columns: `st` = TEV stages, `lit` = colour-channel lighting enabled,
`a-test` = alpha compare (see §5), `tex` = texmaps actually sampled
(`✓` texmap is ground truth from `combine_manual`, `?` inferred from stage
index), `RGB`/`A` = folded polynomial (CPU-known symbols in caps, `T0`/`T0a`
= texel rgb/alpha).

| # | st | fog | lit | a-test | tex | RGB (folded) | A (folded) | class | PVR strategy |
|---|----|-----|-----|--------|-----|--------------|------------|-------|--------------|
| 0 | 2 | 1 | 1 | >=ref1 | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 1 | 2 | 1 | 1 | - | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 2 | 1 | 1 | 0 | - | T0 | `C1` | `A0*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 3 | 1 | 0 | 0 | - | - | `C0` | `KA` | P0/A0 | untextured, vtx colour; poly alpha |
| 4 | 2 | 1 | 1 | - | T0,T1 ? | `T1 + C2*RASC + C1*RASC*T0 - C2*RASC*T0` | `A1 + A0*T0a + A0*T1a` | P4/A4 | **needs 2nd texture** - see §6 |
| 5 | 1 | 0 | 0 | - | T0 | `T0` | `T0a` | P1/A1 | REPLACE; = tex.a |
| 6 | 1 | 0 | 0 | - | - | `C1` | `A1` | P0/A0 | untextured, vtx colour; poly alpha |
| 7 | 2 | 1 | 0 | - | T0,T1 ✓ | `C1` | `A1*T0a*T1a` | P0/A4 | **needs 2nd texture** - see §6 |
| 8 | 2 | 1 | 1 | - | T0 | `C2 + C1*RASC - C2*RASC - A0*C1*RASC + A0*C1*T0 + A0*C2*RASC - A0*C2*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 9 | 1 | 0 | 0 | - | T0 | `C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 10 | 1 | 0 | 0 | >=ref0 | T0 | `C2 + C1*T0 - C2*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 11 | 1 | 0 | 0 | - | T0 | `C1` | `T0a` | P0/A1 | untextured, vtx colour; = tex.a |
| 12 | 1 | 0 | 0 | - | T0 | `C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 13 | 1 | 1 | 1 | - | T0 | `RASC*T0` | `A1*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 14 | 1 | 1 | 1 | - | T0 | `C1` | `A0*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 15 | 1 | 1 | 1 | - | - | `C1` | `A1` | P0/A0 | untextured, vtx colour; poly alpha |
| 16 | 2 | 1 | 0 | >=ref1 | T0 | `C1*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 17 | 1 | 1 | 1 | - | - | `C1` | `0` | P0/A0 | untextured, vtx colour; poly alpha |
| 18 | 1 | 1 | 0 | - | - | `C1` | `0` | P0/A0 | untextured, vtx colour; poly alpha |
| 19 | 1 | 1 | 1 | - | T0 | `C2 + C1*RASC` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 20 | 3 | 1 | 1 | - | T0,T1 ✓ | `C1*RASC + T0*T1a` | `A1 + A0*T0a*T1a` | P4/A4 | **needs 2nd texture** - see §6 |
| 21 | 2 | 1 | 1 | - | T0 | `C2*RASC + C1*RASC*T0 - C2*RASC*T0` | `0` | P3/A0 | MODULATE + oargb; poly alpha |
| 22 | 2 | 1 | 1 | - | T0 | `C2*RASC + C1*RASC*T0 - C2*RASC*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |
| 23 | 3 | 1 | 1 | - | T0,T1 ✓ | `T0 + C1*RASC + T0*T1` | `T0a*T1a` | P4/A4 | **needs 2nd texture** - see §6 |
| 24 | 2 | 1 | 1 | - | T0,T1 ✓ | `T0 + C1*RASC + RASC*T0` | `A1*T1a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 25 | 2 | 0 | 0 | - | T0 | `C2*RASC + C1*RASC*T0 - C2*RASC*T0` | `0` | P3/A0 | MODULATE + oargb; poly alpha |
| 26 | 1 | 1 | 1 | - | T0 | `C1*RASC` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 27 | 2 | 1 | 1 | - | T0 | `C2*RASC + C1*RASC*T0 - C2*RASC*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 28 | 2 | 1 | 1 | - | T0 | `C1*RASC*T0` | `A1*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 29 | 1 | 0 | 0 | - | T0 | `RASC*T0` | `A1` | P2/A0 | MODULATE; poly alpha |
| 30 | 2 | 0 | 0 | - | T0,T1 ✓ | `C2 + C1*T0 - C2*T0` | `T1a + T0a*T1a` | P3/A4 | **needs 2nd texture** - see §6 |
| 31 | 2 | 0 | 0 | - | T0 | `C1` | `A0*A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 32 | 2 | 0 | 1 | >=ref1 | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 33 | 2 | 0 | 1 | - | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 34 | 1 | 0 | 0 | - | T0 | `C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 35 | 1 | 0 | 0 | - | T0 | `C2 + C1*T0 - C2*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 36 | 2 | 0 | 0 | >=ref1 | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 37 | 1 | 0 | 0 | - | T0 | `C2 + C1*T0 - C2*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |
| 38 | 2 | 0 | 0 | >=ref1 | T0 ✓ | `C2 + A0*C1 + C2*T0 + A0*C1*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |
| 39 | 1 | 0 | 0 | - | T0 | `C1*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 40 | 1 | 0 | 0 | - | T0 | `C2 + A0*C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 41 | 2 | 0 | 0 | - | T0 ✓ | `C2 + A0*C1 + C2*T0 + A0*C1*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |
| 42 | 2 | 0 | 0 | - | T0,T1 ✓ | `C1` | `A0*T0a*T1a` | P0/A4 | **needs 2nd texture** - see §6 |
| 43 | 1 | 0 | 1 | - | T0 | `C2 + C1*T0 - C2*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 44 | 1 | 0 | 0 | - | T0 | `T0` | `T0a` | P1/A1 | REPLACE; = tex.a |
| 45 | 1 | 0 | 0 | >=ref1 | T0 | `C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 46 | 1 | 0 | 0 | - | T0 | `C1*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 47 | 1 | 0 | 0 | - | - | `C1` | `A1` | P0/A0 | untextured, vtx colour; poly alpha |
| 48 | 1 | 0 | 0 | >=ref1 | T0 | `T0` | `T0a` | P1/A1 | REPLACE; = tex.a |
| 49 | 1 | 0 | 1 | - | T0 | `C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 50 | 3 | 1 | 1 | - | T0,T1 ✓ | `C1*RASC` | `A1*T0a - A0*A1*T0a + A0*A1*T1a` | P0/A4 | **needs 2nd texture** - see §6 |
| 51 | 1 | 1 | 0 | - | T0 | `C2 + C1*T0 - C2*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 52 | 1 | 1 | 1 | - | T0 | `C2 + C1*T0 - C2*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 53 | 1 | 0 | 0 | - | - | `C1` | `0` | P0/A0 | untextured, vtx colour; poly alpha |
| 54 | 1 | 0 | 0 | - | - | `C1` | `A1*RASA` | P0/A0 | untextured, vtx colour; poly alpha |
| 55 | 1 | 0 | 0 | - | T0 | `C2 + C1*T0 - C2*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 56 | 2 | 1 | 0 | - | T0 | `C1*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 57 | 2 | 0 | 0 | >=ref1 | T0,T1 ✓ | `T0` | `T1a` | P1/A1 | REPLACE; = tex.a |
| 58 | 1 | 0 | 0 | >=ref0 | T0 | `C1 - A0*C1 + A0*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |
| 59 | 2 | 0 | 1 | - | - | `C1` | `A1` | P0/A0 | untextured, vtx colour; poly alpha |
| 60 | 2 | 0 | 1 | - | T0 | `C1*RASC*T0` | `A1*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 61 | 1 | 0 | 0 | >=ref0 | T0 | `C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 62 | 1 | 0 | 0 | >=ref0 | T0 | `C1` | `T0a` | P0/A1 | untextured, vtx colour; = tex.a |
| 63 | 1 | 0 | 0 | >=ref0 | T0 | `C2 + C1*T0 - C2*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |
| 64 | 1 | 0 | 0 | >=ref0 | - | `C1` | `A1` | P0/A0 | untextured, vtx colour; poly alpha |
| 65 | 2 | 1 | 1 | - | T0 | `C1*RASC*T0` | `A2*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 66 | 2 | 1 | 1 | - | T0 | `C1*RASC*T0` | `A1*A2*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 67 | 3 | 1 | 1 | - | T0,T1 ✓ | `C1` | `A1*T0a - A0*A1*T0a + A0*A1*T1a` | P0/A4 | **needs 2nd texture** - see §6 |
| 68 | 3 | 1 | 0 | - | T0,T1 ✓ | `RASC*T0 + C1*C2*RASC - C2*RASC*T0` | `A1*T0a - A0*A1*T0a + A0*A1*T1a` | P3/A4 | **needs 2nd texture** - see §6 |
| 69 | 3 | 1 | 0 | - | T0,T1 ✓ | `C2 + C1*T0*T1a - C2*T0*T1a` | `A1*T0a*T1a` | P4/A4 | **needs 2nd texture** - see §6 |
| 70 | 1 | 1 | 1 | - | - | `C2` | `A1` | P0/A0 | untextured, vtx colour; poly alpha |
| 71 | 2 | 0 | 0 | - | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 72 | 2 | 0 | 0 | - | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 73 | 1 | 0 | 0 | >=ref0 | T0 | `0` | `A2*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 74 | 1 | 0 | 0 | - | T0 | `C2 + C1*T0 - C2*T0` | `A0*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 75 | 3 | 0 | 1 | - | T0,T1 ✓ | `C1*RASC` | `A1*T0a - A0*A1*T0a + A0*A1*T1a` | P0/A4 | **needs 2nd texture** - see §6 |
| 76 | 2 | 1 | 1 | >=ref1 | T0 | `C1*RASC*T0` | `A1 + A0*T0a` | P2/A3 | MODULATE; approx (drop offset) |
| 77 | 2 | 1 | 1 | - | T0,T1 ✓ | `C2 + A0*T1 + C1*RASC` | `T0a*T1a` | P3/A4 | **needs 2nd texture** - see §6 |
| 78 | 2 | 1 | 1 | - | T0,T1 ✓ | `T1 + C1*RASC + RASC*T0` | `A0*T0a*T1a` | P4/A4 | **needs 2nd texture** - see §6 |
| 79 | 2 | 1 | 1 | - | T0,T1 ✓ | `C2 + A0*T0 + C1*RASC` | `T1a + T0a*T1a` | P3/A4 | **needs 2nd texture** - see §6 |
| 80 | 2 | 1 | 1 | - | T0,T1 ✓ | `T0 + C1*RASC + RASC*T0` | `T0a*T1a` | P3/A4 | **needs 2nd texture** - see §6 |
| 81 | 2 | 1 | 1 | >=ref1 | T0 | `C1*RASC*T0` | `A2*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 82 | 2 | 1 | 1 | - | T0 | `C1*RASC*T0` | `A2*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 83 | 2 | 0 | 0 | - | T0 | `C1*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 84 | 2 | 0 | 0 | >=ref1 | T0 | `C1*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 85 | 1 | 1 | 1 | - | T0 | `C1` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 86 | 3 | 1 | 1 | - | T0 ✓ | `1/2*C1*RASC + C2*RASC - C1*RASC*T0` | `A1*T0a` | P3/A2 | MODULATE + oargb; MODULATEALPHA |
| 87 | 2 | 0 | 0 | - | T0 | `C1*RASC*T0` | `A1` | P2/A0 | MODULATE; poly alpha |
| 88 | 2 | 0 | 0 | >=ref1 | T0 | `C1*RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 89 | 2 | 0 | 0 | - | T0 | `RASC*T0` | `A0*KA + A1*T0a` | P2/A3 | MODULATE; approx (drop offset) |
| 90 | 2 | 0 | 0 | - | T0,T1 ✓ | `A1*C1 + A2*T0` | `A0*T0a*T1a` | P3/A4 | **needs 2nd texture** - see §6 |
| 91 | 1 | 0 | 0 | - | T0 | `C1*T0` | `A0*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 92 | 1 | 1 | 1 | >=ref1 | T0 | `T0` | `T0a` | P1/A1 | REPLACE; = tex.a |
| 93 | 1 | 0 | 0 | - | T0 | `C1` | `A0*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 94 | 2 | 0 | 1 | - | T0 | `C1*RASC*T0` | `A1*T0a` | P2/A2 | MODULATE; MODULATEALPHA |
| 95 | 1 | 1 | 1 | >=ref1 | T0 | `C1*RASC` | `A1*T0a` | P0/A2 | untextured, vtx colour; MODULATEALPHA |
| 96 | 2 | 1 | 1 | >=ref1 | T0 | `C2*RASC + C1*RASC*T0 - C2*RASC*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |
| 97 | 2 | 0 | 1 | - | T0 | `C1*RASC*T0` | `A1 + A0*T0a` | P2/A3 | MODULATE; approx (drop offset) |
| 98 | 1 | 0 | 0 | - | T0 | `RASC*T0` | `T0a` | P2/A1 | MODULATE; = tex.a |
| 99 | 2 | 0 | 0 | - | T0,T1 ? | `C1` | `T0a*T1a` | P0/A4 | **needs 2nd texture** - see §6 |
| 100 | 1 | 0 | 0 | >=ref0 | T0 | `C1 - A0*C1 + A0*T0` | `T0a` | P3/A1 | MODULATE + oargb; = tex.a |

---
