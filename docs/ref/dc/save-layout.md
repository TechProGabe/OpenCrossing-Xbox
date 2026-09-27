# Save layout — what Animal Crossing's save actually is, byte by byte

§1 of `kb/save-budget.md`, moved verbatim: the verified struct sizes, the
`.gci` layout, why only 295,910 B of the 456 KB is unique, and the byte
breakdown by field group. Read before touching the save serialiser.
The *layout* numbers here are exact and verified; the compression numbers in
the sibling files are **SYNTHETIC** (`kb/save-budget.md`).

---

## 1. What the save actually is

Verified by compiling `tools/savebench/save_layout_probe.c` against the
vendored decomp headers for a 32-bit ARM EABI target; the resulting offsets
reproduce the `/* 0x...... */` comments in `include/m_common_data.h` byte for
byte. (A native 64-bit host build reports `sizeof(Save_t) == 153216` — wrong,
inflated by LP64 alignment. Always use the armhf container; see the harness
README.)

| symbol | bytes | hex | source |
|---|---:|---|---|
| `sizeof(Save_t)` | 148,128 | 0x242A0 | `include/m_common_data.h:81-174` |
| `sizeof(Save)` (sector-aligned) | 155,648 | 0x26000 | `include/m_common_data.h:176-179` |
| `sizeof(mCD_others_c)` | 153,056 | 0x255E0 | `include/m_card.h:179-185` |
| `OTHERS_SIZE` (sector-aligned) | 155,648 | 0x26000 | `include/m_card.h:287` |
| `mCD_LAND_SAVE_SIZE` | 466,944 | 0x72000 | `include/m_card.h:17` |
| `mCD_MEMCARD_SECTORSIZE` | 8,192 | 0x2000 | `include/m_card.h:128` |

The `.gci` the port writes (`pc/src/pc_m_card.c:56-61`, `:322-335`):

```
0x000000     64  CARDDir header
0x000040 0x26000 mCD_others_c   (GC banner/icon + the three "keep" lockers)
0x026040 0x26000 Save  main
0x04C040 0x26000 Save  backup   <- byte-identical duplicate of the above
        ---------
        467,008 B on disk / 466,944 B on card = 57 x 8 KB blocks
```

Of those 456 KB, only **295,910 bytes** are unique data the Dreamcast has to
keep:

| block | bytes | note |
|---|---:|---|
| `Save_t` | 148,128 | the town |
| `mCD_keep_original_c` | 52,384 | 96 stored design patterns (8 pages x 12) |
| `mCD_keep_mail_c` | 47,780 | 160 stored letters (8 pages x 20) |
| `mCD_keep_diary_c` | 47,618 | 48 diary entries (4 players x 12 months) |
| **unique payload** | **295,910** | **289.0 KiB** |

Everything else in the 456 KB is dead weight on DC: the 5,184 B GC
comment/banner/icon (`MemcardHeader_c`, `include/m_card.h:131-135` — replaced
by a 640 B VMS header), ~15 KB of sector-alignment padding, and the 155,648 B
backup copy. **Dropping those three costs nothing and is the first 35% saved.**

### 1.1 Byte breakdown by field group

From `savebench.py --groups`. Group membership is defined in
`tools/savebench/save_layout.py`, which tiles all four blocks exactly (no gaps,
no overlap — asserted).

| group | raw bytes | % of payload | what it is |
|---|---:|---:|---|
| designs | 74,528 | 25.2% | **137 design slots x 544 B.** 96 in the locker + 8 per player x 4 + 8 Able Sisters + 1 island flag. Each = 16 B name + palette + 512 B of 32x32 4bpp art (`include/m_needlework.h:56-61`) |
| letters | 71,752 | 24.2% | **240 `Mail_c` x 298 B.** 160 locker + 10 inventory x 4 players + 10 mailbox x 4 houses. 248 B of text each (`include/m_mail.h:99-104`) |
| keep_diary | 47,618 | 16.1% | 48 x 992 B of generated prose (`include/m_diary.h:12-16`) |
| villagers | 39,040 | 13.2% | 15 x `Animal_c` (2,440 B). 2,184 B of that is `memories[7]`, each carrying a 262 B saved letter (`include/m_npc.h:177-186, 200-230`) |
| house_layout | 28,840 | 9.7% | 4 houses x 3 rooms x 4 furniture layers, each a 16x16 u16 item grid, + the island cottage (`include/m_home_h.h:132-151`) |
| town_items | 16,320 | 5.5% | `fg[6][5]` = 30 acres x 16x16 u16 item ids (15,360) + `deposit` buried-item bitfield (960) |
| player_misc | 6,496 | 2.2% | ids, quests, fortune, calendar, e-Card bits |
| town_misc | 4,625 | 1.6% | shop, stalk market, events, post office, police box, snowmen, fish records, mask cat |
| noticeboard | 3,000 | 1.0% | 15 x 200 B bulletin-board posts |
| island | 1,168 | 0.4% | island name/acres/flags (its cottage, islander and flag design are counted in their own groups) |
| house_misc | 1,104 | 0.4% | ownership, HRA marks, gyroid messages, music box |
| catalog | 880 | 0.3% | furniture/wall/carpet/paper/music collection bitfields, 4 players |
| player_items | 184 | 0.1% | 15 pocket slots + equipment, 4 players |
| museum | 63 | 0.0% | `mMmd_info_c` display bits |
| keep_* headers | 260 | 0.1% | folder names, checksums |
| header | 32 | 0.0% | `mFRm_chk_t` + scene/counters |

Two groups — **designs and letters — are half the save**, and they are the only
two the player authors freely. Everything else is bounded, structured, and
compresses well.
