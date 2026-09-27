# The texture path — audit, 2026-08-02 (session 3)

What `dc/src/dc_pvr_texture.c` actually does to a GameCube texture on its way
to VRAM, what that costs in quality, and which levers are real. Everything
here is read out of the code or out of KOS; the one computed number (§5) is
flagged as computed and is now instrumented.

Three of the patches this audit produced are **applied**: rounding (§3), the
edge-extended pad (§6), and the VRAM probe (§5). The rest are ranked and
deliberately not applied.

## 1. Filtering — already bilinear, and the GC asked for point

`dc/src/dc_pvr.c` passes `PVR_FILTER_BILINEAR` to `pvr_poly_cxt_txr` — the
only `PVR_FILTER_*` in the tree. "The port is point-sampled" is false; that
win was banked before this audit, and it applies to the punch-through list too
because both go through one `compile_header`.

What the GameCube asked for is **`GX_NEAR, GX_NEAR` on every world texture**
(`emu64.c:2275, 2278, 3398, 3401`). `GX_LINEAR` appears in exactly one
non-Famicom place, the font sheets (`JUTResFont.cpp:442`). The original
rendered the whole town point-sampled.

`dc_gx.c:1843` defaults `TEXOBJ_MIN_FILTER`/`MAG_FILTER` to `GX_LINEAR` and
`dc_gx.c:1860` stores the real values from `GXInitTexObjLOD`. **Neither is
mirrored into `g_gx` nor read by `dc_pvr.c`** — the same recorded-never-consumed
family as wrap, TEV constants and the colour mask. `g_gx`
(`dc_gx_internal.h:187-202`) has `tex_obj_wrap_s/t` and no filter field.

**Keep bilinear.** Honouring `GX_NEAR` makes 32×32 N64-era sheets stretched
over acres crawl and alias. That is authenticity, not quality. If an
authenticity toggle is ever wanted it is three files and 32 B of `.bss`:
mirror `o[TEXOBJ_MAG_FILTER]` into `g_gx`, fold it into `header_key()`, select
the filter. Switch would be `-DDC_PVR_HONOUR_GX_FILTER`.

## 2. Format — twiddled 16-bit only. No VQ, no paletted, no mipmaps

Every upload is `TWIDDLED | VQ_DISABLE | POW2_STRIDE` plus one of `RGB565` /
`ARGB1555` / `ARGB4444`, picked by `survey_alpha()` into three classes.
Twiddling is real — `pvr_txr_load_ex(…, PVR_TXRLOAD_16BPP)` twiddles while
copying, which is what filtering and cache behaviour need.

**Paletted is closed, not deferred.** The PVR has **one global 1024-entry
palette RAM with one format active at a time** (4 banks at PAL8BPP, 64 at
PAL4BPP). A 16-bit palette entry gives no more precision than the direct
formats, and only `PVR_PAL_ARGB8888` gives 8-bit — for which KOS's own header
says *"Rendering speed is greatly affected (cut about in half) if you use any
filtering with paletted textures with ARGB8888 entries."* At 12 FPS in town
that is disqualifying. It would buy VRAM, and VRAM is not the constraint (§5).

**VQ is closed too.** `PVR_TXRLOAD_VQ_LOAD` is documented in KOS as *"not
supported yet, if ever"*; a codebook generator would have to run on the SH-4
per texture. 8:1 VRAM win, no quality win.

⚠️ **CLARIFICATION 2026-08-04 — this closes RUNTIME VQ ENCODING, not VQ.**
`kb/levers.md` P2 proposes OFFLINE VQ conversion in `tools/dcasset`, and the
two documents read as contradictory. They are not: an offline-converted texture
needs only a codebook + index copy and the VQ bit in the poly header — it never
goes near `PVR_TXRLOAD_VQ_LOAD`, and offline asset conversion is an explicitly
legal layout lever. ⚠️ **The clause this used to quote is gone** — CLAUDE.md §1
said "codegen is banned; layout is fair game" until 2026-08-06, when the `-O0`
directive was reversed and codegen became the project's biggest single lever
(`kb/state-log.md`). That does not weaken the point here, it strengthens it:
offline VQ never needed the exemption it was claiming. Do not kill P2 by citing
this paragraph. What is closed is generating a codebook on
the console.

Formats handled: `I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, C4, C8, CMPR`.
**Silently dropped to a transparent rectangle:** everything else — notably
`GX_TF_C14X2` (`0xA`), which `emu64.c:314`'s `dol_fmt` table emits for
`G_IM_FMT_CI` at 16-bit size, and `GX_TF_Z24X8` (`JFWDisplay.cpp:416`, a 4×4
clear-Z dummy). Both are believed unreachable — CI at siz=2 is not a legal N64
combination — but the `default:` arm logs nothing. Worth a `DC_LOGE`.

## 3. Colour-depth loss — and the one that was a bug

| GC source | class | PVR dest | loss |
|---|---|---|---|
| `RGB565` | always OPAQUE | RGB565 | none |
| `RGB5A3`, all-opaque | OPAQUE | RGB565 | none (5→5, 5→6 exact) |
| `RGB5A3` with any graded alpha | GRADED | ARGB4444 | **5→4 on every texel of the whole texture** |
| `I4` | GRADED | ARGB4444 | none — natively 4-bit, decoder replicates the nibble |
| `I8` | GRADED unless pure 0/255 | ARGB4444 | **8→4.** Banding on shadows and gradients. Structural: I8's alpha *is* its intensity |
| `IA4` | GRADED | ARGB4444 | none (natively 4/4) |
| `IA8` | GRADED | ARGB4444 | 8→4 both channels |
| `RGBA8` | GRADED | ARGB4444 | 8→4 |
| `C4`/`C8` | from palette alpha | 565/1555/4444 | palette source is RGB5A3/RGB565/IA8 — same as its own row |
| **`CMPR` (S3TC/DXT1)** | **decoded, never dropped** | RGB565 or ARGB1555 | **near-lossless** — `survey_alpha` can never return GRADED for CMPR, so it never lands in 4444 |

CMPR is used (`emu64.c:312, 317`) and is fully decoded, endpoint and sub-block
order copied verbatim from `pc_gx_texture.c:664`. Nothing is dropped.

**✅ APPLIED — `dc_pack()` truncated.** `r>>3`, `g>>2`, `a>>4` carry a
systematic −0.5 LSB bias on every channel of every texel. In ARGB4444, where
all graded-alpha content lands, that is −8/255 ≈ **3 % darker across the whole
image**, plus double the quantisation error — it reads as "the port looks
murky" rather than as any specific artefact. `pc/src/pc_gx_texture.c` writes
RGBA8 and never had to quantise, so there was no reference to copy from: this
was DC-only. Now rounds via `dc_q()`, `(v*vmax + 127)/255` as a multiply and
two shifts (the divide is avoided on purpose — `-O0` on SH-4 turns it into a
libgcc call, once per texel of every upload). RGB565→RGB565 and I4/IA4→4444
verified bit-exact for all inputs. Kill switch `-DDC_PVR_TEX_TRUNCATE`.

## 4. Mipmaps — none, and the GameCube had none either

No chain is built or uploaded; `cxt.txr.mipmap` exists and is never set.
**`emu64.c:2270` passes `GX_FALSE` for the mipmap argument and
`emu64.c:2275/3398` set `min_lod = max_lod = 0.0f`** — the original hardware
also rendered every world texture from a single level. Ground shimmer on the
DC is the shimmer the GC had.

Cost to add: KOS ships **no mipmap builder** (only the `mipmap_en` bit and
`mip_bias`), so the chain builder and the per-level twiddled offsets are ours;
+33 % VRAM per texture; and PVR mipmaps are *believed* to require **square**
textures (medium confidence — verify before investing), which excludes most of
the atlas. **Ranked last:** highest effort, deviates from the original, and it
fixes shimmer the original had.

## 5. VRAM — textures are nowhere near the ceiling

| item | bytes |
|---|---|
| 2 × framebuffer (640×480×2) | 1,228,800 |
| 2 × vertex buffer (`DC_PVR_VERTBUF_BYTES` 768 KB, double-buffered) | 1,572,864 |
| OPBs: 2 lists × 32 words × 300 tiles × (1+3 overflow) × 2 sets | 614,400 |
| tile matrices | ~15,000 |
| **PVR-reserved** | **≈ 3,431,000** |
| **free for textures** | **≈ 4,957,000** |
| soft ceiling `DC_PVR_TEX_VRAM_BYTES` | 4,194,304 |
| **actually resident, town run** | **676,608 (173 textures)** |

≈ 3.5 MB of headroom below the soft ceiling. `evict_lru()` exists against both
the soft ceiling and real `pvr_mem_malloc` failure and on current evidence has
never fired.

⚠️ **These are computed from `pvr_init_params_t` and `pvr_buffers.c`, not
observed.** ✅ **APPLIED:** `dc_pvr_texture_init()` now prints
`pvr_mem_available=` alongside the ceiling, so one boot line settles it.

The real rejection risk is not VRAM but the **131,072 B scratch**: anything
whose POT-padded area exceeds 65,536 texels is rejected and drawn untextured.
Watch `rejects=` in `dc_pvr_texture_report()`.

## 6. The NPOT pad — ✅ APPLIED

The scratch is zero-filled and the decoders fill only `[0,w)×[0,h)`, so
bilinear at the right and bottom edge of **every** NPOT texture interpolated
against the pad: opaque black in RGB565 (a dark seam), transparent in
1555/4444 (a fading seam). `next_pot()` floors at 8, so a 4×4 source
(`emu64.c:686`'s black texture) is three quarters pad and gets smeared. And
`GX_CLAMP` maps to `PVR_UVCLAMP`, which clamps to the **padded** edge at 1.0
rather than to `u_scale` — so clamping a NPOT texture clamped to black.

The pad is now edge-extended: last real column replicated right, last real row
replicated down. That is exactly what CLAMP means and is strictly closer to
correct under REPEAT than black. Zero VRAM, zero `.bss`, one pass per distinct
upload (uploads are content-cached, so it is not a per-frame cost). Kill
switch `-DDC_PVR_NO_TEX_EDGEPAD`.

**Not fixed by that, and still open:** `GX_REPEAT` on a NPOT texture is wrong
by construction — `dc_pvr.c` scales `u *= u_scale` and the hardware repeats at
1.0, so tile *n* starts at `n·u_scale` instead of at `n`. Scaling cannot
express repeat. The fixes are decode-time period replication into the POT area
or a real stride texture (which cannot be twiddled). **Census how many uploads
are actually NPOT before paying for either** — `DC_TEX_LOG` reports `w`/`h`.

## 7. Not applied, ranked

- **D — point-sample the punch-through list** (`-DDC_PVR_PT_NEAREST`). The
  lever for `kb/station-bugs.md` §2 H2 if trim ghosting survives the PT list.
  Bilinear invents alpha between texels, so a 0/255 texel pair yields a run of
  intermediate alphas straddling the PT threshold and the cutout boundary
  frays. Off by default: PT already does the discarding in hardware, and this
  trades a frayed edge for an aliased one on every leaf in town. **Re-test H2
  against the current build before adopting.**
- **E — ordered dither on the 8→4 quantisation.** A 4×4 Bayer offset in the
  GRADED path only, excluding `I4`/`IA4` (natively 4-bit — dithering would
  *add* error). Turns ARGB4444 banding into fine noise at zero cost. **Real
  risk:** flat UI regions drawn from CI4-with-RGB5A3-palette would gain a
  visible 2-value checker, and this game's UI is mostly flat fills. Needs
  `px`/`py` threaded through `dc_pack`. Look at a screenshot after §3 and §6
  before deciding whether banding is still the complaint.
- **C — honour `GXInitTexObjLOD`'s filter.** See §1. Negative for quality.
- **G — mipmaps.** See §4. Last.
