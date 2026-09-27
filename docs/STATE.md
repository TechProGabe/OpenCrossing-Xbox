# State

**Short by design.** What is true now + next action.

## Where the port is (2026-09-27)

- M0 done: PC port v0.9.3 (`4099d246`) + ac-decomp head (`09ca8e8b`).
- **M1 done:** every TU (~3,990) compiles under nxdk (clang 21), links, and
  `default.xbe` (8.6 MB file, image `0x10000-0x1D23118` = 29 MB incl. BSS)
  boots in xemu. Splash + missing-disc screen verified by screenshot.
- **M2 (headless) mostly done:** with the ISO beside the XBE the game parses
  the disc (14,495 assets), mounts forest_1st/2nd + famicom archives into
  ARAM, opens audio, scans saves in UDATA and runs its main loop — 900 frames
  in ~20 s in xemu at 64 MB (`pc_gx*` against `xbox_gl_stub.c`, no pixels).
  Arena lands at `0x7E7D3108` (above the N64 segment range).

## Next action

M3: NV2A backend (pbkit/xgu) behind `pc_gx.c` → first pixels. Then drive
input to confirm the town scene.

## `src/` + `include/` TARGET_XBOX branch ledger

All are clang strictness (no `-fpermissive`), behaviour-identical:

| file | symbol | why |
|---|---|---|
| `include/JSystem/JSupport/JSUIosBase.h` | `setState(int)`, `clrState(int)` | callers pass stdio `EOF` (int) |
| `include/JSystem/JSupport/JSURandomInputStream.h` | `seek(s32,int)`, `seekPos(s32,int)` ×2 | callers pass stdio `SEEK_*` |
| `src/static/JSystem/JKernel/JKRHeap.cpp` | `operator new/new[]` | must take `size_t`; 2-arg form must match header's `s32` |
| `src/static/JSystem/JKernel/JKRDvdRipper.cpp` | `isErrorRetry == false` | fn-vs-bool compare; same always-false test |
| `src/static/Famicom/famicom.cpp` | `SetupResBanner` call | `u32*` → `size_t*` |
| `src/static/libjsys/jsyswrapper_main.cpp` | `JC__JKRGetResourceEntry_byName` | `void*` → `CSDIFileEntry*` cast (unguarded, valid everywhere) |

## Closed (do not re-propose)

- 64-bit build — decomp assumes 32-bit pointers everywhere; Xbox is 32-bit anyway.
- GLSL/shader-cache path from `pc_gx_tev.c` — no GLSL compiler on nxdk.
