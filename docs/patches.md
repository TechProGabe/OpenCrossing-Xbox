# Patches outside `xbox/`

Every change this port makes to `src/`, `include/` or `pc/` is listed here
with its reason. Anything not listed is identical to its upstream
(`docs/upstream.md`). Keep the list current: CLAUDE.md requires it.

## `src/` + `include/`: clang strictness

Behaviour-identical, under `#if defined(TARGET_XBOX)`:

| file | symbol | why |
|---|---|---|
| `include/JSystem/JSupport/JSUIosBase.h` | `setState(int)`, `clrState(int)` | callers pass stdio `EOF` (int) |
| `include/JSystem/JSupport/JSURandomInputStream.h` | `seek(s32,int)`, `seekPos(s32,int)` ×2 | callers pass stdio `SEEK_*` |
| `src/static/JSystem/JKernel/JKRHeap.cpp` | `operator new/new[]` | must take `size_t`; 2-arg form must match header's `s32` |
| `src/static/JSystem/JKernel/JKRDvdRipper.cpp` | `isErrorRetry == false` | fn-vs-bool compare; same always-false test |
| `src/static/Famicom/famicom.cpp` | `SetupResBanner` call | `u32*` → `size_t*` |
| `src/static/libjsys/jsyswrapper_main.cpp` | `JC__JKRGetResourceEntry_byName` | `void*` → `CSDIFileEntry*` cast (unguarded, valid everywhere) |

## `src/`: memory

Behaviour change, kill switch `-DXBOX_ARAM_FLAT=1`:

| file | symbol | why |
|---|---|---|
| `src/static/jaudio_NES/internal/dvdthread.c` | `DVDT_LoadtoARAM_Main` | `xbox_aram_map_file`: `audiorom.img` served from disc, not copied (8.3 MB) |
| `src/static/JSystem/JKernel/JKRAramArchive.cpp` | `JKRAramArchive::open` | `xbox_aram_map_entry`: uncompressed RARC data served from disc (~6.5 MB) |

## `pc/`

All three are bug fixes that apply upstream too; worth sending to
flyngmt/ACGC-PC-Port.

| file | change | why |
|---|---|---|
| `pc/src/pc_gx_texture.c` | `tex_cache_insert` drops older entries for the same large (≥128×128) buffer | every inventory open grabs the screen into one reused buffer; old versions stayed cached (eviction only at 2048 entries) and filled the Xbox's 8 MB texture pool, so the menu background went white |
| `pc/src/pc_disc.c` | `pc_disc_read` takes an SDL mutex | fseek+fread pair on one `FILE*` raced between DVD, audio and game threads |
| `pc/include/pc_gx_internal.h` | `PC_GX_MAX_VERTS` is `#ifndef`-guarded | the Xbox build passes 16384 (vertex batch 6 MB → 1.5 MB) |

`pc/src/pc_pad.c` is compiled with `SDL_GameControllerGetAxis` renamed to
`xbox_controller_axis` (`xbox/CMakeLists.txt`); the file itself is untouched.
