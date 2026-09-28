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

## `src/`: pointer handling

Bug fix, in the existing `#ifdef TARGET_PC` branch (applies upstream too):

| file | symbol | why |
|---|---|---|
| `src/static/libforest/emu64/emu64_utility.c` | `emu64::seg2k0` | a segment base set from an odd pointer holds a `pc_gbi_runtime.c` token (`0x02F00000 + 2n`), not an address; it was added to the offset raw, so textures bound through segments 8/9 (the station statues' eyes and mouths, `ac_douzou_draw.c_inc`) were read from unmapped memory. On the Xbox that faulted the whole console (the Resetti / train-station / Tortimer freezes); now the token is unpacked |

## `pc/`

All are bug fixes that apply upstream too; worth sending to
flyngmt/ACGC-PC-Port.

| file | change | why |
|---|---|---|
| `pc/src/pc_gx_texture.c` | `tex_cache_insert` drops older entries for the same large (≥128×128) buffer | every inventory open grabs the screen into one reused buffer; old versions stayed cached (eviction only at 2048 entries) and filled the Xbox's 8 MB texture pool, so the menu background went white |
| `pc/src/pc_disc.c` | `pc_disc_read` takes an SDL mutex | fseek+fread pair on one `FILE*` raced between DVD, audio and game threads |
| `pc/include/pc_gx_internal.h` | `PC_GX_MAX_VERTS` is `#ifndef`-guarded | the Xbox build passes 16384 (vertex batch 6 MB → 1.5 MB) |
| `pc/src/pc_gx_texture.c` | `pc_gx_load_tex_obj_impl` checks the image pointer (`#ifdef TARGET_XBOX`, `xbox_tex_ptr_ok`) | a texture pointer into unmapped memory faults the console; it is drawn without the image and logged instead (belt and braces behind the `seg2k0` fix) |

`pc/src/pc_pad.c` is compiled with `SDL_GameControllerGetAxis` renamed to
`xbox_controller_axis` (`xbox/CMakeLists.txt`); the file itself is untouched.
