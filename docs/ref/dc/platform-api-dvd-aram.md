# Platform API — DVD / disc I/O and AR/ARQ (ARAM)

The two storage seams: `DVDReadAsyncPrio` (async faked as sync, callback before
return) and `ARStartDMA` (the whole 16 MB ARAM problem in one function), plus the
DVD, AR/ARQ, runtime asset table and disc-image-reader symbol tables.
Read before writing disc read-ahead, the ARAM window, or the asset loader.
Split out of `kb/design-platform-api.md` (§3.4, §3.5, §5). Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### 3.4 The ARAM DMA contract ★

`ARStartDMA(type, mram_addr, aram_addr, length)` is a `memcpy` today, and it
is the single seam between the game and the 16 MB of auxiliary RAM the
Dreamcast does not have. Three behaviours are load-bearing:

1. **ARAM addresses are offsets, not pointers.** `ARInit` returns 0;
   `ARGetBaseAddress()` returns 0; `ARAlloc` is a 32-byte-aligned bump
   allocator that never frees.
2. **Defensive pointer normalisation.** Some callers pass
   `aram_base + offset` instead of a bare offset. `pc_aram.c:45-48` detects
   an `aram_addr` inside `[aram_base, aram_base+16MB)` and subtracts the
   base. Removing this will break those callers.
3. **Out-of-range reads zero-fill.** For `type == 1` (ARAM→MRAM) with an
   out-of-range source, the destination is `memset` to 0 (capped at 1 MB)
   rather than left as garbage. Game code depends on getting zeros.

`ARQPostRequest` has an **argument-order trap**: its `(source, dest)` are not
`ARStartDMA`'s `(mram, aram)`. For `type == 0` it forwards
`(source→mram, dest→aram)`; for `type == 1` it **swaps** them. The completion
callback is invoked synchronously with the request pointer cast to `u32`.

Consumers: `JKRAram`/`JKRAramArchive`/`JKRAramPiece` (graph half, 6.96 MB of
RARC archives) and jaudio (`aramcall.c`, sound half, 8.44 MB). PLAN §3.1 turns
the graph half into a disc-backed LRU window — **`ARStartDMA` is where that
window is implemented**, and it is the only place it needs implementing.

### 3.5 DVD: async faked as sync ★

`DVDReadAsyncPrio(fileInfo, buf, len, off, callback, prio)` performs the whole
read **inline** and calls `callback(nread, fileInfo)` **before returning
TRUE**. Every "async" DVD path in the game therefore completes before the
caller's next statement. `DVDGetCommandBlockStatus` always returns 0
(`DVD_STATE_END`) for the same reason.

This has been validated across a full playthrough on the base port, so it is
safe — but it means:

- Any real read-ahead thread on DC must **preserve callback-before-return**,
  or every caller that polls a "done" flag needs re-auditing.
- The read-ahead has to be a *prefetch* layer under a still-synchronous
  `DVDRead`, not a genuine async API. That is also the right shape for
  CD-R at ~500 KB/s.

Two more layout facts:

- `DVDConvertPathToEntrynum` **interns** unknown paths into a growing table
  (`MAX_DVD_ENTRIES = 512`) and returns the index — it never returns -1 for
  an unknown file the way real GC does. Failure is deferred to `DVDFastOpen`.
- `DVDFastOpen` writes into the game's 0x3C-byte `DVDFileInfo` at hand-picked
  offsets: handle/sentinel at **+0x18**, `startAddr` at **+0x30**, `length` at
  **+0x34**. Those offsets come from `dolphin/dvd.h` and must not move.

### DVD — file I/O (async faked as sync)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `DVDGetCurrentDiskID` | `DVDDiskID* DVDGetCurrentDiskID(void)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDConvertPathToEntrynum` | `s32 DVDConvertPathToEntrynum(const char* path)` | pc_dvd.c | rewrite-for-KOS | ★ PC fakes the FST: it interns any path into a growing table (MAX_DVD_ENTRIES=512) and returns the index, so it NEVER fails for an unknown path. Real GC returns -1. Keep the interning on DC or code that checks for -1 changes behavior. |
| `DVDFastOpen` | `BOOL DVDFastOpen(s32 entrynum, void* fileInfo)` | pc_dvd.c | rewrite-for-KOS | ★ Writes into the game's DVDFileInfo at hand-picked offsets: FILE*/sentinel at +0x18, startAddr at +0x30, length at +0x34, total 0x3C bytes. Layout-critical. |
| `DVDOpen` | `BOOL DVDOpen(const char* filename, void* fileInfo)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDClose` | `BOOL DVDClose(void* fileInfo)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDReadPrio` | `s32 DVDReadPrio(void* fileInfo, void* buf, s32 length, s32 offset, s32 prio)` | pc_dvd.c | rewrite-for-KOS | Synchronous fread/disc-image read. Blocking. CD-R at ~500 KB/s makes this the read-ahead seam (PLAN §5). |
| `DVDRead` | `s32 DVDRead(void* fileInfo, void* buf, s32 length, s32 offset)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDGetLength` ✱ | `u32 DVDGetLength(void* fileInfo)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDReadAsyncPrio` | `BOOL DVDReadAsyncPrio(void* fileInfo, void* buf, s32 length, s32 offset, pc_DVDCallback callback, s32 prio)` | pc_dvd.c | rewrite-for-KOS | ★ ASYNC FAKED AS SYNC: performs the whole read inline and calls the callback BEFORE returning TRUE. Any game code that assumed 'callback fires later' now sees it fire first — this is load-bearing and already validated on the base port. A real read-ahead thread on DC must preserve callback-before-return or re-validate every caller. |
| `OSDVDFatalError` | `void OSDVDFatalError(void)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDInit` | `void DVDInit(void)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDSetAutoFatalMessaging` ✱ | `void DVDSetAutoFatalMessaging(BOOL enable)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDGetFileInfoStatus` | `s32 DVDGetFileInfoStatus(void* fileInfo)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDGetTransferredSize` | `s32 DVDGetTransferredSize(void* fileInfo)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDFastClose` ✱ | `BOOL DVDFastClose(void* fileInfo)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDGetDriveStatus` | `s32 DVDGetDriveStatus(void)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDCancel` | `s32 DVDCancel(void* block)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDCancelAsync` | `BOOL DVDCancelAsync(void* block, void* callback)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDChangeDisk` | `s32 DVDChangeDisk(void* block, void* id)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDChangeDiskAsync` | `BOOL DVDChangeDiskAsync(void* block, void* id, void* callback)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDGetCommandBlockStatus` | `s32 DVDGetCommandBlockStatus(void* block)` | pc_dvd.c | rewrite-for-KOS | Always 0 (DVD_STATE_END) because everything already completed. |
| `DVDPrepareStreamAsync` | `BOOL DVDPrepareStreamAsync(void* fi, u32 len, u32 off, void* cb)` | pc_dvd.c | rewrite-for-KOS |  |
| `DVDCancelStream` | `s32 DVDCancelStream(void* block)` | pc_dvd.c | rewrite-for-KOS |  |

### DVD — directory stubs

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `DVDCheckDisk` | `BOOL DVDCheckDisk(void)` | pc_stubs.c | stub-and-log |  |
| `DVDOpenDir` | `BOOL DVDOpenDir(char* dirName, void* dir)` | pc_stubs.c | stub-and-log | Directory enumeration — pure stub, 0 real callers. |
| `DVDReadDir` | `BOOL DVDReadDir(void* dir, void* dirEntry)` | pc_stubs.c | stub-and-log |  |
| `DVDCloseDir` | `BOOL DVDCloseDir(void* dir)` | pc_stubs.c | stub-and-log |  |

### AR / ARQ — auxiliary RAM DMA

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `ARInit` | `u32 ARInit(u32* stack_idx_addr, u32 length)` | pc_aram.c | rewrite-for-KOS | ★ Returns 0: ARAM addresses are OFFSETS from 0, not pointers. jsyswrap requests soundAram 0x810000 + graphAram 0x6A3780 = 16 MB. On DC this whole 16 MB cannot exist; PLAN §3.1 turns the graph half into a disc-backed LRU window and the sound half into AICA/stream data. |
| `pc_aram_get_base` ✱ | `u8* pc_aram_get_base(void)` | pc_aram.c | rewrite-for-KOS |  |
| `ARGetBaseAddress` | `u32 ARGetBaseAddress(void)` | pc_aram.c | rewrite-for-KOS |  |
| `ARGetSize` | `u32 ARGetSize(void)` | pc_aram.c | rewrite-for-KOS |  |
| `ARAlloc` | `u32 ARAlloc(u32 size)` | pc_aram.c | rewrite-for-KOS | Bump allocator, 32-byte aligned, never frees (ARFree is a no-op). |
| `ARFree` | `void ARFree(u32* addr)` | pc_aram.c | rewrite-for-KOS |  |
| `ARStartDMA` | `void ARStartDMA(u32 type, u32 mram_addr, u32 aram_addr, u32 length)` | pc_aram.c | rewrite-for-KOS | ★ THE ARAM SEAM. type 0 = MRAM->ARAM, 1 = ARAM->MRAM; it is a memcpy today. It also DEFENSIVELY normalizes aram_addr when a caller passes `aram_base + offset` instead of a bare offset, and zero-fills the destination on an out-of-range ARAM->MRAM read (cap 1 MB) so callers get zeros, not garbage. Both behaviors are relied on. This function is where disc-backed ARAM must be implemented. |
| `ARGetInternalSize` | `u32 ARGetInternalSize(void)` | pc_aram.c | rewrite-for-KOS |  |
| `ARCheckInit` | `BOOL ARCheckInit(void)` | pc_aram.c | rewrite-for-KOS |  |
| `ARQInit` | `void ARQInit(void)` | pc_aram.c | rewrite-for-KOS |  |
| `ARQPostRequest` | `void ARQPostRequest(void* req, u32 owner, u32 type, u32 prio, u32 source, u32 dest, u32 length, void* callback)` | pc_aram.c | rewrite-for-KOS | ★ Argument-order trap: ARQ's (source,dest) is NOT ARStartDMA's (mram,aram) — for type 1 the two are swapped. Completion callback is invoked synchronously with the request pointer as a u32. |
| `ARQFlushQueue` ✱ | `void ARQFlushQueue(void)` | pc_aram.c | rewrite-for-KOS | No-op because everything is synchronous. 0 call sites. |

### Runtime asset table (DOL/REL offsets + bswap class)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_bswap_asset_u16` ✱ | `void pc_bswap_asset_u16(void* data, unsigned int size)` | pc_assets.c | rewrite-for-KOS |  |
| `pc_bswap_asset_u32` ✱ | `void pc_bswap_asset_u32(void* data, unsigned int size)` | pc_assets.c | rewrite-for-KOS |  |
| `pc_bswap_asset_vtx` ✱ | `void pc_bswap_asset_vtx(void* data, unsigned int size)` | pc_assets.c | rewrite-for-KOS | Vtx records are mixed-width (s16 pos + u8 color) — cannot be swapped with a uniform stride, hence the per-asset class. |
| `pc_assets_pal_n64_to_gc` | `void pc_assets_pal_n64_to_gc(u16* pal, int count)` | pc_assets.c | rewrite-for-KOS |  |
| `pc_load_asset` | `void pc_load_asset(const char* bin_path, void* dest, unsigned int size, unsigned int rom_off, int rom_src, int swap_type)` | pc_assets.c | rewrite-for-KOS | ★ Generated 30k-line dispatch table (~2500 assets -> DOL/REL offset + per-asset byte-swap class), produced by pc/tools/gen_runtime_assets.py. The DC build keeps the table but the source moves from a runtime ISO parse to files on /cd. |
| `pc_assets_init` ✱ | `void pc_assets_init(void)` | pc_assets.c | rewrite-for-KOS | Called from main() BEFORE ac_entry(); populates every .inc asset the linked game code references. |

### Disc-image reader (moves to host tools/)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_disc_init` ✱ | `int pc_disc_init(void)` | pc_disc.c | drop | GC disc/CISO/GCM/FST/Yaz0 reader. On DC this runs OFFLINE in tools/ at disc-build time; nothing of it ships in 1ST_READ.BIN. |
| `pc_disc_is_open` ✱ | `int pc_disc_is_open(void)` | pc_disc.c | drop |  |
| `pc_disc_last_error` ✱ | `int pc_disc_last_error(void)` | pc_disc.c | drop |  |
| `pc_disc_game_id` ✱ | `const char* pc_disc_game_id(void)` | pc_disc.c | drop |  |
| `pc_disc_find_file` ✱ | `int pc_disc_find_file(const char* path, u32* disc_offset, u32* file_size)` | pc_disc.c | drop |  |
| `pc_disc_read` ✱ | `int pc_disc_read(u32 offset, void* dest, u32 size)` | pc_disc.c | drop |  |
| `pc_disc_extract_dol` ✱ | `u8* pc_disc_extract_dol(void)` | pc_disc.c | drop |  |
| `pc_disc_extract_rel` ✱ | `u8* pc_disc_extract_rel(void)` | pc_disc.c | drop |  |
| `pc_disc_shutdown` ✱ | `void pc_disc_shutdown(void)` | pc_disc.c | drop |  |
