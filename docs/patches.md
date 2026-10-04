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

## `src/`: log noise

Under `#if !defined(TARGET_XBOX)`:

| file | symbol | why |
|---|---|---|
| `src/static/jaudio_NES/internal/neosthread.c` | `Neos_Update` `[NEOS_OUT]` diagnostic | the PC port's once-a-second audio level line kept the watchdog rewriting `last.log` on the HDD every 3 s and pushed everything else out of its 4 KB tail |

## `src/` + `include/`: bug fixes that apply upstream too

| file | symbol | why |
|---|---|---|
| `src/static/libforest/emu64/emu64_utility.c` | `emu64::seg2k0` (existing `#ifdef TARGET_PC` branch) | a segment base set from an odd pointer holds a `pc_gbi_runtime.c` token (`0x02F00000 + 2n`), not an address; it was added to the offset raw, so textures bound through segments 8/9 (the station statues' eyes and mouths, `ac_douzou_draw.c_inc`) were read from unmapped memory. On the Xbox that faulted the whole console (the Resetti / train-station / Tortimer freezes); now the token is unpacked |
| `include/m_lib.h` | `DEG2SHORT_ANGLE`, `RAD2SHORTANGLE`, `RAD2SHORT_ANGLE2` convert through `int` (`#if defined(TARGET_PC)`; the GameCube-matching build keeps the original macros) | `(s16)32768.0f` is undefined behaviour in C (the value doesn't fit). Clang folds `DEG2SHORT_ANGLE(180)` to poison and deletes the code that depends on it: `aMR_JudgeBreedNewFurniture` was compiled down to "refuse", so no furniture could be put down indoors. 14 files had such constants (museum, igloo and buggy doors, museum fish and insects, Majin, effects, `f_furniture.c`). Through `int` the value wraps to -32768 like the GameCube's `fctiwz` + truncate |

## `src/`: Xbox robustness

Under `#if defined(TARGET_XBOX)`:

| file | symbol | why |
|---|---|---|
| `src/static/libforest/emu64/emu64.c` | `emu64::dl_G_DL` skips a display list whose address isn't mapped (`xbox_ptr_readable`) and logs it (`[EMU64] gsSPDisplayList(...) not mapped`, 16 at most) | v8 on hardware faulted reading the first command of a "display list" at `0xc1180000` (the float -9.5) a few seconds into a new scene after an NES game; on a PC that read would usually just return junk. Skipping one call loses a model piece for a frame instead of the session |
| `src/famicom_emu.c` | `famicom_emu_main` goes back to the room when `pc_fixnes_failed()` | a game fixNES couldn't start (Clu Clu Land D, a disk image) otherwise sat on a black screen, or crashed |
| `src/famicom_emu.c` | `famicom_emu_init`: the NES emulator's heap falls back to 3.5, 3 or 2.75 MB when the PC branch's 4 MB `malloc` fails | at 720p only ~3.8 MB is free, the 4 MB heap failed and the room showed "memory card in slot A could not be read"; `famicom_init`'s buffers take ~2.6 MB |
| `src/static/Famicom/famicom.cpp` | `famicom_emu` frame: `famicom_draw()` skipped | fixNES (`pc_fixnes_render_frame`) already draws the frame, 4:3 or stretched per `nes_aspect`. The GameCube quad samples `result_bufp`, which fixNES never fills, but pc_gx's texture bind cache didn't know fixNES had rebound unit 0, so it drew the NES picture a second time, full screen, over the 4:3 one (hardware shot, v6) |

## `src/`: symbol renames by compile flag

No source edit; `xbox/CMakeLists.txt` compiles the TU with a `-D`:

| file | definition | why |
|---|---|---|
| `src/lb_rtc.c` | `lbRTC_Sub_DD=lbRTC_Sub_DD_game`, only with `-DXBOX_RTC_SHIM=ON` (implied by `-DXBOX_DIAG_ISSUE3=ON`) | GitHub #3 (`known-issues.md`): `xbox/src/xbox_diag.c` defines `lbRTC_Sub_DD` for every other TU and runs either the game's function or a plain -O0 copy of the same C (`settings.ini` `rtc_shim = 1`), to test whether that console's CPU faults on the -O2 code's byte sequence. Off in normal builds: the symbol and code are the upstream ones |

## `src/`: NES diagnostics (Xbox only)

`[NES]` log lines under `#if defined(TARGET_XBOX)`; no behaviour change:

| file | symbol | logs |
|---|---|---|
| `src/actor/ac_my_room_msg_ctrl.c_inc` | `aMR_SetEmulatorStartMessage` | internal ROM or not, player, the card check's result |
| `src/famicom_emu.c` | `famicom_emu_init`, `famicom_emu_main` | the ROM id, `famicom_init` and ROM load failures, the emulator heap's size |
| `src/static/Famicom/famicom.cpp` | `pc_nes_rom_scan`, the ROM load, `famicom_internal_data_load/_save` | a missing `nes_roms` folder, a memory-card game not found, the NES save file read/write |

## `pc/`: bug fixes that apply upstream

Worth sending to flyngmt/ACGC-PC-Port:

| file | change | why |
|---|---|---|
| `pc/src/pc_gx_texture.c` | `tex_cache_insert` drops older entries for the same large (≥128×128) buffer | every inventory open grabs the screen into one reused buffer; old versions stayed cached (eviction only at 2048 entries) and filled the Xbox's 8 MB texture pool, so the menu background went white |
| `pc/src/pc_gx_texture.c` | `tex_cache_find` walks a per-pointer chain instead of the whole cache (`PC_TEX_CACHE_INDEX`, 0 = the linear scan) | the scan of up to 2048 entries on every `GXLoadTexObj` was the top function of a busy town frame on the Xbox (8.6% of ~19 ms, v4 profile). Chains are rebuilt after any compaction |
| `pc/src/pc_os.c` | `osGetTime` multiplies whole seconds and the remainder apart | `(now - start) * 40.5 MHz` overflowed 64 bits 10.4 minutes after boot with the Xbox's 733 MHz counter, and the game clock jumped back every 10.4 minutes (on a PC, after ~12.7 hours at 10 MHz) |
| `pc/src/pc_os.c` | `OSInit`: a failed `mktime` gives a 0 time-zone offset | `difftime(now, -1)` made the "offset" the whole Unix time (nxdk's `mktime` returns -1: the clock read ~2083) |
| `pc/src/pc_disc.c` | `pc_disc_read` takes an SDL mutex | fseek+fread pair on one `FILE*` raced between DVD, audio and game threads |
| `pc/src/pc_m_card.c` | `mCD_EraseLand_bg` and `mCD_SaveErasePlayer_bg` write the save (they were stubs returning success) | "clear village data" and erasing a player at player select did nothing on disk: the town or player came back on the next load. Erasing a town now rewrites the town file with its save check cleared, as the GameCube does (`mCD_EraseLand_bg_set_data`), so the next load starts a new town; the old file stays as `.bak1`. Both run before any game start, so they write past the `pc_save_ready` gate |
| `pc/src/pc_m_card.c` | `mCD_SaveStation_NextLand_bg` loads the Card B town before it marks the player away and saves home; a failed save restores `exists`/`reset_code`; every failure clears `l_keepSave_set` | the trip used to save home with the player away first, so a Card B that couldn't be read left them away (the gyroid punishment on the next load) with `l_keepSave` still armed for `mCD_toNextLand` at the next scene change |
| `pc/src/pc_nes_fixnes.c` | `pc_fixnes_init` rejects a ROM without the iNES `NES\x1a` magic and a failed PRG RAM `malloc`, logs why, and reports it (`pc_fixnes_failed`) | Clu Clu Land D is a Famicom Disk System image: its "header" asked for 136 KB of PRG RAM, the `malloc` failed at 720p (436 KB free) and `memset(NULL)` crashed the console (v9). On a PC the mapper parse of disk data fails into a black screen |
| `pc/include/pc_gx_internal.h` | `PC_GX_MAX_VERTS` is `#ifndef`-guarded | the Xbox build passes 16384 (vertex batch 6 MB → 1.5 MB) |

## `pc/`: Xbox only

Under `#ifdef TARGET_XBOX`:

| file | change | why |
|---|---|---|
| `pc/src/pc_os.c` | `OSInit`: the time-zone offset is the dashboard's time zone and DST (`xbox_local_offset_secs`) | the game clock reads local time, as a GameCube's does |
| `pc/src/pc_m_card.c` | a loaded save's `time_delta` beyond ±40 years goes to 0 (`pc_xbox_fix_time_delta`; Card A's town and a visited Card B town) | deltas set under the old Xbox clock are ~-56 years and would put the fixed clock in ~1970; the game's years make anything past 40 impossible otherwise |
| `pc/src/pc_m_card.c` | `mCD_CheckStation_bg` answers `mCD_TRANS_ERR_NO_TOWN_DATA` when `save/card_b` has no other town | it answered `mCD_TRANS_ERR_NONE`, the GameCube's passport trip (the traveller saved to slot B, then the title screen); the PC port keeps the passport in memory only, so on the Xbox (hardware, 2026-10-03) the train went nowhere and dropped the player on the title |
| `pc/src/pc_gx_texture.c` | logs a decode buffer that couldn't be allocated | the texture is drawn white then; the log says why |
| `pc/src/pc_gx_texture.c` | `pc_gx_load_tex_obj_impl` checks the image pointer (`xbox_tex_ptr_ok`) | a texture pointer into unmapped memory faults the console; it is drawn without the image and logged instead (belt and braces behind the `seg2k0` fix) |

## Built differently, file untouched (`xbox/CMakeLists.txt`)

| file | how | why |
|---|---|---|
| `pc/src/pc_settings_menu.c` | not built; `xbox/src/xbox_settings_menu.c` implements the same `pc_settings_menu_*` API | Xbox rows (output, widescreen, radial dead zone, rumble, controller-only bindings) and the game-style look |
| `pc/src/pc_settings.c` | `pc_settings_load`/`pc_settings_save` renamed to `*_pc`; `xbox/src/xbox_settings.c` wraps them | adds the `[Xbox]` section of `settings.ini` |
| `pc/src/pc_pad.c` | `SDL_GameControllerGetAxis` → `xbox_controller_axis`, `SDL_GameControllerRumble` → `xbox_controller_rumble`, `g_pc_settings` → `g_xbox_pad_settings` | the radial left-stick dead zone and spike filter in `xbox_pad_axis.c`, rumble scaled by the setting; the copy of the settings has the left stick's per-axis dead zone zeroed, so the saved setting is never overwritten |
| `pc/src/pc_card.c` | `-U_WIN32` | nxdk defines `_WIN32`, and that branch of `pc_card_scan_for_gci` calls `FindFirstFileA` on a relative `save/card_a\*.gci`, which never resolves: a save under any name but the two exact ones was never found, nor a town in `save/card_b`. The POSIX branch goes through `xbox_posix.c`'s `opendir` (save/ → UDATA) |
| `pc/src/pc_disc.c` | `-DXBOX_DISC_TU`: `xbox_prelude.h` maps its `fread`/`fseek` to `xbox_disc_fread`/`xbox_disc_fseek` | pdclib reads 1 KB per kernel call; the disc image goes straight to `ReadFile` (`perf.md`) |
| `pc/src/pc_vi.c` | `g_frame_limiter` → `g_xbox_vi_frame_limit`, `g_pc_nes_active` → `g_xbox_vi_nes_pace` | the vblank pacer in `xbox_nv2a.c` owns the real variables and sets what the timer limiter sees (off while it paces; `perf.md`) |
| `pc/src/pc_os.c`, `pc/src/pc_vi.c`, `pc/src/pc_profiler.c` | `SDL_GetPerformanceFrequency` → `xbox_perf_frequency` | nxdk's frequency for the CPU's cycle counter falls back to 733 MHz on CPUs missing from its table; on an upgraded CPU the game clock and the frame limiter ran fast. `xbox_clock_check` (`xbox_io.c`) times the counter against the ACPI timer at boot and replaces a frequency more than 3% off |
| `pc/src/pc_vi.c`, `src/actor/ac_animal_logo.c` | `g_pc_verbose` → `g_xbox_verbose_noisy` (0 = off) | their verbose lines come every second (`[LOGO] draw`) or every slow frame (`[STUTTER]`, whose `%f` pdclib prints empty); the title menu's one-shot `[LOGO]` transition lines go with them. Verbose stays on elsewhere |
| `src/static/Famicom/famicom.cpp` | `pc_fixnes_frame` → `xbox_nes_frame`; `fopen=xbox_fopen`, `fclose=xbox_fclose` | `xbox_nv2a.c` times the emulator around the real call (`[NES]` lines); C++ files don't get the prelude's path routing, so the NES save file (a relative `save/card_a/...` path) was never read or written on the Xbox |
| `src/actor/ac_animal_logo.c` | `-DPC_ENHANCEMENTS` for this file only (`XBOX_TITLE_MENU`) | upstream's title Start / Options / Quit menu. Elsewhere the define changes gameplay; the actor struct is the same size either way (`include/ac_animal_logo.h`) |
| `pc/src/pc_gx.c`, `pc/src/pc_gx_texture.c`, `src/game/m_actor.c`, `src/static/libforest/emu64/emu64.c` | `-DPC_ENHANCEMENTS` for these files only (`XBOX_WIDESCREEN`) | hor+ widescreen, viewport scaling and the matching wider culling. The only header guards it touches are declarations (`pc_gx_internal.h`, `m_private.h`), so layouts match the other TUs. At 4:3 the one behaviour change is that EFB copies stay full-res GL textures instead of RGB565 written into game memory |
| `src/data/**` | `-fcommon` | the PC branch declares textures as tentative definitions, which then become COFF common symbols that lld aligns up to 32 bytes; without it they could sit at odd addresses (`traps.md`) |
