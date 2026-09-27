# Platform API — stub surfaces: PPC, EXI/SI/DB, libultra, GBA, Famicom, libc

The symbols that exist only to satisfy the linker: REL loader, libc64 malloc
shims, N64 trig, PPC intrinsics, EXI/SI/debugger, libc, libultra, GBA link,
Famicom (NES) stubs, JSystem C++ vtable fills, glibc compat, console settings.
Read when a link fails on an unresolved symbol that is not a real subsystem.
Split out of `kb/design-platform-api.md` §5. Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### OS — REL module loader

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `OSLink` | `BOOL OSLink(void* info, void* bss)` | pc_misc.c | stub-and-log | Returns TRUE without doing anything — the PC build links foresta.rel's code in statically instead of relocating it. Same approach needed for DC (or a real REL loader); 1 call site in boot.c. |
| `OSUnlink` | `BOOL OSUnlink(void* info)` | pc_misc.c | stub-and-log |  |

### libc64 malloc-arena shims

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `MallocInit` | `void MallocInit(void* base, unsigned long size)` | pc_misc.c | port-as-is | libc64/malloc.c is excluded from the build; these wrap the game's arena bookkeeping. jsyswrap.cpp calls MallocInit(gameheap_base, gameheap_len) where gameheap = systemHeap free - 0x10000. |
| `MallocCleanup` | `void MallocCleanup(void)` | pc_misc.c | port-as-is |  |
| `MallocIsInitalized` | `int MallocIsInitalized(void)` | pc_misc.c | port-as-is |  |
| `GetFreeArena` | `void GetFreeArena(unsigned long* max, unsigned long* free_size, unsigned long* alloc)` | pc_misc.c | port-as-is | Reports the malloc arena; used by DisplayArena in the fault handler. |
| `DisplayArena` | `void DisplayArena(void)` | pc_misc.c | port-as-is |  |
| `CheckArena` | `int CheckArena(void)` | pc_misc.c | port-as-is |  |

### N64 fixed-point trig

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `sins` | `short sins(unsigned short angle)` | pc_misc.c | port-as-is | N64 fixed-point sin over a u16 angle. Uses double sin() per call — on SH-4 replace with a 16-bit LUT. |
| `coss` | `short coss(unsigned short angle)` | pc_misc.c | port-as-is |  |

### PPC intrinsic shims

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `PPCMfmsr` | `u32 PPCMfmsr(void)` | pc_misc.c | stub-and-log | 2 live call sites (JUTException.cpp masks interrupts via MSR). Needs an SH-4 SR equivalent or the exception path changes behavior. |
| `PPCMtmsr` | `void PPCMtmsr(u32 msr)` | pc_misc.c | stub-and-log |  |
| `PPCMfhid0` | `u32 PPCMfhid0(void)` | pc_misc.c | drop |  |
| `PPCMthid0` | `void PPCMthid0(u32 hid0)` | pc_misc.c | drop |  |
| `PPCMfhid2` | `u32 PPCMfhid2(void)` | pc_misc.c | drop |  |
| `PPCMthid2` | `void PPCMthid2(u32 hid2)` | pc_misc.c | drop |  |
| `PPCHalt` | `void PPCHalt(void)` | pc_misc.c | drop |  |
| `PPCMfl2cr` | `u32 PPCMfl2cr(void)` | pc_misc.c | drop |  |
| `PPCMtl2cr` | `void PPCMtl2cr(u32 val)` | pc_misc.c | drop |  |
| `PPCMtdec` | `void PPCMtdec(u32 val)` | pc_misc.c | drop |  |
| `PPCSync` | `void PPCSync(void)` | pc_misc.c | stub-and-log | 2 live call sites (emu64.c:5912, dspdriver.c:373) — memory barriers. On SH-4 these should be real (`__sync_synchronize()` / OCBWB), not no-ops, if any DMA is in flight. |
| `PPCMtmmcr0` ✱ | `void PPCMtmmcr0(u32 val)` | pc_misc.c | drop |  |
| `PPCMtmmcr1` ✱ | `void PPCMtmmcr1(u32 val)` | pc_misc.c | drop |  |
| `PPCMtpmc1` ✱ | `void PPCMtpmc1(u32 val)` | pc_misc.c | drop |  |
| `PPCMtpmc2` ✱ | `void PPCMtpmc2(u32 val)` | pc_misc.c | drop |  |
| `PPCMtpmc3` ✱ | `void PPCMtpmc3(u32 val)` | pc_misc.c | drop |  |
| `PPCMtpmc4` ✱ | `void PPCMtpmc4(u32 val)` | pc_misc.c | drop |  |
| `PPCMfpmc1` ✱ | `u32 PPCMfpmc1(void)` | pc_misc.c | drop |  |
| `PPCMfpmc2` ✱ | `u32 PPCMfpmc2(void)` | pc_misc.c | drop |  |
| `PPCMfpmc3` ✱ | `u32 PPCMfpmc3(void)` | pc_misc.c | drop |  |
| `PPCMfpmc4` ✱ | `u32 PPCMfpmc4(void)` | pc_misc.c | drop |  |

### EXI / SI / debugger shims

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `EXILock` | `BOOL EXILock(s32 chan, u32 dev, void* unlockCallback)` | pc_misc.c | drop |  |
| `EXIUnlock` | `BOOL EXIUnlock(s32 chan)` | pc_misc.c | drop |  |
| `EXISelect` | `BOOL EXISelect(s32 chan, u32 dev, u32 freq)` | pc_misc.c | drop |  |
| `EXIDeselect` | `BOOL EXIDeselect(s32 chan)` | pc_misc.c | drop |  |
| `EXIImm` | `BOOL EXIImm(s32 chan, void* data, s32 len, u32 type, void* callback)` | pc_misc.c | drop |  |
| `EXIDma` | `BOOL EXIDma(s32 chan, void* data, s32 len, u32 type, void* callback)` | pc_misc.c | drop |  |
| `EXISync` | `BOOL EXISync(s32 chan)` | pc_misc.c | drop |  |
| `EXIInit` | `void EXIInit(void)` | pc_misc.c | drop |  |
| `EXIAttach` | `BOOL EXIAttach(s32 chan, void* extCallback)` | pc_misc.c | drop |  |
| `EXIDetach` | `BOOL EXIDetach(s32 chan)` | pc_misc.c | drop |  |
| `EXIProbe` | `BOOL EXIProbe(s32 chan)` | pc_misc.c | drop |  |
| `EXIProbeEx` | `BOOL EXIProbeEx(s32 chan)` | pc_misc.c | drop |  |
| `EXIGetID` | `s32 EXIGetID(s32 chan, u32 dev, u32* id)` | pc_misc.c | drop |  |
| `EXISetExiCallback` | `void EXISetExiCallback(s32 chan, void* cb)` | pc_misc.c | drop |  |
| `SIInit` | `void SIInit(void)` | pc_misc.c | drop |  |
| `SIGetType` | `u32 SIGetType(s32 chan)` | pc_misc.c | drop |  |
| `SIGetTypeAsync` | `u32 SIGetTypeAsync(s32 chan, void* callback)` | pc_misc.c | drop |  |
| `SITransfer` | `BOOL SITransfer(s32 chan, void* output, u32 outputLen, void* input, u32 inputLen, void* callback, s64 time)` | pc_misc.c | drop |  |
| `SISetXY` | `u32 SISetXY(u32 x, u32 y)` | pc_misc.c | drop |  |
| `SIEnablePolling` | `u32 SIEnablePolling(u32 poll)` | pc_misc.c | drop |  |
| `SIDisablePolling` | `u32 SIDisablePolling(u32 poll)` | pc_misc.c | drop |  |
| `SISetSamplingRate` | `void SISetSamplingRate(u32 rate)` | pc_misc.c | drop |  |
| `SIIsChanBusy` | `BOOL SIIsChanBusy(s32 chan)` | pc_misc.c | drop |  |
| `SIRefreshSamplingRate` | `void SIRefreshSamplingRate(void)` | pc_misc.c | drop |  |
| `SIRegisterPollingHandler` | `BOOL SIRegisterPollingHandler(void* handler)` | pc_misc.c | drop |  |
| `SIUnregisterPollingHandler` | `BOOL SIUnregisterPollingHandler(void* handler)` | pc_misc.c | drop |  |
| `DBInit` | `void DBInit(void)` | pc_misc.c | drop |  |
| `DBIsDebuggerPresent` | `BOOL DBIsDebuggerPresent(void)` | pc_misc.c | drop |  |
| `DBPrintf` | `void DBPrintf(const char* fmt, ...)` | pc_misc.c | drop |  |
| `InitMetroTRK` | `void InitMetroTRK(void)` | pc_misc.c | port-as-is |  |
| `InitMetroTRK_BBA` | `void InitMetroTRK_BBA(void)` | pc_misc.c | port-as-is |  |

### libc shims

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `bzero` | `void bzero(void* s, unsigned int n)` | pc_misc.c | drop |  |
| `bcopy` | `void bcopy(const void* src, void* dst, unsigned int n)` | pc_misc.c | drop |  |

### Famicom (NES emu) stubs

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `famicom_1frame` | `void famicom_1frame(void)` | pc_stubs.c | port-as-is |  |
| `famicom_cleanup` | `int famicom_cleanup(void)` | pc_stubs.c | port-as-is |  |
| `famicom_external_data_save` | `int famicom_external_data_save(void)` | pc_stubs.c | port-as-is |  |
| `famicom_external_data_save_check` | `int famicom_external_data_save_check(void)` | pc_stubs.c | port-as-is |  |
| `famicom_getErrorChan` | `int famicom_getErrorChan(void)` | pc_stubs.c | port-as-is |  |
| `famicom_get_disksystem_titles` | `int famicom_get_disksystem_titles(int* n_games, char* title_name_bufp, int namebuf_size)` | pc_stubs.c | port-as-is |  |
| `famicom_init` | `int famicom_init(int rom_idx, void* malloc_info, int player_no)` | pc_stubs.c | port-as-is | The NES emulator core is PPC assembly and is excluded from the build; these stubs satisfy the linker. NES is a declared non-goal for DC. |
| `famicom_internal_data_load` | `int famicom_internal_data_load(void)` | pc_stubs.c | port-as-is |  |
| `famicom_internal_data_save` | `int famicom_internal_data_save(void)` | pc_stubs.c | port-as-is |  |
| `famicom_mount_archive` | `void famicom_mount_archive(void)` | pc_stubs.c | port-as-is |  |
| `famicom_mount_archive_end_check` | `int famicom_mount_archive_end_check(void)` | pc_stubs.c | port-as-is |  |
| `famicom_rom_load_check` | `int famicom_rom_load_check(void)` | pc_stubs.c | port-as-is |  |
| `famicom_setCallback_getSaveChan` | `void famicom_setCallback_getSaveChan(void* proc)` | pc_stubs.c | port-as-is |  |

### libultra stubs

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `osContGetQuery` | `void osContGetQuery(void* status)` | pc_stubs.c | port-as-is |  |
| `osContGetReadData` | `void osContGetReadData(void* pad)` | pc_stubs.c | port-as-is |  |
| `osContInit` | `s32 osContInit(void* mq, u8* pattern_p, void* status)` | pc_stubs.c | port-as-is | libultra controller API — the decomp calls it but the GC build never uses it. Stubs compile unchanged. |
| `osContSetCh` | `s32 osContSetCh(u8 num_controllers)` | pc_stubs.c | port-as-is |  |
| `osContStartQuery` | `s32 osContStartQuery(void* mq)` | pc_stubs.c | port-as-is |  |
| `osContStartReadData` | `s32 osContStartReadData(void* mq)` | pc_stubs.c | port-as-is |  |
| `osDestroyThread` | `void osDestroyThread(void* t)` | pc_stubs.c | port-as-is |  |
| `osGetThreadId` | `s32 osGetThreadId(void* thread)` | pc_stubs.c | port-as-is |  |
| `osSetTimer` | `int osSetTimer(void* t, s64 countdown, s64 interval, void* mq, void* msg)` | pc_stubs.c | port-as-is |  |
| `osSyncPrintf` | `void osSyncPrintf(const char* fmt, ...)` | pc_stubs.c | port-as-is |  |

### GBA link stubs

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `GBAInit` | `void GBAInit(void)` | pc_stubs.c | port-as-is |  |
| `GBAGetStatus` | `s32 GBAGetStatus(s32 chan, u8* status)` | pc_stubs.c | port-as-is |  |
| `GBAGetProcessStatus` | `s32 GBAGetProcessStatus(s32 chan, u8* percentp)` | pc_stubs.c | port-as-is |  |
| `GBARead` | `s32 GBARead(s32 chan, u8* dst, u8* status)` | pc_stubs.c | port-as-is |  |
| `GBAWrite` | `s32 GBAWrite(s32 chan, u8* src, u8* status)` | pc_stubs.c | port-as-is |  |
| `GBAReset` | `s32 GBAReset(s32 chan, u8* status)` | pc_stubs.c | port-as-is |  |
| `GBAJoyBootAsync` | `s32 GBAJoyBootAsync(s32 chan, s32 palette_color, s32 palette_speed, u8* programp, s32 length, u8* status, void* callback)` | pc_stubs.c | port-as-is |  |

### Misc stubs

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `__abs` | `int __abs(int x)` | pc_stubs.c | port-as-is |  |
| `_strip` ✱ | `void _strip(float x)` | pc_stubs.c | port-as-is |  |
| `vaprintf` | `int vaprintf(void* func, const char* fmt, va_list ap)` | pc_stubs.c | port-as-is |  |

### JSystem C++ vtable fills

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `JSUOutputStream::~JSUOutputStream` ✱ | `JSUOutputStream::~JSUOutputStream()` | pc_stubs_cpp.cpp | port-as-is |  |
| `JSUOutputStream::skip` ✱ | `int JSUOutputStream::skip(s32 amount)` | pc_stubs_cpp.cpp | port-as-is |  |
| `JSURandomOutputStream::getAvailable` ✱ | `int JSURandomOutputStream::getAvailable() const` | pc_stubs_cpp.cpp | port-as-is |  |
| `JSURandomOutputStream::skip` ✱ | `int JSURandomOutputStream::skip(s32 amount)` | pc_stubs_cpp.cpp | port-as-is |  |
| `JKRHeap::destroy` ✱ | `void JKRHeap::destroy()` | pc_stubs_cpp.cpp | port-as-is | Declared in the decomp headers but never compiled; C++ vtables need a body. Compiles unchanged for sh-elf. |
| `JKRTask::searchBlank` ✱ | `JKRTask::Request* JKRTask::searchBlank()` | pc_stubs_cpp.cpp | port-as-is |  |

### glibc symbol-version compat

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `__isoc23_strtol` ✱ | `long int __isoc23_strtol(const char *nptr, char **endptr, int base)` | glibc_compat.c | drop | glibc 2.38+ symbol-version shim so the ARM binary runs on older glibc. Irrelevant with newlib — drop. |
| `__isoc23_sscanf` ✱ | `int __isoc23_sscanf(const char *str, const char *fmt, ...)` | glibc_compat.c | drop |  |

### OS — console settings / misc stubs

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `OSGetFontEncode` | `u16 OSGetFontEncode(void)` | pc_stubs.c | port-as-is |  |
| `OSGetProgressiveMode` | `u32 OSGetProgressiveMode(void)` | pc_stubs.c | port-as-is |  |
| `OSSetProgressiveMode` | `void OSSetProgressiveMode(u32 on)` | pc_stubs.c | port-as-is |  |
| `OSGetSoundMode` | `u32 OSGetSoundMode(void)` | pc_stubs.c | port-as-is |  |
| `OSSetSoundMode` | `void OSSetSoundMode(u32 mode)` | pc_stubs.c | port-as-is |  |
| `OSProtectRange` | `void OSProtectRange(u32 chan, void* addr, u32 nBytes, u32 control)` | pc_stubs.c | port-as-is |  |
| `OSSetErrorHandler` | `void* OSSetErrorHandler(u16 error, void* handler)` | pc_stubs.c | port-as-is |  |
| `OSReportEnable` | `void OSReportEnable(void)` | pc_stubs.c | port-as-is |  |

---
