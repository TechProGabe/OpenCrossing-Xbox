# Platform API — VI (video / frame pacing) and PAD (controller)

Every `VI*` and `PAD*` symbol, including `VIWaitForRetrace` — the frame
heartbeat where swap, pacing and dynamic-FPS all live.
Read when working on frame pacing, vsync, or maple input.
Split out of `kb/design-platform-api.md` §5. Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### VI — video interface & frame pacing

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `VIConfigurePan` | `void VIConfigurePan(u16 x_origin, u16 y_origin, u16 width, u16 height)` | pc_stubs.c | port-as-is |  |
| `VIInit` | `void VIInit(void)` | pc_vi.c | rewrite-for-KOS |  |
| `VIConfigure` | `void VIConfigure(void* rm)` | pc_vi.c | rewrite-for-KOS | Takes a GXRenderModeObj (the GXNtsc480IntDf[] blobs in pc_misc.c are 64 zero bytes). On DC this must at minimum select 640x480 NTSC vs PAL. |
| `VISetNextFrameBuffer` | `void VISetNextFrameBuffer(void* fb)` | pc_vi.c | rewrite-for-KOS | No-op on PC (renders straight to the back buffer). |
| `VIFlush` | `void VIFlush(void)` | pc_vi.c | rewrite-for-KOS |  |
| `pc_dynamic_fps_reset` ✱ | `void pc_dynamic_fps_reset(void)` | pc_vi.c | rewrite-for-KOS |  |
| `VIWaitForRetrace` | `void VIWaitForRetrace(void)` | pc_vi.c | rewrite-for-KOS | ★ The frame heartbeat: pumps events, does the swap, runs the frame-pacing wait, computes the dynamic-FPS target, and calls pc_gx_begin_frame(). Everything about pacing lives here. On DC it becomes pvr_scene_finish()/vid_waitvbl plus the same pacing logic; PLAN targets 30 fps. |
| `VIGetRetraceCount` | `u32 VIGetRetraceCount(void)` | pc_vi.c | rewrite-for-KOS | Monotonic frame counter the game uses for timing. |
| `VISetBlack` | `void VISetBlack(BOOL black)` | pc_vi.c | rewrite-for-KOS |  |
| `VIGetTvFormat` | `u32 VIGetTvFormat(void)` | pc_vi.c | rewrite-for-KOS |  |
| `VIGetDTVStatus` | `u32 VIGetDTVStatus(void)` | pc_vi.c | rewrite-for-KOS |  |
| `VISetPreRetraceCallback` | `void* VISetPreRetraceCallback(void* cb)` | pc_vi.c | rewrite-for-KOS |  |
| `VISetPostRetraceCallback` | `void* VISetPostRetraceCallback(void* cb)` | pc_vi.c | rewrite-for-KOS |  |
| `VIGetCurrentLine` | `u32 VIGetCurrentLine(void)` | pc_vi.c | rewrite-for-KOS |  |
| `VISetNextXFB` ✱ | `void VISetNextXFB(void* xfb)` | pc_vi.c | rewrite-for-KOS |  |

### PAD — controller

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_pad_dpad_as_stick_active` ✱ | `int pc_pad_dpad_as_stick_active(void)` | pc_pad.c | rewrite-for-KOS |  |
| `PADInit` | `BOOL PADInit(void)` | pc_pad.c | rewrite-for-KOS |  |
| `PADRead` | `u32 PADRead(PADStatus* status)` | pc_pad.c | rewrite-for-KOS | Fills PADStatus[4]; then JUTGamePad calls PADClamp() (compiled from src/static/dolphin/pad/Padclamp.c — the ONE Dolphin SDK source the PC build keeps). DC maple pad has no C-stick/Z; see PLAN §7. |
| `PADControlMotor` | `void PADControlMotor(s32 chan, u32 command)` | pc_pad.c | rewrite-for-KOS |  |
| `PADControlAllMotors` | `void PADControlAllMotors(const u32* commands)` | pc_pad.c | rewrite-for-KOS |  |
| `PADCleanup` ✱ | `void PADCleanup(void)` | pc_pad.c | rewrite-for-KOS |  |
| `PADReset` | `BOOL PADReset(u32 mask)` | pc_pad.c | rewrite-for-KOS |  |
| `PADRecalibrate` | `BOOL PADRecalibrate(u32 mask)` | pc_pad.c | rewrite-for-KOS |  |
| `PADSync` | `BOOL PADSync(void)` | pc_pad.c | rewrite-for-KOS |  |
| `PADSetSpec` | `void PADSetSpec(u32 spec)` | pc_pad.c | rewrite-for-KOS |  |
| `PADSetAnalogMode` | `void PADSetAnalogMode(u32 mode)` | pc_pad.c | rewrite-for-KOS |  |
| `PADGetType` | `BOOL PADGetType(s32 chan, u32* type)` | pc_pad.c | rewrite-for-KOS |  |
| `pc_pad_get_controller` ✱ | `SDL_GameController* pc_pad_get_controller(void)` | pc_pad.c | rewrite-for-KOS |  |
