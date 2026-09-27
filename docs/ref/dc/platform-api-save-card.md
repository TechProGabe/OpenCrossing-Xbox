# Platform API — CARD (memory card / VMU) and the save layer

Save sizes and layout, the 29 `CARD*` entry points with their layout-critical
structs, the ~1440 LOC `pc_m_card.c` game-side save manager, and the GCI
byte-swap layer that keeps Dolphin interchange working.
Read before writing the VMU backend (PLAN §6, `kb/save-budget.md`).
Split out of `kb/design-platform-api.md` (§3.8, §5). Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### 3.8 Saves

`GCI_FILE_DATA_SIZE = mCD_LAND_SAVE_SIZE = 0x72000` = **466,944 bytes**
(≈456 KB), sector size `0x2000`. Layout inside the file: others block at 0,
main save at `0x26000`, backup save at `0x4C000`. VMU user space ≈ 100 KB.
`pc_save_bswap.c` keeps the on-disk image big-endian so GCI files interchange
with Dolphin — that stays true on SH-4 (also LE), and is worth preserving
regardless of what the on-VMU format ends up being (PLAN §6).

---

### CARD — memory card

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `CARDInit` | `void CARDInit(void)` | pc_card.c | rewrite-for-KOS |  |
| `CARDMount` | `s32 CARDMount(s32 chan, void* workArea, void* detachCallback)` | pc_card.c | rewrite-for-KOS | PC backs channel 0 with save/card_a/ and channel 1 with save/card_b/, one .gci file each. DC: VMU via KOS vmufs, ~100 KB (PLAN §6). |
| `CARDMountAsync` | `s32 CARDMountAsync(s32 chan, void* workArea, void* detachCb, void* attachCb)` | pc_card.c | rewrite-for-KOS |  |
| `CARDUnmount` | `s32 CARDUnmount(s32 chan)` | pc_card.c | rewrite-for-KOS |  |
| `CARDOpen` | `s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo_PC* fileInfo)` | pc_card.c | rewrite-for-KOS | ★ CARDFileInfo is 20 bytes (chan/fileNo/offset/length/iBlock) and must match dolphin/card.h; extra PC state lives in a side table keyed by the fileInfo pointer. |
| `CARDClose` | `s32 CARDClose(CARDFileInfo_PC* fileInfo)` | pc_card.c | rewrite-for-KOS |  |
| `CARDCreate` | `s32 CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo_PC* fileInfo)` | pc_card.c | rewrite-for-KOS |  |
| `CARDCreateAsync` | `s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, void* fileInfo, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `CARDRead` | `s32 CARDRead(CARDFileInfo_PC* fileInfo, void* buf, s32 length, s32 offset)` | pc_card.c | rewrite-for-KOS |  |
| `CARDReadAsync` | `s32 CARDReadAsync(void* fileInfo, void* buf, s32 length, s32 offset, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `CARDWrite` | `s32 CARDWrite(CARDFileInfo_PC* fileInfo, const void* buf, s32 length, s32 offset)` | pc_card.c | rewrite-for-KOS | Sector size is 0x2000; the AC land file is 0x72000 bytes (466,944 = ~456 KB) — 4.6x the VMU budget. |
| `CARDWriteAsync` | `s32 CARDWriteAsync(void* fileInfo, const void* buf, s32 length, s32 offset, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `CARDDelete` | `s32 CARDDelete(s32 chan, const char* fileName)` | pc_card.c | rewrite-for-KOS |  |
| `CARDDeleteAsync` | `s32 CARDDeleteAsync(s32 chan, const char* fileName, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `CARDGetResultCode` | `s32 CARDGetResultCode(s32 chan)` | pc_card.c | rewrite-for-KOS |  |
| `CARDFreeBlocks` | `s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed)` | pc_card.c | rewrite-for-KOS |  |
| `CARDGetSectorSize` | `s32 CARDGetSectorSize(s32 chan, u32* size)` | pc_card.c | rewrite-for-KOS |  |
| `CARDProbeEx` | `s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)` | pc_card.c | rewrite-for-KOS | Reports card size/sector size; feeds the game's free-block arithmetic. Lying here about a VMU's capacity is how a compressed/segmented scheme gets accepted by game code. |
| `CARDProbe` | `s32 CARDProbe(s32 chan)` | pc_card.c | rewrite-for-KOS |  |
| `CARDCheck` | `s32 CARDCheck(s32 chan)` | pc_card.c | rewrite-for-KOS |  |
| `CARDCheckAsync` | `s32 CARDCheckAsync(s32 chan, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `CARDGetStatus` | `s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat)` | pc_card.c | rewrite-for-KOS |  |
| `CARDSetStatus` | `s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat)` | pc_card.c | rewrite-for-KOS |  |
| `CARDSetStatusAsync` | `s32 CARDSetStatusAsync(s32 chan, s32 fileNo, void* stat, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `CARDRename` | `s32 CARDRename(s32 chan, const char* oldName, const char* newName)` | pc_card.c | rewrite-for-KOS |  |
| `CARDRenameAsync` | `s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `CARDFormat` | `s32 CARDFormat(s32 chan)` | pc_card.c | rewrite-for-KOS |  |
| `CARDFormatAsync` | `s32 CARDFormatAsync(s32 chan, void* callback)` | pc_card.c | rewrite-for-KOS |  |
| `pc_card_scan_for_gci` ✱ | `int pc_card_scan_for_gci(s32 chan, char* out_path, int out_size)` | pc_card.c | rewrite-for-KOS |  |

### m_card — game save manager (replaces src/game/m_card.c)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `mCD_save_data_aram_malloc` | `void mCD_save_data_aram_malloc(void)` | pc_m_card.c | rewrite-for-KOS | The mail/original-design/diary blocks are ARAM-resident on GC; the PC port malloc()s them instead. On DC these are small enough to stay resident. |
| `mCD_save_data_aram_to_main` | `int mCD_save_data_aram_to_main(void* dst, u32 size, u32 idx)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_save_data_main_to_aram` | `int mCD_save_data_main_to_aram(void* src, u32 size, u32 idx)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_set_aram_save_data` | `void mCD_set_aram_save_data(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `pc_save_reload` | `int pc_save_reload(void)` | pc_m_card.c | rewrite-for-KOS | PC-only hot-reload of the .gci; keep for the emulator workflow, drop on hardware. |
| `pc_save_check_and_load` | `int pc_save_check_and_load(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_init_card` | `void mCD_init_card(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_InitAll` | `void mCD_InitAll(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_InitGameStart_bg` | `int mCD_InitGameStart_bg(int player_no, int card_private_idx, int start_cond, s32* mounted_chan)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_LoadLand` | `void mCD_LoadLand(void)` | pc_m_card.c | rewrite-for-KOS | pc_m_card.c is NOT a Dolphin SDK replacement — it re-implements the GAME's own src/game/m_card.c, which CMake excludes. ~1440 LOC of save/village/travel logic. Reusable on DC above a new storage backend. |
| `mCD_SaveHome_bg` | `int mCD_SaveHome_bg(int param_1, int* chan)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_CheckStation_bg` | `int mCD_CheckStation_bg(s32* chan)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_SaveStation_NextLand_bg` | `int mCD_SaveStation_NextLand_bg(s32* chan)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_SaveStation_Passport_bg` | `int mCD_SaveStation_Passport_bg(s32* chan)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_toNextLand` | `void mCD_toNextLand(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_ReCheckLoadLand` | `void mCD_ReCheckLoadLand(GAME_PLAY* play)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_GetThisLandSlotNo` | `int mCD_GetThisLandSlotNo(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_GetThisLandSlotNo_code` | `int mCD_GetThisLandSlotNo_code(int* player_no, s32* slot_card_results)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_GetSaveHomeSlotNo` | `int mCD_GetSaveHomeSlotNo(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_GetPlayerNum` | `int mCD_GetPlayerNum(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_GetCardPrivateNameCopy` | `int mCD_GetCardPrivateNameCopy(u8* name, int idx)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_CheckCardPlayerNative` | `int mCD_CheckCardPlayerNative(int idx)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_CheckPassportFile` | `int mCD_CheckPassportFile(void)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_CheckBrokenPassportFile` | `int mCD_CheckBrokenPassportFile(int slot)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_EraseBrokenLand_bg` | `int mCD_EraseBrokenLand_bg(int* slot)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_EraseLand_bg` | `int mCD_EraseLand_bg(int* slot)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_ErasePassportFile_bg` | `int mCD_ErasePassportFile_bg(int slot)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_SaveErasePlayer_bg` | `int mCD_SaveErasePlayer_bg(int* slot)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_card_format_bg` | `int mCD_card_format_bg(s32 chan)` | pc_m_card.c | rewrite-for-KOS |  |
| `mCD_PrintErrInfo` | `void mCD_PrintErrInfo(gfxprint_t* gfxprint)` | pc_m_card.c | rewrite-for-KOS |  |

### Save byte-swap (GCI interchange)

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_save_bswap` | `void pc_save_bswap(Save_t* save, pc_bswap_dir_t dir)` | pc_save_bswap.c | port-as-is | SH-4 is little-endian exactly like the base port's targets, so the whole GCI byte-swap layer carries over verbatim and Dolphin interchange is preserved. |
| `pc_save_bswap_keep_mail` ✱ | `void pc_save_bswap_keep_mail(mCD_keep_mail_c* mail, pc_bswap_dir_t dir)` | pc_save_bswap.c | port-as-is |  |
| `pc_save_bswap_keep_original` ✱ | `void pc_save_bswap_keep_original(mCD_keep_original_c* orig, pc_bswap_dir_t dir)` | pc_save_bswap.c | port-as-is |  |
| `pc_save_bswap_keep_diary` ✱ | `void pc_save_bswap_keep_diary(mCD_keep_diary_c* diary, pc_bswap_dir_t dir)` | pc_save_bswap.c | port-as-is |  |
| `pc_save_bswap_verify_roundtrip` ✱ | `int pc_save_bswap_verify_roundtrip(const u8* original_be, u32 size)` | pc_save_bswap.c | port-as-is |  |
| `pc_save_bswap_verify_roundtrip_mail` ✱ | `int pc_save_bswap_verify_roundtrip_mail(const u8* original_be, u32 size)` | pc_save_bswap.c | port-as-is |  |
| `pc_save_bswap_verify_roundtrip_original` ✱ | `int pc_save_bswap_verify_roundtrip_original(const u8* original_be, u32 size)` | pc_save_bswap.c | port-as-is |  |
| `pc_save_bswap_verify_roundtrip_diary` ✱ | `int pc_save_bswap_verify_roundtrip_diary(const u8* original_be, u32 size)` | pc_save_bswap.c | port-as-is |  |
| `pc_checksum_be` ✱ | `u16 pc_checksum_be(const u8* data, u32 size, u16 old_checksum)` | pc_save_bswap.c | port-as-is | Big-endian checksum over the save; must stay BE for GCI compatibility regardless of host endianness. |
| `pc_save_bswap_foreigner` ✱ | `void pc_save_bswap_foreigner(mCD_foreigner_c* f, pc_bswap_dir_t dir)` | pc_save_bswap.c | port-as-is |  |
