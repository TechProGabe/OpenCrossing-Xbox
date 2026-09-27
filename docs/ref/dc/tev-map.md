# TEV → PowerVR CLX2 map — split index

All 101 TEV configurations Animal Crossing actually uses, classified into
fixed-function PVR rendering strategies. Written 2026-08-01 (M2 deliverable,
`PLAN.md` §3.3). Source data: `pc/shaders/shader_seed.bin` — 101
driver-independent `ShaderKey`s harvested from a full playthrough of the ARM
port.

| part | sections | contents |
|---|---|---|
| `kb/tev-map-table.md` | §2, §3, §4 | **the reference table** — global facts, the RGB/alpha class definitions, and all 101 configs with folded polynomial, class and PVR strategy |
| `kb/tev-map-implementation.md` | verdict, §7–§10 | hot classes first, the `PVRDrawMode` fold for the DC GX layer, open items, feasibility verdict |
| `kb/tev-map-alpha.md` | §5 | alpha compare → OP/PT/TR list assignment, blend modes, the global `PT_ALPHA_REF` problem, and **§5.6 config #007 losing BOTH alpha factors — the black wedges, open** |
| `kb/tev-map-hard-cases.md` | §6 | two-texture configs, out-of-range coefficients, the A3 alpha approximation, the one `GX_TEV_SUB`, blend modes PVR cannot express, and **§6.6 — 🔴 CLASS P3 IS NOT IMPLEMENTED AT ALL (27 configs, `pv.oargb` hardcoded 0), which is why the name-entry keyboard renders black** |

⚠️ **Two of the 101 are known-wrong on screen today, and they are the same
failure in opposite halves of the combiner: #007 loses both ALPHA factors
(§5.6), #037 loses both COLOUR constants (§6.6).** The folded polynomials in
`kb/tev-map-table.md` are right; the recognisers in `dc/src/dc_pvr.c` are
narrower than the table. Check §6.6 before concluding that a black model is a
missing asset.
| `kb/tev-map-decoding.md` | §1 | the seed file format, the parser, the folding method, and what the seed cannot tell you |

Marking convention, used throughout every part:
`[F]` = decoded fact (from the seed file or read directly out of the source).
`[I]` = inference. `[?]` = not determinable from available data, needs work.
