# Platform API — GX: state machine, vertices, TEV, textures

The four GX behaviours the DC backend must reproduce (no `GXEnd`, deferred vertex
commit, batch merging, state dedup + cull) and every `GX*` symbol: submission,
transform, TEV, raster, lighting, texgen, copies, display lists, FIFO, texture
objects and TLUTs. Read before touching `dc/src/dc_gx.c` or the PVR backend.
Split out of `kb/design-platform-api.md` (§3.6, §5). Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### 3.6 The GX vertex/state machine

The GX layer is not a thin wrapper; it is a state machine with four
behaviours the DC backend must reproduce:

1. **`GXEnd` is never called.** emu64 omits it. Batches terminate when the
   declared vertex count from `GXBegin(prim, fmt, nverts)` is reached
   (`pc_gx_flush_if_begin_complete`). Strip/fan batches complete on their
   **source** count while the emitted count is 3×(n−2).
2. **Deferred vertex commit.** A `GXPosition*` call commits the *previous*
   vertex and starts a new one. The reset clears normal, color1 and texcoords
   but **carries color0 forward**. Break this and vertex colours break.
3. **Batch merging.** `GXBegin` concatenates into an already-open, already-
   complete batch when `dirty == 0`, the primitive is QUADS or TRIANGLES, and
   the vtxfmt matches. Measured: 491–600 draws/frame → **114.6**.
4. **State dedup + whole-batch frustum cull.** Re-setting identical state
   returns early (no flush, no dirty bit), because the decomp re-sets state
   constantly. The cull tests an object-space AABB against the exact P·MV and
   rejects **60–80 %** of submitted batches (~286/frame). Both are pure C and
   both are exactly what a tile-based deferred GPU wants — transplant them
   unchanged.

Where the batches break today (why merging is rarer than it could be):
modelview loads **41 %**, texture changes **30 %**. `GXLoadPosMtxImm` is the
single biggest batching obstacle; a CPU pre-transform pass would help DC even
more than it helps the base port.

Indexed attributes: `GXSetArray(attr, ptr, size, stride)` stores a raw
GameCube-era 32-bit pointer, and `GXPosition1x16`/`GXNormal1x16`/
`GXTexCoord1x16`/`GXColor1x16` fetch through it assuming `f32[3]` /
`f32[2]` sources. Those pointers must survive the seg2k0 range heuristic
(PLAN §11.6).

TEV: max **3** stages (`PC_GX_MAX_TEV_STAGES`), **101** unique configurations
harvested from a full playthrough. That is a complete, finite spec — it is
what `tev-map.md` (M2) must classify.

TLUT: per-slot `is_be` flag. ROM-sourced palettes are big-endian; emu64/EFB
ones are native little-endian. `pc_gx_tlut_set_native_le(idx)` is the setter.
This distinction carries to DC unchanged.

### GX — init / sync

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXInit` | `void* GXInit(void* base, u32 size)` | pc_gx.c | rewrite-for-KOS | PC returns `base` unchanged and ignores the FIFO. Called from JUTGraphFifo.cpp with a JKRHeap-allocated 0x10001-byte buffer (JW_Init sets FifoBufSize). On DC that allocation can be reclaimed. |
| `GXSetMisc` | `void GXSetMisc(u32 token, u32 val)` | pc_gx.c | rewrite-for-KOS |  |
| `GXFlush` | `void GXFlush(void)` | pc_gx.c | rewrite-for-KOS | Maps to nothing on a deferred tile renderer; leave as a no-op. |
| `GXResetWriteGatherPipe` | `void GXResetWriteGatherPipe(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXAbortFrame` | `void GXAbortFrame(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetDrawSync` | `void GXSetDrawSync(u16 token)` | pc_gx.c | rewrite-for-KOS |  |
| `GXReadDrawSync` | `u16 GXReadDrawSync(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetDrawDone` | `void GXSetDrawDone(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXWaitDrawDone` | `void GXWaitDrawDone(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXDrawDone` | `void GXDrawDone(void)` | pc_gx.c | rewrite-for-KOS | No-op on PC; on DC this is a real pvr_wait_ready()/scene sync point. |
| `GXPixModeSync` | `void GXPixModeSync(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexModeSync` | `void GXTexModeSync(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetDrawSyncCallback` | `void* GXSetDrawSyncCallback(void* cb)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetDrawDoneCallback` | `void* GXSetDrawDoneCallback(void* cb)` | pc_gx.c | rewrite-for-KOS |  |

### GX — vertex submission (immediate mode)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXBegin` | `void GXBegin(u32 primitive, u32 vtxfmt, u16 nverts)` | pc_gx.c | rewrite-for-KOS | ★ Batching heart. Merges consecutive complete batches when dirty==0 and prim is QUADS/TRIANGLES; converts TRIANGLESTRIP/FAN to independent triangles up front. `nverts` is the DECLARED count — emu64 never calls GXEnd, so completion is detected by counting. |
| `GXEnd` | `void GXEnd(void)` | pc_gx.c | rewrite-for-KOS | ★ emu64 OMITS GXEnd. Never rely on it; pc_gx_flush_if_begin_complete() is what actually terminates a batch. |
| `GXPosition3f32` | `void GXPosition3f32(f32 x, f32 y, f32 z)` | pc_gx.c | rewrite-for-KOS | ★ Deferred-commit state machine: a position call COMMITS the previous vertex, then resets normal/color1/texcoords but CARRIES COLOR0 FORWARD. Any DC rewrite must preserve the carry-forward or vertex colors break. |
| `GXPosition3u16` | `void GXPosition3u16(u16 x, u16 y, u16 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition3s16` | `void GXPosition3s16(s16 x, s16 y, s16 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition3u8` | `void GXPosition3u8(u8 x, u8 y, u8 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition3s8` | `void GXPosition3s8(s8 x, s8 y, s8 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition2f32` | `void GXPosition2f32(f32 x, f32 y)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition2u16` | `void GXPosition2u16(u16 x, u16 y)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition2s16` | `void GXPosition2s16(s16 x, s16 y)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition2u8` | `void GXPosition2u8(u8 x, u8 y)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition2s8` | `void GXPosition2s8(s8 x, s8 y)` | pc_gx.c | rewrite-for-KOS |  |
| `GXPosition1x16` | `void GXPosition1x16(u16 index)` | pc_gx.c | rewrite-for-KOS | Indexed attribute fetch from GXSetArray base + stride. Assumes f32[3] source. Indexed positions/normals are how the game submits most static geometry. |
| `GXPosition1x8` | `void GXPosition1x8(u8 index)` | pc_gx.c | rewrite-for-KOS |  |
| `GXNormal3f32` | `void GXNormal3f32(f32 x, f32 y, f32 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXNormal3s16` | `void GXNormal3s16(s16 x, s16 y, s16 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXNormal3s8` | `void GXNormal3s8(s8 x, s8 y, s8 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXNormal1x16` | `void GXNormal1x16(u16 index)` | pc_gx.c | rewrite-for-KOS |  |
| `GXNormal1x8` | `void GXNormal1x8(u8 index)` | pc_gx.c | rewrite-for-KOS |  |
| `GXColor4u8` | `void GXColor4u8(u8 r, u8 g, u8 b, u8 a)` | pc_gx.c | rewrite-for-KOS |  |
| `GXColor3u8` | `void GXColor3u8(u8 r, u8 g, u8 b)` | pc_gx.c | rewrite-for-KOS |  |
| `GXColor1u32` | `void GXColor1u32(u32 clr)` | pc_gx.c | rewrite-for-KOS | Packed RGBA8; several variants funnel to GXColor4u8. |
| `GXColor1u16` | `void GXColor1u16(u16 clr)` | pc_gx.c | rewrite-for-KOS |  |
| `GXColor1x16` | `void GXColor1x16(u16 index)` | pc_gx.c | rewrite-for-KOS |  |
| `GXColor1x8` | `void GXColor1x8(u8 index)` | pc_gx.c | rewrite-for-KOS |  |
| `GXColor4f32` | `void GXColor4f32(float r, float g, float b, float a)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord2f32` | `void GXTexCoord2f32(f32 s, f32 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord2u16` | `void GXTexCoord2u16(u16 s, u16 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord2s16` | `void GXTexCoord2s16(s16 s, s16 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord2u8` | `void GXTexCoord2u8(u8 s, u8 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord2s8` | `void GXTexCoord2s8(s8 s, s8 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord1f32` | `void GXTexCoord1f32(f32 s, f32 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord1u16` | `void GXTexCoord1u16(u16 s, u16 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord1s16` | `void GXTexCoord1s16(s16 s, s16 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord1u8` | `void GXTexCoord1u8(u8 s, u8 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord1s8` | `void GXTexCoord1s8(s8 s, s8 t)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord1x16` | `void GXTexCoord1x16(u16 index)` | pc_gx.c | rewrite-for-KOS |  |
| `GXTexCoord1x8` | `void GXTexCoord1x8(u8 index)` | pc_gx.c | rewrite-for-KOS |  |

### GX — vertex descriptor / indexed arrays

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXSetVtxDesc` | `void GXSetVtxDesc(u32 attr, u32 type)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetVtxDescv` | `void GXSetVtxDescv(const void* list)` | pc_gx.c | rewrite-for-KOS |  |
| `GXClearVtxDesc` | `void GXClearVtxDesc(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetVtxAttrFmt` | `void GXSetVtxAttrFmt(u32 vtxfmt, u32 attr, u32 cnt, u32 type, u8 frac)` | pc_gx.c | rewrite-for-KOS | Recorded but the PC path always builds the same 48-byte PCGXVertex. On DC the record shrinks to a PVR-native 32-byte vertex. |
| `GXSetArray` | `void GXSetArray(u32 attr, const void* data, u32 size, u8 stride)` | pc_gx.c | rewrite-for-KOS | ★ Stores a raw pointer + stride per attribute; the pointer is a GameCube-era 32-bit address the game computed. On DC this must survive the seg2k0 pointer heuristic (PLAN §11.6). |
| `GXInvalidateVtxCache` | `void GXInvalidateVtxCache(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXGetVtxAttrFmt` | `void GXGetVtxAttrFmt(u32 idx, u32 attr, u32* compCnt, u32* compType, u8* shift)` | pc_gx.c | rewrite-for-KOS |  |

### GX — transform / viewport / scissor

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXSetProjection` | `void GXSetProjection(const void* mtx, u32 type)` | pc_gx.c | rewrite-for-KOS | GC projection matrices are 4x4 (GX_PERSPECTIVE) or 3x4 (GX_ORTHOGRAPHIC) with a different memory layout — check the `type` argument. |
| `GXLoadPosMtxImm` | `void GXLoadPosMtxImm(const void* mtx, u32 id)` | pc_gx.c | rewrite-for-KOS | 3x4 row-major modelview into slot id (0..9 used). 41% of all batch breaks are modelview loads — the single biggest batching obstacle. |
| `GXLoadNrmMtxImm` | `void GXLoadNrmMtxImm(const void* mtx, u32 id)` | pc_gx.c | rewrite-for-KOS |  |
| `GXLoadTexMtxImm` | `void GXLoadTexMtxImm(const void* mtx, u32 id, u32 type)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetCurrentMtx` | `void GXSetCurrentMtx(u32 id)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetViewport` | `void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz)` | pc_gx.c | rewrite-for-KOS | Shadowed: re-setting an identical viewport skips both flush and the GL call. Keep that dedup on DC. |
| `GXSetViewportJitter` | `void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz, u32 field)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetScissor` | `void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetScissorBoxOffset` | `void GXSetScissorBoxOffset(s32 x, s32 y)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetClipMode` | `void GXSetClipMode(u32 mode)` | pc_gx.c | rewrite-for-KOS |  |
| `GXGetProjectionv` | `void GXGetProjectionv(f32* p)` | pc_gx.c | rewrite-for-KOS |  |

### GX — TEV / indirect

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXSetNumTevStages` | `void GXSetNumTevStages(u8 nStages)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevOp` | `void GXSetTevOp(u32 stage, u32 mode)` | pc_gx.c | rewrite-for-KOS | One of 101 harvested TEV configurations (kb/tev-map-table.md). Max 3 stages (PC_GX_MAX_TEV_STAGES). This is the surface tev-map.md must classify. |
| `GXSetTevColorIn` | `void GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevAlphaIn` | `void GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevColorOp` | `void GXSetTevColorOp(u32 stage, u32 op, u32 bias, u32 scale, GXBool clamp, u32 out_reg)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevAlphaOp` | `void GXSetTevAlphaOp(u32 stage, u32 op, u32 bias, u32 scale, GXBool clamp, u32 out_reg)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevOrder` | `void GXSetTevOrder(u32 stage, u32 coord, u32 map, u32 color)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevColor` | `void GXSetTevColor(u32 id, u32 color_packed)` | pc_gx.c | rewrite-for-KOS | TEV register colors: PVR has no TEV registers; these collapse into vertex colors or a second pass. |
| `GXSetTevColorS10` | `void GXSetTevColorS10(u32 id, s16 r, s16 g, s16 b, s16 a)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevKColor` | `void GXSetTevKColor(u32 id, u32 color_packed)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevKColorSel` | `void GXSetTevKColorSel(u32 stage, u32 sel)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevKAlphaSel` | `void GXSetTevKAlphaSel(u32 stage, u32 sel)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevSwapMode` | `void GXSetTevSwapMode(u32 stage, u32 ras_sel, u32 tex_sel)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevSwapModeTable` | `void GXSetTevSwapModeTable(u32 table, u32 red, u32 green, u32 blue, u32 alpha)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevDirect` | `void GXSetTevDirect(u32 stage)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetNumIndStages` | `void GXSetNumIndStages(u8 n)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetIndTexMtx` | `void GXSetIndTexMtx(u32 mtx_sel, const void* offset, s8 scale)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetIndTexOrder` | `void GXSetIndTexOrder(u32 ind_stage, u32 tex_coord, u32 tex_map)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTevIndirect` | `void GXSetTevIndirect(u32 stage, u32 ind_stage, u32 fmt, u32 bias_sel, u32 mtx_sel, u32 wrap_s, u32 wrap_t, GXBool add_prev, GXBool ind_lod, u32 alpha_sel)` | pc_gx.c | rewrite-for-KOS | Indirect-texture stages. PVR has no equivalent — the 101-config census must say whether AC ever uses them. |
| `GXSetTevIndWarp` | `void GXSetTevIndWarp(u32 stage, u32 ind_stage, GXBool signed_ofs, GXBool replace, u32 mtx_sel)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetIndTexCoordScale` | `void GXSetIndTexCoordScale(u32 ind_stage, u32 scale_s, u32 scale_t)` | pc_gx.c | rewrite-for-KOS |  |
| `__GXSetIndirectMask` | `void __GXSetIndirectMask(u32 mask)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetZTexture` | `void GXSetZTexture(u32 op, u32 fmt, u32 bias)` | pc_gx.c | rewrite-for-KOS |  |

### GX — raster / blend / depth / fog

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXSetAlphaCompare` | `void GXSetAlphaCompare(u32 comp0, u8 ref0, u32 op, u32 comp1, u8 ref1)` | pc_gx.c | rewrite-for-KOS | ★ Drives the OPAQUE / PUNCH-THROUGH / TRANSLUCENT list classification on PVR — the most consequential single mapping decision in the renderer. |
| `GXSetBlendMode` | `void GXSetBlendMode(u32 type, u32 src, u32 dst, u32 logic_op)` | pc_gx.c | rewrite-for-KOS | Maps to PVR per-poly blend modes; PVR sorts translucents per-tile so draw order matters much less. |
| `GXSetZMode` | `void GXSetZMode(GXBool compare_enable, u32 func, GXBool update_enable)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetColorUpdate` | `void GXSetColorUpdate(GXBool enable)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetAlphaUpdate` | `void GXSetAlphaUpdate(GXBool enable)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetZCompLoc` | `void GXSetZCompLoc(GXBool before_tex)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetDither` | `void GXSetDither(GXBool dither)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetDstAlpha` | `void GXSetDstAlpha(GXBool enable, u8 alpha)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetFieldMask` | `void GXSetFieldMask(GXBool odd, GXBool even)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetFieldMode` | `void GXSetFieldMode(GXBool field_mode, GXBool half_aspect)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetPixelFmt` | `void GXSetPixelFmt(u32 pix_fmt, u32 z_fmt)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetCullMode` | `void GXSetCullMode(u32 mode)` | pc_gx.c | rewrite-for-KOS | GX cull sense is opposite to GL's in places; verify against the PC implementation before trusting it. |
| `GXSetCoPlanar` | `void GXSetCoPlanar(GXBool enable)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetFog` | `void GXSetFog(u32 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color)` | pc_gx.c | rewrite-for-KOS | PVR table fog is a near-native fit for GX fog. |
| `GXInitFogAdjTable` | `void GXInitFogAdjTable(void* table, u16 width, f32 projmtx[4][4])` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetFogRangeAdj` | `void GXSetFogRangeAdj(GXBool enable, u16 center, void* table)` | pc_gx.c | rewrite-for-KOS |  |

### GX — lighting & color channels

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXSetNumChans` | `void GXSetNumChans(u8 nChans)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetChanCtrl` | `void GXSetChanCtrl(u32 chan, GXBool enable, u32 amb_src, u32 mat_src, u32 light_mask, u32 diff_fn, u32 attn_fn)` | pc_gx.c | rewrite-for-KOS | 8 lights, ambient/material source select, diffuse fn + attenuation fn. Runs in the vertex shader on PC; on DC this is SH-4 work (FIPR). |
| `GXSetChanAmbColor` | `void GXSetChanAmbColor(u32 chan, u32 color_packed)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetChanMatColor` | `void GXSetChanMatColor(u32 chan, u32 color_packed)` | pc_gx.c | rewrite-for-KOS |  |
| `GXInitLightSpot` | `void GXInitLightSpot(void* lt, f32 cutoff, u32 spot_func)` | pc_gx.c | rewrite-for-KOS |  |
| `GXInitLightDistAttn` | `void GXInitLightDistAttn(void* lt, f32 ref_dist, f32 ref_bright, u32 dist_func)` | pc_gx.c | rewrite-for-KOS |  |
| `GXInitLightPos` | `void GXInitLightPos(void* lt, f32 x, f32 y, f32 z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXInitLightDir` | `void GXInitLightDir(void* lt, f32 nx, f32 ny, f32 nz)` | pc_gx.c | rewrite-for-KOS |  |
| `GXInitLightColor` | `void GXInitLightColor(void* lt, u32 color)` | pc_gx.c | rewrite-for-KOS |  |
| `GXInitLightAttn` | `void GXInitLightAttn(void* lt, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2)` | pc_gx.c | rewrite-for-KOS | Angular (a0,a1,a2) + distance (k0,k1,k2) attenuation. Full GC model; clamp to measured usage on DC. |
| `GXInitLightAttnA` | `void GXInitLightAttnA(void* lt, f32 a0, f32 a1, f32 a2)` | pc_gx.c | rewrite-for-KOS |  |
| `GXInitLightAttnK` | `void GXInitLightAttnK(void* lt, f32 k0, f32 k1, f32 k2)` | pc_gx.c | rewrite-for-KOS |  |
| `GXLoadLightObjImm` | `void GXLoadLightObjImm(void* lt, u32 light)` | pc_gx.c | rewrite-for-KOS |  |
| `GXGetLightPos` | `void GXGetLightPos(void* lt, f32* x, f32* y, f32* z)` | pc_gx.c | rewrite-for-KOS |  |
| `GXGetLightColor` | `void GXGetLightColor(void* lt, void* color)` | pc_gx.c | rewrite-for-KOS |  |

### GX — texgen

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXSetNumTexGens` | `void GXSetNumTexGens(u8 n)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTexCoordGen2` | `void GXSetTexCoordGen2(u32 dst, u32 func, u32 src, u32 mtx, GXBool normalize, u32 postmtx)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetLineWidth` | `void GXSetLineWidth(u8 width, u32 texOffsets)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetPointSize` | `void GXSetPointSize(u8 size, u32 texOffsets)` | pc_gx.c | rewrite-for-KOS |  |
| `GXEnableTexOffsets` | `void GXEnableTexOffsets(u32 coord, GXBool line, GXBool point)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTexCoordScaleManually` | `void GXSetTexCoordScaleManually(u32 coord, GXBool enable, u16 ss, u16 ts)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTexCoordBias` | `void GXSetTexCoordBias(u32 coord, u8 s, u8 t)` | pc_gx.c | rewrite-for-KOS |  |

### GX — display copy

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXSetCopyClear` | `void GXSetCopyClear(GXColor clear_clr, u32 clear_z)` | pc_gx.c | rewrite-for-KOS | Clear color/Z for the next copy; on DC feeds pvr_set_bg_color / the OP list background plane. |
| `GXCopyDisp` | `void GXCopyDisp(void* dest, GXBool clear)` | pc_gx.c | rewrite-for-KOS | PC only flushes geometry; the swap happens in VIWaitForRetrace. On DC this is where pvr_scene_finish() belongs (or stays a flush, with the swap in VI). |
| `GXSetDispCopyGamma` | `void GXSetDispCopyGamma(u32 gamma)` | pc_gx.c | port-as-is |  |
| `GXSetDispCopySrc` | `void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht)` | pc_gx.c | port-as-is |  |
| `GXSetDispCopyDst` | `void GXSetDispCopyDst(u16 wd, u16 ht)` | pc_gx.c | port-as-is |  |
| `GXGetYScaleFactor` | `f32 GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight)` | pc_gx.c | port-as-is |  |
| `GXSetDispCopyYScale` | `u32 GXSetDispCopyYScale(f32 vscale)` | pc_gx.c | port-as-is |  |
| `GXGetNumXfbLines` | `u16 GXGetNumXfbLines(u16 efbHeight, f32 yScale)` | pc_gx.c | port-as-is |  |
| `GXSetCopyFilter` | `void GXSetCopyFilter(GXBool aa, const void* pattern, GXBool vf, const void* vfilter)` | pc_gx.c | port-as-is |  |
| `GXAdjustForOverscan` | `void GXAdjustForOverscan(void* rmin, void* rmout, u16 hor, u16 ver)` | pc_gx.c | port-as-is |  |

### GX — EFB copy / render-to-texture

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_gx_efb_capture_store` ✱ | `void pc_gx_efb_capture_store(u32 dest_ptr, GLuint gl_tex)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_efb_capture_find` ✱ | `GLuint pc_gx_efb_capture_find(u32 data_ptr)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_efb_capture_cleanup` ✱ | `void pc_gx_efb_capture_cleanup(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTexCopySrc` | `void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht)` | pc_gx.c | rewrite-for-KOS |  |
| `GXSetTexCopyDst` | `void GXSetTexCopyDst(u16 wd, u16 ht, u32 fmt, GXBool mipmap)` | pc_gx.c | rewrite-for-KOS |  |
| `GXCopyTex` | `void GXCopyTex(void* dest, GXBool clear)` | pc_gx.c | rewrite-for-KOS | ★ EFB->texture capture. PC does an FBO blit. PVR render-to-texture is costly; enumerate the AC call sites (PLAN §3.3) and handle per-case. |
| `GXSetCopyClamp` | `void GXSetCopyClamp(u32 clamp)` | pc_gx.c | rewrite-for-KOS |  |

### GX — display lists

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXBeginDisplayList` | `void GXBeginDisplayList(void* list, u32 size)` | pc_gx.c | rewrite-for-KOS |  |
| `GXEndDisplayList` | `u32 GXEndDisplayList(void)` | pc_gx.c | rewrite-for-KOS |  |
| `GXCallDisplayList` | `void GXCallDisplayList(void* list, u32 nbytes)` | pc_gx.c | rewrite-for-KOS |  |

### GX — FIFO / write-gather (all no-ops)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXInitFifoBase` | `void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size)` | pc_gx.c | stub-and-log |  |
| `GXInitFifoPtrs` | `void GXInitFifoPtrs(GXFifoObj* fifo, void* rp, void* wp)` | pc_gx.c | stub-and-log |  |
| `GXInitFifoLimits` | `void GXInitFifoLimits(GXFifoObj* fifo, u32 hi, u32 lo)` | pc_gx.c | stub-and-log |  |
| `GXSetCPUFifo` | `void GXSetCPUFifo(GXFifoObj* fifo)` | pc_gx.c | stub-and-log |  |
| `GXSetGPFifo` | `void GXSetGPFifo(GXFifoObj* fifo)` | pc_gx.c | stub-and-log |  |
| `GXSaveCPUFifo` | `void GXSaveCPUFifo(GXFifoObj* fifo)` | pc_gx.c | stub-and-log |  |
| `GXSaveGPFifo` | `void GXSaveGPFifo(GXFifoObj* fifo)` | pc_gx.c | stub-and-log |  |
| `GXGetGPStatus` | `void GXGetGPStatus(GXBool* a, GXBool* b, GXBool* c, GXBool* d, GXBool* e)` | pc_gx.c | stub-and-log |  |
| `GXGetFifoStatus` | `void GXGetFifoStatus(GXFifoObj* f, GXBool* a, GXBool* b, u32* c, GXBool* d, GXBool* e, GXBool* g)` | pc_gx.c | stub-and-log |  |
| `GXGetFifoPtrs` | `void GXGetFifoPtrs(GXFifoObj* f, void** rp, void** wp)` | pc_gx.c | stub-and-log |  |
| `GXGetFifoBase` | `void* GXGetFifoBase(GXFifoObj* f)` | pc_gx.c | stub-and-log |  |
| `GXGetFifoSize` | `u32 GXGetFifoSize(GXFifoObj* f)` | pc_gx.c | stub-and-log |  |
| `GXGetFifoLimits` | `void GXGetFifoLimits(GXFifoObj* f, u32* hi, u32* lo)` | pc_gx.c | stub-and-log |  |
| `GXSetBreakPtCallback` | `void* GXSetBreakPtCallback(void* cb)` | pc_gx.c | stub-and-log |  |
| `GXEnableBreakPt` | `void GXEnableBreakPt(void* bp)` | pc_gx.c | stub-and-log |  |
| `GXDisableBreakPt` | `void GXDisableBreakPt(void)` | pc_gx.c | stub-and-log |  |
| `GXSetCurrentGXThread` | `void* GXSetCurrentGXThread(void)` | pc_gx.c | stub-and-log |  |
| `GXGetCurrentGXThread` | `void* GXGetCurrentGXThread(void)` | pc_gx.c | stub-and-log |  |
| `GXGetCPUFifo` | `GXFifoObj* GXGetCPUFifo(void)` | pc_gx.c | stub-and-log |  |
| `GXGetGPFifo` | `GXFifoObj* GXGetGPFifo(void)` | pc_gx.c | stub-and-log |  |
| `GXGetOverflowCount` | `u32 GXGetOverflowCount(void)` | pc_gx.c | stub-and-log |  |
| `GXResetOverflowCount` | `u32 GXResetOverflowCount(void)` | pc_gx.c | stub-and-log |  |
| `GXRedirectWriteGatherPipe` | `volatile void* GXRedirectWriteGatherPipe(void* ptr)` | pc_gx.c | stub-and-log |  |
| `GXRestoreWriteGatherPipe` | `void GXRestoreWriteGatherPipe(void)` | pc_gx.c | stub-and-log |  |
| `IsWriteGatherBufferEmpty` | `int IsWriteGatherBufferEmpty(void)` | pc_gx.c | stub-and-log | GC write-gather pipe; always empty. All FIFO functions are pure no-ops with 0 semantic content. |

### GX — unused / verify

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GXDrawSphere` | `void GXDrawSphere(u8 numMajor, u8 numMinor)` | pc_gx.c | drop |  |
| `GXReadXfRasMetric` | `void GXReadXfRasMetric(u32* xf_wait_in, u32* xf_wait_out, u32* ras_busy, u32* clocks)` | pc_gx.c | drop |  |
| `GXSetVerifyLevel` | `void GXSetVerifyLevel(u32 level)` | pc_gx.c | drop |  |
| `GXSetVerifyCallback` | `void* GXSetVerifyCallback(void* cb)` | pc_gx.c | drop |  |

### GX — platform-internal helpers

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_gx_flush_if_begin_complete` ✱ | `void pc_gx_flush_if_begin_complete(void)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_mark_dirty` ✱ | `void pc_gx_mark_dirty(unsigned int flag)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_init` ✱ | `void pc_gx_init(void)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_begin_frame` | `void pc_gx_begin_frame(void)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_restore_after_nes` ✱ | `void pc_gx_restore_after_nes(void)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_shutdown` ✱ | `void pc_gx_shutdown(void)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_blit_to_screen` ✱ | `void pc_gx_blit_to_screen(void)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_fill_uniform_locations` ✱ | `void pc_gx_fill_uniform_locations(GLuint shader, PCGXUniformLocs* u)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_flush_vertices` ✱ | `void pc_gx_flush_vertices(void)` | pc_gx.c | rewrite-for-KOS |  |
| `pc_gx_frame_timing_snapshot` ✱ | `void pc_gx_frame_timing_snapshot(void)` | pc_gx.c | rewrite-for-KOS |  |

### GX — texture objects, TLUT, texture cache

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_gx_texture_cache_invalidate` ✱ | `void pc_gx_texture_cache_invalidate(void)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `pc_gx_texture_init` ✱ | `void pc_gx_texture_init(void)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `pc_gx_texture_shutdown` ✱ | `void pc_gx_texture_shutdown(void)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXInitTexObj` | `void GXInitTexObj(void* obj, void* image_ptr, u16 width, u16 height, u32 format, u32 wrap_s, u32 wrap_t, u8 mipmap)` | pc_gx_texture.c | rewrite-for-KOS | ★ TexObj is an opaque blob the GAME allocates (GXTexObj, 32 bytes) — the DC layer must fit its state in the same footprint or the game's structs overflow. |
| `GXInitTexObjCI` | `void GXInitTexObjCI(void* obj, void* image_ptr, u16 width, u16 height, u32 format, u32 wrap_s, u32 wrap_t, u8 mipmap, u32 tlut_name)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXInitTexObjData` | `void GXInitTexObjData(void* obj, void* image_ptr)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXInitTexObjLOD` | `void GXInitTexObjLOD(void* obj, u32 min_filt, u32 mag_filt, f32 min_lod, f32 max_lod, f32 lod_bias, GXBool bias_clamp, GXBool edge_lod, u32 max_aniso)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXInitTexObjWrapMode` | `void GXInitTexObjWrapMode(void* obj, u32 s, u32 t)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXLoadTexObj` | `void GXLoadTexObj(void* obj, u32 id)` | pc_gx_texture.c | rewrite-for-KOS | Binds to a texture unit id (GX_TEXMAP0..7). PVR has ONE texture unit — multi-map TEV stages become extra passes. |
| `GXGetTexBufferSize` | `u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, GXBool mipmap, u8 max_lod)` | pc_gx_texture.c | rewrite-for-KOS | Pure arithmetic over GC texture formats; used by the game to size allocations. Must keep returning GC sizes even though DC stores twiddled/VQ data elsewhere. |
| `GXInvalidateTexAll` | `void GXInvalidateTexAll(void)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXInvalidateTexRegion` | `void GXInvalidateTexRegion(void* region)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXInitTlutObj` | `void GXInitTlutObj(void* obj, void* lut, u32 fmt, u16 n_entries)` | pc_gx_texture.c | rewrite-for-KOS | TLUT (palette) object. CI4/CI8 map to PVR native 4/8-bit paletted textures — a VRAM win. |
| `GXLoadTlut` | `void GXLoadTlut(void* obj, u32 idx)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `pc_gx_tlut_set_native_le` | `void pc_gx_tlut_set_native_le(unsigned int idx)` | pc_gx_texture.c | rewrite-for-KOS | ★ Per-slot is_be flag: ROM-sourced TLUTs are big-endian, emu64/EFB ones are native LE. This distinction survives on DC unchanged. |
| `GXInitTexCacheRegion` | `void GXInitTexCacheRegion(void* region, GXBool is_32b, u32 tmem_even, u32 size_even, u32 tmem_odd, u32 size_odd)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXSetTexRegionCallback` | `void* GXSetTexRegionCallback(void* callback)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXInitTlutRegion` | `void GXInitTlutRegion(void* region, u32 tmem_addr, u32 tlut_size)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXGetTexObjMipMap` | `GXBool GXGetTexObjMipMap(const void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXGetTexObjFmt` | `u32 GXGetTexObjFmt(const void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXGetTexObjHeight` | `u16 GXGetTexObjHeight(const void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXGetTexObjWidth` | `u16 GXGetTexObjWidth(const void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXGetTexObjWrapS` | `u32 GXGetTexObjWrapS(const void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXGetTexObjWrapT` | `u32 GXGetTexObjWrapT(const void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXGetTexObjData` | `void* GXGetTexObjData(const void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXDestroyTexObj` | `void GXDestroyTexObj(void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |
| `GXDestroyTlutObj` | `void GXDestroyTlutObj(void* obj)` | pc_gx_texture.c | rewrite-for-KOS |  |

### GX — TEV→GLSL shader generator

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_gx_tev_init` ✱ | `void pc_gx_tev_init(void)` | pc_gx_tev.c | drop |  |
| `pc_gx_tev_shutdown` ✱ | `void pc_gx_tev_shutdown(void)` | pc_gx_tev.c | drop |  |
| `pc_gx_tev_get_shader` ✱ | `GLuint pc_gx_tev_get_shader(PCGXState* state)` | pc_gx_tev.c | drop |  |
