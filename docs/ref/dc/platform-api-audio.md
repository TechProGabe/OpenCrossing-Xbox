# Platform API — AI / DSP audio interface

`AIInitDMA` is the audio handoff point: jaudio hands over a finished 32 kHz s16
stereo block and everything upstream is decompiled game code. DSP mailbox calls
are stubs because rspsim synthesises in software.
Read before writing `dc/src/dc_audio.c` / the `snd_stream` backend.
Split out of `kb/design-platform-api.md` §5. Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### AI — audio interface

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `AIInit` | `void AIInit(u8* stack)` | pc_audio.c | rewrite-for-KOS | Opens the output device. On DC: KOS snd_stream. |
| `AIInitDMA` | `void AIInitDMA(u32 addr, u32 size)` | pc_audio.c | rewrite-for-KOS | ★ THE AUDIO HANDOFF POINT. jaudio calls it with (addr,size) of a finished 32 kHz s16 stereo block; the PC layer copies into a 32768-sample SPSC ring drained by the audio callback. Everything upstream (rspsim software DSP) is decompiled game code that keeps working. |
| `AIStartDMA` | `void AIStartDMA(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIStopDMA` | `void AIStopDMA(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetDMAStartAddr` | `u32 AIGetDMAStartAddr(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetDMALength` | `u16 AIGetDMALength(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetStreamTrigger` | `u32 AIGetStreamTrigger(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetStreamSampleCount` | `u32 AIGetStreamSampleCount(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AISetStreamPlayState` | `void AISetStreamPlayState(u32 state)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetStreamPlayState` | `u32 AIGetStreamPlayState(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AISetStreamSampleRate` | `void AISetStreamSampleRate(u32 rate)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetStreamSampleRate` | `u32 AIGetStreamSampleRate(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AISetStreamVolLeft` | `void AISetStreamVolLeft(u8 vol)` | pc_audio.c | rewrite-for-KOS |  |
| `AISetStreamVolRight` | `void AISetStreamVolRight(u8 vol)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetStreamVolLeft` | `u8 AIGetStreamVolLeft(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIGetStreamVolRight` | `u8 AIGetStreamVolRight(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIResetStreamSampleCount` | `void AIResetStreamSampleCount(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AISetDSPSampleRate` | `void AISetDSPSampleRate(u32 rate)` | pc_audio.c | rewrite-for-KOS | Game runs at 32 kHz; PLAN §3.4 stage A drops to 22.05 kHz on DC. |
| `AIGetDSPSampleRate` | `u32 AIGetDSPSampleRate(void)` | pc_audio.c | rewrite-for-KOS |  |
| `AIRegisterDMACallback` | `void* AIRegisterDMACallback(void* callback)` | pc_audio.c | rewrite-for-KOS |  |

### DSP — mailbox (rspsim does the work)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `DSPInit` | `void DSPInit(void)` | pc_audio.c | port-as-is | All DSP mailbox functions are stubs — rspsim (src/static/jaudio_NES) does the synthesis in software. Compile unchanged. |
| `DSPCheckMailToDSP` | `BOOL DSPCheckMailToDSP(void)` | pc_audio.c | port-as-is |  |
| `DSPCheckMailFromDSP` | `BOOL DSPCheckMailFromDSP(void)` | pc_audio.c | port-as-is |  |
| `DSPReadMailFromDSP` | `u32 DSPReadMailFromDSP(void)` | pc_audio.c | port-as-is |  |
| `DSPSendMailToDSP` | `void DSPSendMailToDSP(u32 mail)` | pc_audio.c | port-as-is |  |
| `DSPAssertInt` | `void DSPAssertInt(void)` | pc_audio.c | port-as-is |  |
| `DSPAddTask` | `void* DSPAddTask(void* task)` | pc_audio.c | port-as-is | Returns the task pointer; the real DSP is never used. |

### Audio — platform-internal

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_audio_start_producer_thread` | `void pc_audio_start_producer_thread(void)` | pc_audio.c | rewrite-for-KOS |  |
| `pc_audio_update_volumes` | `void pc_audio_update_volumes(void)` | pc_audio.c | rewrite-for-KOS |  |
| `pc_audio_get_buffer_fill` ✱ | `int pc_audio_get_buffer_fill(void)` | pc_audio.c | rewrite-for-KOS |  |
| `pc_audio_is_active` ✱ | `int pc_audio_is_active(void)` | pc_audio.c | rewrite-for-KOS |  |
| `pc_audio_shutdown` ✱ | `void pc_audio_shutdown(void)` | pc_audio.c | rewrite-for-KOS |  |
