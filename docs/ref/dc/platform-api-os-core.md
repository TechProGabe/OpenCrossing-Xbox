# Platform API — OS core: arena, time, cache, address translation, threads

The load-bearing OS semantics (`+0x28` memory-size word, SH-4 cache ops, tick
conventions, the single-threaded model) plus the `OS*` symbol tables for arena,
time, cache, address translation, threads, init, diagnostics and reset.
Read before writing `dc/src/dc_os.c` or anything that touches the heap or clock.
Split out of `kb/design-platform-api.md` (§3.1–3.3, §3.7, §5). Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

## 3. Semantics that will bite (read before writing any `dc/` code)

### 3.1 The arena and the `+ 0x28` word

`pc_os.c:283` writes `*(u32*)(arena_memory + 0x28) = PC_MAIN_MEMORY_SIZE;`.

Why: on real hardware the GameCube boot info block lives at physical address
0, and offset `0x28` holds the console's memory size. `JKRHeap::initArena`
(`src/static/JSystem/JKernel/JKRHeap.cpp:64-69`) does exactly this:

```c
u8* start = (u8*)OSPhysicalToCached(0);
mCodeStart   = start;
mCodeEnd     = (u8*)arenaLo;
mUserRamStart= (u8*)arenaLo;
mUserRamEnd  = (u8*)arenaHi;
mMemorySize  = *(u32*)((start + 0x28));      // ← the word
```

The PC port makes `OSPhysicalToCached(p)` return `arena_memory + p`, so
"physical 0" *is* the base of the mmap'd arena, and the port has to
manufacture the 0x28 word there itself. Every `JKRHeap` in the game inherits
`mMemorySize` from it; `JKRHeap::getTotalFreeSize`/heap reporting and the
`JUTProcBar` heap bar read it.

**On Dreamcast:** KOS main RAM starts at `0x8C000000` and the first ~64 KB
belong to KOS/the IP.BIN loader. Two options, decide once and write it down:

- **(A) Keep the PC trick.** `OSPhysicalToCached(p) == dc_arena_base + p`,
  and reserve the first 0x2C bytes of the arena for the fake boot block. Zero
  KOS interaction, identical to the proven base port. Costs 44 bytes.
- **(B) Real mapping.** `OSPhysicalToCached(p) == 0x8C000000 + p`, then
  `start+0x28` is inside KOS's low memory and must not be written.

**(A) is strongly recommended** — it also keeps `OSCachedToPhysical` a simple
subtraction, which emu64's pointer plumbing depends on.

Related: `adjustOSArena()` in `boot.c` `bzero`s the *entire* `arenaLo..arenaHi`
span at boot. On a 9 MB DC arena that is a ~9 MB memset every cold boot —
measurable but acceptable; on a 24 MB arena it is not.

Also related: `JW_Init` computes `SystemHeapSize = arenaHi - arenaLo - 0xD0`,
so the DC arena size *is* the system heap size. There is no separate knob.

### 3.2 Cache maintenance — no-ops on x86/ARM, REAL on SH-4 ★

Every one of these is `{ (void)addr; (void)len; }` in `pc_os.c` today. On
SH-4 with PVR store-queue DMA and AICA DMA they are load-bearing. Call-site
counts are over compiled (non-excluded) `src/`:

| symbol | call sites | where | SH-4 mapping |
|---|---|---|---|
| `DCStoreRangeNoSync` | **25** | jaudio (`dspbuf`, `cpubuf`, `aramcall`), JKRAram | `dcache_flush_range` (writeback) |
| `DCFlushRange` | **12** | `m_card.c`, `dspinterface.c`, `fxinterface.c`, `JUTDirectPrint` | `dcache_flush_range` (writeback + inval) |
| `DCStoreRange` | **10** | **`emu64.c:965` (decoded texture) and `emu64.c:1004` (TLUT)**, `aictrl.c` DAC buffers, `cpubuf`, `dspbuf` | writeback |
| `DCFlushRangeNoSync` | **10** | jaudio | writeback |
| `DCInvalidateRange` | **9** | `JKRAramPiece.cpp:97` (DMA completion), `dspinterface.c`, `JFWDisplay.cpp` XFB | `dcache_inval_range` |
| `osWritebackDCache` | **8** | libultra-flavoured game code | writeback |
| `DCTouchRange` | 6 | prefetch hint | safe as a no-op |
| `DCZeroRange` | 1 | `memset` is a correct (slower) substitute | — |
| `ICInvalidateRange`, `ICFlashInvalidate`, `LCEnable`, `LCDisable` | **0** | — | drop |

Two SH-4 traps:

- **Invalidate discards dirty lines.** `dcache_inval_range` on a range that
  is not 32-byte aligned will drop neighbouring dirty data. GC's
  `DCInvalidateRange` had the same constraint and the game's call sites are
  believed aligned, but this is exactly the class of bug that shows up as
  "audio buffer has one wrong sample every N frames". Assert alignment in
  the DC implementation during bring-up.
- **`emu64.c:965/1004` are the texture path.** Those two `DCStoreRange`
  calls are the game telling the platform "the texture bytes are ready".
  They are the correct hook for "upload to VRAM"; treating them as no-ops
  and instead uploading on `GXLoadTexObj` also works, but pick one.

`OSCachedToUncached`/`OSUncachedToCached` are identity functions on PC. On
SH-4 they are the P1↔P2 window flip (`^ 0x20000000`) and are the *other*
correct way to keep DMA-visible buffers coherent.

### 3.3 `OSGetTime` / `OSGetTick` conventions

- GC tick rate = bus clock / 4 = 162 MHz / 4 = **40.5 MHz**
  (`GC_TIMER_CLOCK` in `pc_platform.h`). Every timeout, animation delta and
  RTC computation in the game is denominated in these ticks. **Do not change
  the rate** — change the source only.
- `OSGetTime()` returns absolute ticks since the **GameCube epoch,
  2000-01-01 00:00:00 local time** (`GC_UNIX_EPOCH_DIFF = 946684800`), with
  the host timezone offset folded in at `OSInit()` time. Animal Crossing's
  entire calendar/event system reads this.
- `OSGetTick()` / `osGetCount()` return the **low 32 bits**. The game uses
  `OSDiffTick(a,b) = (s32)a - (s32)b`, which wraps every ~106 s at 40.5 MHz.
  Anything that measures longer than that is already broken on GC too.
- `pc_os.c` splits whole seconds from the remainder before scaling
  (`(diff/freq)*CLK + (diff%freq)*CLK/freq`) because the naive form
  overflows u64 after ~455 s at a 1 GHz counter. Keep the split.
- **DC source:** the Dreamcast has a battery-backed RTC. KOS exposes
  `rtc_unix_secs()`; the DC RTC epoch is 1950-01-01, so KOS's conversion plus
  the GC-epoch offset gives wall-clock time. A monotonic high-resolution
  counter (KOS `timer_ns_gettime64` / TMU) provides the tick delta. *The
  exact KOS API names have not been verified in this pass — confirm against
  the KOS tree during M0.*
- `boot.c` calls `osGetTime()` **before** `OSInit()`. Whatever provides the
  monotonic base must be safe to call before init.

### 3.7 Threads: the port is single-threaded, and the game knows

`osCreateThread2`/`osStartThread` deliberately **do not start anything**;
`OSCreateThread` returns FALSE; `OSDisableInterrupts` returns 0.
`OSSendMessage` is non-blocking and returns FALSE when full (real Dolphin
blocks when `flags == OS_MESSAGE_BLOCK`). The one real thread on PC is the
SDL audio producer, created by `pc_audio_start_producer_thread()` and driving
`pc_audio_process_frame()` — which lives in
`src/static/jaudio_NES/internal/audiothread.c` under `#ifdef TARGET_PC`.

Two consequences for DC:

- `OSGetCurrentThread()` must return a pointer whose `stackBase`/`stackEnd`
  fields are readable — `boot.c` dereferences them at startup. PC returns a
  512-byte zeroed dummy.
- If DC introduces a real KOS audio thread, `OSDisableInterrupts` /
  `OSInitMessageQueue` / `OSSendMessage` stop being safe as stubs, and the
  `OSMessageQueue` struct layout (16 bytes of thread queues, then
  `msgArray`/`msgCount`/`firstIndex`/`usedCount`) must be honoured because
  the game allocates those structs itself.

### Entry point & platform lifecycle

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_crash_get_data_addr` | `unsigned int pc_crash_get_data_addr(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_crash_protection_init` | `void pc_crash_protection_init(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_crash_set_jmpbuf` | `void pc_crash_set_jmpbuf(jmp_buf* buf)` | pc_main.c | rewrite-for-KOS |  |
| `pc_crash_get_jmpbuf` | `jmp_buf* pc_crash_get_jmpbuf(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_crash_get_addr` | `unsigned int pc_crash_get_addr(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_platform_init` ✱ | `void pc_platform_init(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_platform_shutdown` | `void pc_platform_shutdown(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_platform_update_window_size` ✱ | `void pc_platform_update_window_size(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_platform_swap_buffers` ✱ | `void pc_platform_swap_buffers(void)` | pc_main.c | rewrite-for-KOS |  |
| `pc_platform_poll_events` ✱ | `int pc_platform_poll_events(void)` | pc_main.c | rewrite-for-KOS |  |
| `main` | `int main(int argc, char* argv[])` | pc_main.c | rewrite-for-KOS |  |

### OS — arena / JKRHeap contract

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `OSGetArenaLo` | `void* OSGetArenaLo(void)` | pc_os.c | rewrite-for-KOS | JKRHeap::initArena reads lo/hi; must return the DC static arena. Value must be >= 32B-aligned. |
| `OSGetArenaHi` | `void* OSGetArenaHi(void)` | pc_os.c | rewrite-for-KOS | boot.c adjustOSArena() bzero()s the whole lo..hi span at boot — sizing this is the RAM ledger. |
| `OSSetArenaLo` | `void OSSetArenaLo(void* lo)` | pc_os.c | rewrite-for-KOS |  |
| `OSSetArenaHi` | `void OSSetArenaHi(void* hi)` | pc_os.c | rewrite-for-KOS |  |
| `OSInitAlloc` | `void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps)` | pc_os.c | port-as-is | Pure arithmetic (skip maxHeaps*24 descriptors, round up 32B). Compiles unchanged. |
| `OSAllocFromHeap` | `void* OSAllocFromHeap(int heap, u32 size)` | pc_os.c | rewrite-for-KOS | PC cheats with malloc(). On DC must come out of the arena or KOS malloc; __OSCurrHeap is ignored. |
| `OSFreeToHeap` | `void OSFreeToHeap(int heap, void* ptr)` | pc_os.c | rewrite-for-KOS |  |
| `OSCreateHeap` | `int OSCreateHeap(void* lo, void* hi)` | pc_os.c | rewrite-for-KOS |  |
| `OSSetCurrentHeap` | `int OSSetCurrentHeap(int heap)` | pc_os.c | rewrite-for-KOS |  |
| `OSGetPhysicalMemSize` | `u32 OSGetPhysicalMemSize(void)` | pc_os.c | rewrite-for-KOS | Returns PC_MAIN_MEMORY_SIZE (24 MB). Must return the DC arena size, and must agree with *(u32*)(OSPhysicalToCached(0)+0x28). |

### OS — time & calendar

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `osGetTime` | `s64 osGetTime(void)` | pc_os.c | rewrite-for-KOS | GC tick = bus clock/4 = 40.5 MHz. PC derives from SDL perf counter with a split-seconds trick to avoid u64 overflow (~455 s). DC: use KOS timer_ns_gettime64()/perf counter and keep the same split; the game's clock depends on ticks-per-second being exactly GC_TIMER_CLOCK. |
| `OSGetTime` | `s64 OSGetTime(void)` | pc_os.c | rewrite-for-KOS | Absolute ticks since GC epoch = 2000-01-01 (946684800 s after Unix epoch), local-time-adjusted. Dreamcast has a battery-backed RTC readable via KOS `rtc_unix_secs()` — that is the natural source; DC RTC epoch is 1950-01-01, so a second offset conversion is needed. |
| `__OSGetSystemTime` | `s64 __OSGetSystemTime(void)` | pc_os.c | rewrite-for-KOS |  |
| `osGetCount` | `u32 osGetCount(void)` | pc_os.c | rewrite-for-KOS |  |
| `OSGetTick` | `u32 OSGetTick(void)` | pc_os.c | rewrite-for-KOS | u32 truncation of the 64-bit tick. Game does OSDiffTick((s32)a-(s32)b) — wraps every ~106 s. Must keep the SAME tick rate or every timeout in the game changes. |
| `OSTicksToCalendarTime` | `void OSTicksToCalendarTime(s64 ticks, void* td_ptr)` | pc_os.c | port-as-is | Pure integer math ported from OSTime.c; BIAS=0xB2575 days. Compiles unchanged. |
| `OSCalendarTimeToTicks` | `s64 OSCalendarTimeToTicks(void* td_ptr)` | pc_os.c | port-as-is |  |

### OS — cache maintenance (REAL on SH-4)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `DCFlushRange` | `void DCFlushRange(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | ★ REAL on SH-4. 12 call sites in compiled game code (m_card.c, jaudio dspinterface/fxinterface, JUTDirectPrint). Must become `dcache_flush_range()` (KOS) — writeback+invalidate — wherever the buffer is then touched by PVR/AICA/DMA. |
| `DCTouchRange` | `void DCTouchRange(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | 6 call sites; prefetch hint, safe to keep as a no-op. |
| `DCStoreRange` | `void DCStoreRange(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | ★ REAL on SH-4. 10 call sites (emu64.c texture/TLUT upload, aictrl DAC buffers, cpubuf/dspbuf). emu64.c:965/1004 store decoded texture+TLUT ranges — exactly the buffers the PVR texture uploader will read. |
| `DCInvalidateRange` | `void DCInvalidateRange(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | ★ REAL on SH-4. 9 call sites (JKRAramPiece DMA completion, dspinterface, JFWDisplay XFB). SH-4 has `dcache_inval_range()`; note SH-4 invalidate is line-granular and will DISCARD dirty lines — the address/length must be 32-byte aligned or data is lost. |
| `DCFlushRangeNoSync` | `void DCFlushRangeNoSync(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | ★ 10 call sites. Same as DCFlushRange minus the trailing sync; on SH-4 just alias to the flush. |
| `DCStoreRangeNoSync` | `void DCStoreRangeNoSync(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | ★ 25 call sites — the most-used cache op in the codebase. All in jaudio + JKRAram paths. |
| `DCZeroRange` | `void DCZeroRange(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | PC does memset. On SH-4 the real op allocates cache lines without reading memory; memset is a correct (slower) substitute. |
| `ICFlashInvalidate` | `void ICFlashInvalidate(void)` | pc_os.c | rewrite-for-KOS |  |
| `ICInvalidateRange` | `void ICInvalidateRange(void* addr, u32 len)` | pc_os.c | rewrite-for-KOS | 0 call sites in compiled code. Keep as no-op. |
| `LCEnable` ✱ | `void LCEnable(void)` | pc_os.c | drop | GC locked-cache. 0 call sites. Drop. |
| `LCDisable` | `void LCDisable(void)` | pc_os.c | drop | GC locked-cache. 0 call sites. Drop. |
| `osWritebackDCache` | `void osWritebackDCache(void* vaddr, u32 nbytes)` | pc_stubs.c | rewrite-for-KOS | ★ REAL on SH-4. 8 call sites in compiled libultra-flavoured game code; currently a no-op. Same treatment as DCStoreRange. |

### OS — address translation

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `OSPhysicalToCached` | `void* OSPhysicalToCached(u32 paddr)` | pc_os.c | rewrite-for-KOS | ★ LANDMINE. PC maps phys 0 -> arena base, so JKRHeap's `start = OSPhysicalToCached(0)` and `*(u32*)(start+0x28)` land inside the arena. On DC the natural mapping is phys 0 -> 0x8C000000; then +0x28 is inside KOS's own low memory. Either keep the PC trick (phys 0 == arena base) or explicitly reserve/write the 0x28 word. |
| `OSPhysicalToUncached` | `void* OSPhysicalToUncached(u32 paddr)` | pc_os.c | rewrite-for-KOS |  |
| `OSCachedToPhysical` | `u32 OSCachedToPhysical(void* caddr)` | pc_os.c | rewrite-for-KOS | Inverse of the above; used by emu64/ARAM address plumbing. Keep the two consistent. |
| `OSUncachedToPhysical` | `u32 OSUncachedToPhysical(void* ucaddr)` | pc_os.c | rewrite-for-KOS |  |
| `OSCachedToUncached` | `void* OSCachedToUncached(void* caddr)` | pc_os.c | rewrite-for-KOS | Identity on PC. On SH-4 this is a REAL operation: P1 (0x8Cxxxxxx) -> P2 (0xACxxxxxx). Any buffer handed to PVR/AICA/DMA must use the P2 alias or be flushed. |
| `OSUncachedToCached` | `void* OSUncachedToCached(void* ucaddr)` | pc_os.c | rewrite-for-KOS | SH-4: clear bit 0x20000000. Real operation, not identity. |

### OS — threads, queues, sync

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `osCreateMesgQueue` | `void osCreateMesgQueue(void* mq, void* buf, int count)` | pc_os.c | port-as-is |  |
| `osSendMesg` | `int osSendMesg(void* mq, void* msg, int flags)` | pc_os.c | port-as-is |  |
| `osRecvMesg` | `int osRecvMesg(void* mq, void** msg, int flags)` | pc_os.c | port-as-is |  |
| `osCreateThread2` | `void osCreateThread2(void* thread, int id, void (*entry)(void*), void* arg, void* stack, int stack_size, int priority)` | pc_os.c | rewrite-for-KOS |  |
| `osStartThread` | `void osStartThread(void* thread)` | pc_os.c | rewrite-for-KOS |  |
| `osSetThreadPri` | `void osSetThreadPri(void* thread, int pri)` | pc_os.c | rewrite-for-KOS |  |
| `OSGetCurrentThread` | `void* OSGetCurrentThread(void)` | pc_os.c | rewrite-for-KOS | ★ boot.c dereferences the returned pointer's stackBase/stackEnd fields (OSThread layout) to paint the stack with 0xFD. Returning a bogus pointer crashes at boot. PC returns a 512-byte zeroed dummy. |
| `OSGetStackPointer` | `void* OSGetStackPointer(void)` | pc_os.c | rewrite-for-KOS | PC returns &a_static — only used relative to the above; any in-arena address works. |
| `OSDisableInterrupts` | `BOOL OSDisableInterrupts(void)` | pc_os.c | rewrite-for-KOS | PC returns 0 always. If DC uses real KOS threads for audio, these must become irq_disable()/irq_restore() or the jaudio queues race. |
| `OSEnableInterrupts` | `BOOL OSEnableInterrupts(void)` | pc_os.c | rewrite-for-KOS |  |
| `OSRestoreInterrupts` | `BOOL OSRestoreInterrupts(BOOL level)` | pc_os.c | rewrite-for-KOS |  |
| `msleep` | `void msleep(int ms)` | pc_os.c | rewrite-for-KOS | boot.c sleeps 2500 ms during sound_initial(); with a real DC boot this is a visible dead wait — candidate to shorten. |
| `OSInitMutex` | `void OSInitMutex(OSMutex* mutex)` | pc_os.c | rewrite-for-KOS |  |
| `OSLockMutex` | `void OSLockMutex(OSMutex* mutex)` | pc_os.c | rewrite-for-KOS |  |
| `OSUnlockMutex` | `void OSUnlockMutex(OSMutex* mutex)` | pc_os.c | rewrite-for-KOS |  |
| `OSTryLockMutex` | `BOOL OSTryLockMutex(OSMutex* mutex)` | pc_os.c | rewrite-for-KOS |  |
| `OSInitMessageQueue` | `void OSInitMessageQueue(void* queue, void** msgArray, int msgCount)` | pc_os.c | rewrite-for-KOS | ★ Layout-critical: the struct must match dolphin/os/OSMessage.h exactly (16 bytes of thread queues, then msgArray/msgCount/firstIndex/usedCount). The game allocates these structs itself. |
| `OSSendMessage` | `BOOL OSSendMessage(void* queue, void* msg, int flags)` | pc_os.c | rewrite-for-KOS | PC version is NON-BLOCKING and returns FALSE when full (real Dolphin blocks when flags==OS_MESSAGE_BLOCK). jaudio depends on the non-blocking behavior in the TARGET_PC path. |
| `OSReceiveMessage` | `BOOL OSReceiveMessage(void* queue, void** msgPtr, int flags)` | pc_os.c | rewrite-for-KOS |  |
| `OSJamMessage` | `BOOL OSJamMessage(void* queue, void* msg, int flags)` | pc_os.c | rewrite-for-KOS |  |
| `OSCreateThread` | `BOOL OSCreateThread(void* thread, void* (*func)(void*), void* param, void* stack, u32 stackSize, OSPriority priority, u16 attr)` | pc_stubs.c | rewrite-for-KOS | ★ Called by JKRThread/JKRDecomp and by jaudio's GC path (which is #ifdef'd out for TARGET_PC). PC returns FALSE and never runs the thread. If DC uses real KOS threads for audio, this stub has to become real. |
| `OSCancelThread` | `void OSCancelThread(void* thread)` | pc_stubs.c | rewrite-for-KOS |  |
| `OSDetachThread` | `void OSDetachThread(void* thread)` | pc_stubs.c | rewrite-for-KOS |  |
| `OSResumeThread` | `s32 OSResumeThread(void* thread)` | pc_stubs.c | rewrite-for-KOS |  |
| `OSSuspendThread` | `s32 OSSuspendThread(void* thread)` | pc_stubs.c | rewrite-for-KOS | PC no-op; JKRDecomp assumes a suspended decompression thread it can resume. |
| `OSIsThreadTerminated` | `BOOL OSIsThreadTerminated(void* thread)` | pc_stubs.c | rewrite-for-KOS |  |
| `OSEnableScheduler` | `s32 OSEnableScheduler(void)` | pc_stubs.c | rewrite-for-KOS |  |
| `OSYieldThread` | `void OSYieldThread(void)` | pc_stubs.c | rewrite-for-KOS |  |
| `OSCheckActiveThreads` | `long OSCheckActiveThreads(void)` | pc_stubs.c | rewrite-for-KOS |  |
| `OSFillFPUContext` | `void OSFillFPUContext(void* context)` | pc_stubs.c | rewrite-for-KOS |  |

### OS — init / misc

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `OSInitFastCast` | `void OSInitFastCast(void)` | pc_misc.c | port-as-is |  |
| `__OSSetupFPU` ✱ | `void __OSSetupFPU(void)` | pc_misc.c | port-as-is |  |
| `__sync` | `void __sync(void)` | pc_misc.c | port-as-is |  |
| `__isync` ✱ | `void __isync(void)` | pc_misc.c | port-as-is |  |
| `OSGetSaveRegion` | `void OSGetSaveRegion(void** start, void** end)` | pc_os.c | port-as-is |  |
| `OSInit` | `void OSInit(void)` | pc_os.c | rewrite-for-KOS |  |
| `OSInitAlarm` | `void OSInitAlarm(void)` | pc_os.c | port-as-is |  |
| `__osInitialize_common` | `void __osInitialize_common(void)` | pc_os.c | port-as-is |  |
| `__OSPSInit` | `void __OSPSInit(void)` | pc_os.c | port-as-is |  |
| `__OSFPRInit` | `void __OSFPRInit(void)` | pc_os.c | port-as-is |  |
| `__OSCacheInit` | `void __OSCacheInit(void)` | pc_os.c | port-as-is |  |
| `OSGetConsoleType` | `u32 OSGetConsoleType(void)` | pc_os.c | port-as-is |  |
| `OSSetAlarm` | `void OSSetAlarm(OSAlarm* alarm, s64 tick, void (*callback)(OSAlarm*, void*))` | pc_os.c | port-as-is |  |
| `OSSetPeriodicAlarm` | `void OSSetPeriodicAlarm(OSAlarm* alarm, s64 start, s64 period, void (*callback)(OSAlarm*, void*))` | pc_os.c | port-as-is |  |
| `OSCancelAlarm` | `void OSCancelAlarm(OSAlarm* alarm)` | pc_os.c | port-as-is |  |
| `OSCreateAlarm` | `void OSCreateAlarm(OSAlarm* alarm)` | pc_os.c | port-as-is |  |
| `ReconfigBATs` | `void ReconfigBATs(void)` | pc_os.c | port-as-is |  |
| `OSSetStringTable` | `void OSSetStringTable(void* table)` | pc_os.c | port-as-is |  |
| `__OSLockSramEx` | `OSSramEx* __OSLockSramEx(void)` | pc_os.c | port-as-is |  |
| `__OSUnlockSramEx` | `void __OSUnlockSramEx(BOOL commit)` | pc_os.c | port-as-is |  |
| `OSClearContext` | `void OSClearContext(OSContext* ctx)` | pc_os.c | port-as-is |  |
| `__OSSetExceptionHandler` | `void* __OSSetExceptionHandler(u8 type, void* handler)` | pc_os.c | rewrite-for-KOS | ★ Use this seam to install the SH-4 unaligned-access / TLB exception handler that PLAN §3.2 needs for -O2 triage. |

### OS — diagnostics

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `__OSUnhandledException` | `void __OSUnhandledException(u8 type, void* ctx, u32 dsisr, u32 dar)` | pc_misc.c | rewrite-for-KOS | PC prints and returns. DC should dump PC + faulting address, matching the base port's SIGBUS recovery pattern. |
| `OSPanic` | `void OSPanic(const char* file, int line, const char* msg, ...)` | pc_os.c | rewrite-for-KOS |  |
| `OSReport` | `void OSReport(const char* fmt, ...)` | pc_os.c | rewrite-for-KOS | Every OSReport goes through here; gated on g_pc_verbose. On DC route to KOS dbgio (dcload/serial) and keep it OFF by default — serial at 57600 baud will destroy frame time. |
| `OSVReport` | `void OSVReport(const char* fmt, va_list list)` | pc_os.c | rewrite-for-KOS |  |
| `OSReportDisable` | `void OSReportDisable(void)` | pc_os.c | rewrite-for-KOS |  |

### OS — reset / shutdown

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `OSRegisterResetFunction` | `void OSRegisterResetFunction(OSResetFunctionInfo* info)` | pc_misc.c | drop |  |
| `OSUnregisterResetFunction` | `void OSUnregisterResetFunction(OSResetFunctionInfo* info)` | pc_misc.c | drop |  |
| `OSGetResetSwitchState` | `BOOL OSGetResetSwitchState(void)` | pc_os.c | rewrite-for-KOS |  |
| `OSResetSystem` | `void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu)` | pc_os.c | rewrite-for-KOS | PC sets g_pc_running=0. DC: arch_exit()/arch_reboot(). |
| `OSGetResetCode` | `u32 OSGetResetCode(void)` | pc_os.c | rewrite-for-KOS |  |
| `OSChangeBootMode` | `void OSChangeBootMode(u32 mode)` | pc_os.c | rewrite-for-KOS |  |
| `osShutdownStart` | `void osShutdownStart(int type)` | pc_os.c | rewrite-for-KOS |  |
