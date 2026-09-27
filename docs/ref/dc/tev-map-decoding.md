# TEV map — where the data comes from and how it was decoded

`shader_seed.bin`'s format, the parser used, the polynomial-folding method
that turns a `ShaderKey` into a PVR strategy, and the one gap the seed cannot
close (§1). Read to reproduce or re-harvest the data — not needed to use the
table. Part of `kb/tev-map.md`, whose stub maps every § to its file.

**Marking convention used throughout:**
`[F]` = decoded fact (from the seed file or read directly out of the source).
`[I]` = inference. `[?]` = not determinable from available data, needs work.

## 1. Where the data comes from, and how it was decoded

### 1.1 File format `[F]`

`pc/src/pc_gx_tev.c` defines the container (`SHADER_DISK_CACHE` block,
lines 588-702) and the key (lines 112-132):

```
header : u32 magic 'ACSC' (0x41435343) | u32 version | u32 driver_hash
entry* : ShaderKey (72 B) | u32 binary_format | u32 length | <length> bytes
```

The seed ships with the blobs stripped (`length == 0`), so every entry is
exactly 80 bytes. Decode check:

```
magic    = 0x41435343 'ACSC'   ✓
version  = 1                   ✓ (matches SDC_VERSION)
filesize = 8092 = 12 + 101×80  ✓  bytes consumed == filesize, 101 entries
all entries: length == 0, _pad == 0   ✓
```

**The file decodes cleanly and the entry count is exactly 101.** No fallback
to static enumeration was needed.

### 1.2 The parser (as used)

```python
#!/usr/bin/env python3
"""Decode pc/shaders/shader_seed.bin -> ShaderKey records.
ShaderKey, verbatim from pc/src/pc_gx_tev.c (all members u8, no padding):
  0  num_stages      1  num_chans
  2  light_enable    3  light_mat_src   4  light_amb_src   (chan_ctrl[0])
  5  alpha_light_en  6  alpha_mat_src                      (chan_ctrl[1])
  7  fog_enabled
  8  alpha_comp0     9  alpha_aop      10  alpha_comp1     11 _pad
  12..71 : 3 x stage[20]:
     +0..3 cin[4] (GXTevColorArg 0-15)   +4..7 ain[4] (GXTevAlphaArg 0-7)
     +8 color_op   +9 alpha_op
     +10 color_bias +11 color_scale +12 alpha_bias +13 alpha_scale
     +14 color_clamp +15 alpha_clamp +16 color_out +17 alpha_out
     +18 k_color_sel +19 k_alpha_sel
"""
import struct
SEED  = "pc/shaders/shader_seed.bin"
MAGIC = 0x41435343

def parse(path=SEED):
    data = open(path, "rb").read()
    magic, ver, drvhash = struct.unpack_from("<III", data, 0)
    assert magic == MAGIC, "bad magic %08x" % magic
    off, keys = 12, []
    while off + 80 <= len(data):
        raw     = data[off:off + 72]
        fmt, ln = struct.unpack_from("<II", data, off + 72)
        off    += 80 + ln                      # ln == 0 in the seed
        k = {}
        (k["num_stages"], k["num_chans"], k["light_en"], k["light_mat_src"],
         k["light_amb_src"], k["alpha_light_en"], k["alpha_mat_src"],
         k["fog"], k["acomp0"], k["aop"], k["acomp1"], k["pad"]) = raw[:12]
        k["s"] = [dict(cin=list(b[0:4]), ain=list(b[4:8]),
                       cop=b[8],    aop=b[9],
                       cbias=b[10], cscale=b[11], abias=b[12], ascale=b[13],
                       cclamp=b[14], aclamp=b[15], cout=b[16], aout=b[17],
                       kc=b[18],    ka=b[19])
                  for b in (raw[12+i*20 : 32+i*20] for i in range(3))]
        keys.append(k)
    return ver, drvhash, keys          # len(keys) == 101
```

### 1.3 How each key was turned into a strategy `[F]` method, `[I]` conclusions

The TEV stage function is `out = d ± lerp(a, b, c)` (± bias, × scale), i.e.
**pure +, −, × over its inputs**. So a whole config is a *polynomial* in two
disjoint symbol sets:

| class | symbols | why it matters on DC |
|---|---|---|
| CPU-known | `C0 C1 C2 A0 A1 A2` (TEV registers, per-draw), `P0 PA0` (initial PREV), `RASC RASA` (lit vertex colour), `KC KA` (konst), rationals | **On DC, lighting runs on SH-4, so `RASC` is computed per vertex on the CPU anyway. Every one of these is a number the CPU already holds at submit time.** |
| per-pixel | `T<m>` / `T<m>a` — rgb / alpha of the texture bound to texmap `m` | only these need the texture unit |

Each config was evaluated with an exact rational polynomial algebra (dict of
sorted-monomial → `Fraction`), then the folded result was matched against
what PVR fixed function can produce:

```python
# PVR pixel = env(base_colour, texel) + offset_colour   (offset alpha ignored)
#   PVR_TXRENV_REPLACE       px = ARGB(tex)
#   PVR_TXRENV_MODULATE      px = A(tex) + RGB(col)*RGB(tex)
#   PVR_TXRENV_DECAL         px = A(col) + RGB(tex)*A(tex) + RGB(col)*(1-A(tex))
#   PVR_TXRENV_MODULATEALPHA px = ARGB(col)*ARGB(tex)
def split_affine(p, T):          # p == Q + C*T with Q, C CPU-known?
    Q, C = Poly(), Poly()
    for mono, coef in p.items():
        occ = [s for s in mono if s in TEXSYM]
        if not occ:        Q[mono] += coef
        elif occ == [T]:   C[mono_without_one(T)] += coef
        else:              return None          # ≥2 texture symbols -> not affine
    return Q, C
```

`base_colour` (`pvr_vertex_t.argb`) and `offset_colour` (`.oargb`) are both
**per-vertex**, so `C` and `Q` may depend on `RASC` — they are emitted by the
same SH-4 transform/lighting loop that already has to run.

### 1.4 The one thing the seed cannot tell you `[?]` → resolved `[F]` for 20 of 101

`ShaderKey` records the combiner *maths* but **not** `tex_map` / `tex_coord`
(they live in `PCGXTevStage` but `build_key()` drops them), and **not** the
alpha-compare reference *values* (`alpha_ref0/1` are uniforms). So the seed
alone cannot say whether stage 0 and stage 1 sample the same texture.

That gap was closed from the source instead: 20 of the 101 configs match
`emu64::combine_manual()` cases (`src/static/libforest/emu64/emu64.c`
1503-1954) exactly on `(num_stages, cin[], ain[])`, and those cases carry
literal `GXSetTevOrder(..., GX_TEXMAP0/1/NULL, ...)` calls. **17 of the 19
configs that reference a second texture slot are in that matched set, so
their texmaps are ground truth, not assumption.** In the table below a
texmap column marked `✓` is ground truth; `?` means the stage index was used
as a proxy (only 2 configs, #004 and #099).

**Action item:** bump `SDC_VERSION` and add `tex_map`, `tex_coord`,
`alpha_ref0`, `alpha_ref1`, `blend_mode/src/dst` and `z_*` to `ShaderKey`,
then re-harvest on the ARM port. That single change turns every `[?]` in this
document into `[F]` and is the cheapest measurement left on the board.

---
