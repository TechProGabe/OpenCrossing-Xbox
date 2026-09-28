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

## `include/`: float to short conversions

Bug fix, under `#if defined(TARGET_PC)` (applies upstream too; the
GameCube-matching build keeps the original macros):

| file | symbol | why |
|---|---|---|
| `include/m_lib.h` | `DEG2SHORT_ANGLE`, `RAD2SHORTANGLE`, `RAD2SHORT_ANGLE2` convert through `int` | `(s16)32768.0f` is undefined behaviour in C (the value doesn't fit). Clang folds `DEG2SHORT_ANGLE(180)` to poison and deletes the code that depends on it: `aMR_JudgeBreedNewFurniture` was compiled down to "refuse", so no furniture could be put down indoors. 14 files had such constants (museum, igloo and buggy doors, museum fish and insects, Majin, effects, `f_furniture.c`). Through `int` the value wraps to -32768 like the GameCube's `fctiwz` + truncate |

## `pc/`

All are bug fixes that apply upstream too; worth sending to
flyngmt/ACGC-PC-Port.

| file | change | why |
|---|---|---|
| `pc/src/pc_gx_texture.c` | `tex_cache_insert` drops older entries for the same large (≥128×128) buffer | every inventory open grabs the screen into one reused buffer; old versions stayed cached (eviction only at 2048 entries) and filled the Xbox's 8 MB texture pool, so the menu background went white |
| `pc/src/pc_disc.c` | `pc_disc_read` takes an SDL mutex | fseek+fread pair on one `FILE*` raced between DVD, audio and game threads |
| `pc/include/pc_gx_internal.h` | `PC_GX_MAX_VERTS` is `#ifndef`-guarded | the Xbox build passes 16384 (vertex batch 6 MB → 1.5 MB) |
| `pc/src/pc_gx_texture.c` | logs a decode buffer that couldn't be allocated (`#ifdef TARGET_XBOX`) | the texture is drawn white then; the log says why |
| `pc/src/pc_gx_texture.c` | `pc_gx_load_tex_obj_impl` checks the image pointer (`#ifdef TARGET_XBOX`, `xbox_tex_ptr_ok`) | a texture pointer into unmapped memory faults the console; it is drawn without the image and logged instead (belt and braces behind the `seg2k0` fix) |

`pc/src/pc_pad.c` is compiled with `SDL_GameControllerGetAxis` renamed to
`xbox_controller_axis`, `SDL_GameControllerRumble` renamed to
`xbox_controller_rumble` (scaled by the rumble setting) and `g_pc_settings`
renamed to `g_xbox_pad_settings` (a copy with the left stick's per-axis
deadzone zeroed, so the radial one in `xbox_pad_axis.c` replaces it without
overwriting the saved setting; `xbox/CMakeLists.txt`); the file itself is
untouched.

## Built differently, file untouched (`xbox/CMakeLists.txt`)

| file | how | why |
|---|---|---|
| `pc/src/pc_settings_menu.c` | not built; `xbox/src/xbox_settings_menu.c` implements the same `pc_settings_menu_*` API | Xbox rows (output, widescreen, radial dead zone, rumble, controller-only bindings) |
| `pc/src/pc_settings.c` | `pc_settings_load`/`pc_settings_save` renamed to `*_pc`; `xbox/src/xbox_settings.c` wraps them | adds the `[Xbox]` section of `settings.ini` |
| `src/actor/ac_animal_logo.c` | `-DPC_ENHANCEMENTS` for this file only (`XBOX_TITLE_MENU`) | upstream's title Start / Options / Quit menu. Elsewhere the define changes gameplay; the actor struct is the same size either way (`include/ac_animal_logo.h`) |
| `pc/src/pc_gx.c`, `pc/src/pc_gx_texture.c`, `src/game/m_actor.c`, `src/static/libforest/emu64/emu64.c` | `-DPC_ENHANCEMENTS` for these files only (`XBOX_WIDESCREEN`) | hor+ widescreen, viewport scaling and the matching wider culling. The only header guards it touches are declarations (`pc_gx_internal.h`, `m_private.h`), so layouts match the other TUs. At 4:3 the one behaviour change is that EFB copies stay full-res GL textures instead of RGB565 written into game memory |

`src/data/**` is compiled with `-fcommon` (`xbox/CMakeLists.txt`): the PC
branch declares textures as tentative definitions, which then become COFF
common symbols that lld aligns up to 32 bytes; without it they could sit at
odd addresses (`docs/traps.md`).
