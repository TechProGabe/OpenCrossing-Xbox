# Platform API — global variables the platform layer must define

Link-level obligations that are not functions: MMIO shadow arrays, clocks,
render-mode blobs, arena/image bounds, GX state and diagnostics counters.
Read when the link fails on a missing *variable*, or when sizing `dc/` globals.
Split out of `kb/design-platform-api.md` §6. Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

## 6. Global variables the platform layer also defines

Link-level obligations that are not functions. Verified by scanning column-0
definitions in `pc/src/*.c` (excluding the generated `pc_assets.c`).

| symbol | type | file | referenced by game code | DC disposition |
|---|---|---|---|---|
| `__VIRegs[59]`, `__PIRegs[12]`, `__MEMRegs[64]`, `__DSPRegs[32]`, `__DIRegs[16]`, `__SIRegs[0x100]`, `__EXIRegs[0x40]`, `__AIRegs[8]` | `volatile u16/u32[]` | pc_misc.c | yes (decomp reads/writes MMIO shadows) | port-as-is — zeroed dummies; **do not** point these at real DC hardware |
| `__OSPhysicalMemSize`, `__OSSimulatedMemSize` | `u32` = 24 MB | pc_misc.c | yes | rewrite — must match the DC arena size |
| `__OSBusClock` (162 MHz), `__OSCoreClock` (486 MHz) | `u32` | pc_misc.c | yes | port-as-is — these are the *GameCube* clocks the tick rate derives from; changing them changes game timing |
| `__OSTVMode`, `__OSDeviceCode` | `volatile` | pc_misc.c | yes | port-as-is |
| `GXNtsc480IntDf`, `GXNtsc480Int`, `GXMpal480IntDf`, `GXPal528IntDf`, `GXEurgb60Hz480IntDf` | `u8[64]` all-zero | pc_misc.c | yes — `JW_Init` passes `&GXNtsc480IntDf` to `JFWDisplay::createManager` and `JFWSystem::CSetUpParam::renderMode` points at it | rewrite — DC must fill in at least width/height/efbHeight; `JFWSystem::init()` branches on `renderMode->efbHeight < 300` |
| `BaseModule`, `__OSModuleList`, `__OSStringTable` | REL module state | pc_misc.c | yes (`boot.c` walks `BaseModule`) | stub-and-log (NULL) |
| `DiskID[32]` | `u8[]` | pc_misc.c | yes | port-as-is |
| `__gUnkThread1`, `__gCurrentThread`, `__gUnknown800030C0[2]`, `__gUnknown800030E3` | low-memory globals | pc_misc.c | yes | port-as-is |
| `__OSCurrHeap` | `volatile int` = −1 | pc_os.c | yes (`OSAlloc` macro) | port-as-is |
| `osAppNMIBuffer[64]` | `s32[]` | pc_os.c | yes — the zurumode/reset flag words | port-as-is |
| `__osResetSwitchPressed`, `osShutdown` | `int` | pc_os.c | yes | port-as-is |
| `pc_arena_base`, `pc_arena_end` | `u8*` | pc_os.c | yes — seg2k0 pointer heuristic | rewrite (§3.1, PLAN §11.6) |
| `pc_OSBusClock`, `pc_OSCoreClock` | `u32` | pc_os.c | ✱ | port-as-is |
| `pc_image_base`, `pc_image_end` | `unsigned int` | pc_main.c | yes — seg2k0 | rewrite — ELF program headers → KOS's fixed `0x8C010000` load address |
| `g_gx` | `PCGXState` (≈3.2 MB, incl. the 65536×48 B vertex buffer) | pc_gx.c | ✱ | rewrite — shrinks to a PVR-native 32-byte vertex record |
| `pc_gx_draw_call_count`, `pc_gx_prim_draws[5]`, `pc_gx_merged_batches`, `pc_gx_culled_draws`, `pc_gx_flush_reason[18]`, `pc_gx_flush_time_us`, `pc_gx_texload_time_us` | diagnostics | pc_gx.c | ✱ | port-as-is (keep — this is how batching regressions get caught) |
| `pc_emu64_frame_cmds/_crashes/_noop_cmds/_tri_cmds/_vtx_cmds/_dl_cmds/_cull_visible/_cull_rejected` | `int` | pc_gx.c | yes (emu64 increments them) | port-as-is |
| `pc_gx_state_dedup`, `pc_gx_uniform_shadow` | `int` kill switches | pc_gx.c | ✱ | port-as-is — PLAN requires every optimization keep a kill switch |
| `pc_frame_counter` | `u32` | pc_vi.c | ✱ | port-as-is |
| `pc_save_loaded` | `int` | pc_m_card.c | yes | port-as-is |
| `g_pc_settings` | `PCSettings` | pc_settings.c | yes (`pc_settings_cull_limit_xz`) | rewrite/trim |
| `g_pc_running`, `g_pc_verbose`, `g_pc_frameskip_active`, `g_pc_fps_target`, `g_pc_render_w/h`, `g_pc_window_w/h`, `g_pc_widescreen_stretch`, `g_pc_zoom`, `g_pc_scale_mode`, `g_pc_no_framelimit`, `g_pc_time_override`, `g_pc_min_override`, `g_pc_sec_override` | platform globals | pc_main.c | mixed | rewrite/trim |
| `g_pc_window`, `g_pc_gl_context` | SDL handles | pc_main.c | ✱ | drop |
| `g_pc_keybindings`, `g_pc_typing_mode`, `g_pc_editor_active`, `g_pc_typing_queue`, `g_pc_menu_open`, `g_pc_model_viewer*` | PC-only feature state | pc_keybindings/typing/overlay/model_viewer | ✱ | drop for M1 |
| `s_tlut_first_word[16]` | `u16[]` | pc_gx_texture.c | ✱ | rewrite (TLUT cache key) |
| `pc_gx_tev_last_locs`, `pc_gx_tev_last_gens` | GLSL uniform cache | pc_gx_tev.c | ✱ | drop |

---
