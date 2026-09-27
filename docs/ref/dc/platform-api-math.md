# Platform API — MTX / GU math (PPC paired-singles replacement)

Every `PSMTX*` / `C_MTX*` / `gu*` entry point, all `port-as-is` scalar C today,
with the SH-4 FTRV/FIPR rewrite candidates marked.
Read when planning SH-4 math work (PLAN §3.2).
Split out of `kb/design-platform-api.md` §5. Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### MTX / GU math (PPC paired-singles replacement)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `PSMTXIdentity` | `void PSMTXIdentity(MtxP m)` | pc_mtx.c | port-as-is |  |
| `C_MTXIdentity` | `void C_MTXIdentity(MtxP m)` | pc_mtx.c | port-as-is |  |
| `PSMTXCopy` | `void PSMTXCopy(const MtxP src, MtxP dst)` | pc_mtx.c | port-as-is |  |
| `PSMTXConcat` | `void PSMTXConcat(const MtxP a, const MtxP b, MtxP result)` | pc_mtx.c | port-as-is | PPC paired-singles replaced with scalar C. #1 candidate for SH-4 FTRV (4x4 matrix multiply in one instruction). |
| `PSMTXInverse` | `void PSMTXInverse(const MtxP src, MtxP inv)` | pc_mtx.c | port-as-is | Scalar; not hot. |
| `PSMTXMultVec` | `void PSMTXMultVec(const MtxP m, const Vec* src, Vec* dst)` | pc_mtx.c | port-as-is |  |
| `PSMTXMultVecSR` | `void PSMTXMultVecSR(const MtxP m, const Vec* src, Vec* dst)` | pc_mtx.c | port-as-is |  |
| `PSMTXMultVecArray` | `void PSMTXMultVecArray(const MtxP m, const Vec* srcBase, Vec* dstBase, u32 count)` | pc_mtx.c | port-as-is | Bulk vector transform — SH-4 FTRV loop, ~32 vertices per block (dca3 pattern). |
| `PSMTXScale` | `void PSMTXScale(MtxP m, f32 sx, f32 sy, f32 sz)` | pc_mtx.c | port-as-is |  |
| `PSMTXTrans` | `void PSMTXTrans(MtxP m, f32 tx, f32 ty, f32 tz)` | pc_mtx.c | port-as-is |  |
| `PSMTXTransApply` | `void PSMTXTransApply(const MtxP src, MtxP dst, f32 tx, f32 ty, f32 tz)` | pc_mtx.c | port-as-is |  |
| `PSMTXScaleApply` | `void PSMTXScaleApply(const MtxP src, MtxP dst, f32 sx, f32 sy, f32 sz)` | pc_mtx.c | port-as-is |  |
| `PSVECNormalize` | `void PSVECNormalize(const Vec* src, Vec* dst)` | pc_mtx.c | port-as-is |  |
| `PSVECCrossProduct` | `void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* dst)` | pc_mtx.c | port-as-is |  |
| `PSVECDotProduct` | `f32 PSVECDotProduct(const Vec* a, const Vec* b)` | pc_mtx.c | port-as-is | SH-4 FIPR (4-way dot product) candidate. |
| `PSVECMag` | `f32 PSVECMag(const Vec* v)` | pc_mtx.c | port-as-is |  |
| `C_MTXFrustum` | `void C_MTXFrustum(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)` | pc_mtx.c | port-as-is |  |
| `C_MTXPerspective` | `void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f)` | pc_mtx.c | port-as-is | Builds a GC-convention projection (z in [-1,0]); PVR wants its own W-buffer convention — check this before debugging 'everything is inside out'. |
| `C_MTXOrtho` | `void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)` | pc_mtx.c | port-as-is |  |
| `C_MTXLookAt` | `void C_MTXLookAt(MtxP m, const Vec* camPos, const Vec* camUp, const Vec* target)` | pc_mtx.c | port-as-is |  |
| `C_MTXLightPerspective` | `void C_MTXLightPerspective(MtxP m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT)` | pc_mtx.c | port-as-is |  |
| `C_MTXLightOrtho` | `void C_MTXLightOrtho(MtxP m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT)` | pc_mtx.c | port-as-is |  |
| `guMtxIdentF` | `void guMtxIdentF(float mf[4][4])` | pc_mtx.c | port-as-is |  |
| `guMtxF2L` | `void guMtxF2L(float mf[4][4], Mtx* m)` | pc_mtx.c | port-as-is |  |
| `guMtxIdent` | `void guMtxIdent(Mtx* m)` | pc_mtx.c | port-as-is |  |
| `guOrthoF` | `void guOrthoF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale)` | pc_mtx.c | port-as-is |  |
| `guOrtho` | `void guOrtho(Mtx* m, float l, float r, float b, float t, float n, float f, float scale)` | pc_mtx.c | port-as-is |  |
| `guPerspectiveF` | `void guPerspectiveF(float mf[4][4], u16* perspNorm, float fovy, float aspect, float near, float far, float scale)` | pc_mtx.c | port-as-is |  |
| `guPerspective` | `void guPerspective(Mtx* m, u16* perspNorm, float fovy, float aspect, float near, float far, float scale)` | pc_mtx.c | port-as-is |  |
| `guLookAtF` | `void guLookAtF(float mf[4][4], float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp)` | pc_mtx.c | port-as-is |  |
| `guLookAt` | `void guLookAt(Mtx* m, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp)` | pc_mtx.c | port-as-is |  |
| `guLookAtHilite` | `void guLookAtHilite(Mtx* m, void* lv, void* hv, float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp, float yUp, float zUp, float xl1, float yl1, float zl1, float xl2, float yl2, float zl2, int twidth, int theight)` | pc_mtx.c | port-as-is |  |
| `guScale` | `void guScale(Mtx* m, float x, float y, float z)` | pc_mtx.c | port-as-is |  |
| `guTranslate` | `void guTranslate(Mtx* m, float x, float y, float z)` | pc_mtx.c | port-as-is |  |
| `guRotateF` | `void guRotateF(float mf[4][4], float a, float x, float y, float z)` | pc_mtx.c | port-as-is |  |
| `guRotate` | `void guRotate(Mtx* m, float a, float x, float y, float z)` | pc_mtx.c | port-as-is |  |
| `guNormalize` | `void guNormalize(float* x, float* y, float* z)` | pc_mtx.c | port-as-is |  |
