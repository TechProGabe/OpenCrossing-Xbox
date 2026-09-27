/* xbox_aram.c — the GameCube's 16 MB ARAM without 16 MB of RAM.
 * Replaces pc/src/pc_aram.c (a flat malloc(16 MB)).
 *
 * ARAM is only ever touched through ARStartDMA/ARQPostRequest (verified: no
 * caller dereferences the base), so it can be an address space instead of a
 * buffer:
 *   - resident pages (32 KB) allocated on first write — the RARC archives
 *     JKRAram mounts (~6.7 MB) and the audio DMA/heap buffers land here;
 *   - disc-backed ranges: DVDT_LoadtoARAM (jaudio: audiorom.img, 8.3 MB) maps
 *     the file instead of copying it (one TARGET_XBOX branch in dvdthread.c),
 *     and reads are served from the disc image through a small LRU cache;
 *   - anything else reads as zero.
 * Kill switch: -DXBOX_ARAM_FLAT=1 restores the flat 16 MB buffer semantics
 * (every range resident, no disc mapping). docs/memory.md has the numbers. */
#include "pc_platform.h"
#include "pc_disc.h"
#include "xbox_io.h"

#define ARAM_PAGE     (32u * 1024u)
#define ARAM_NPAGES   (PC_ARAM_SIZE / ARAM_PAGE)
#ifndef XBOX_ARAM_CACHE_SLOTS
#define XBOX_ARAM_CACHE_SLOTS 48     /* x 32 KB = 1.5 MB of cached disc data */
#endif
#ifndef XBOX_ARAM_FLAT
#define XBOX_ARAM_FLAT 0
#endif

static u8* s_page[ARAM_NPAGES];
static u32 s_resident;
static u32 aram_alloc_ptr;
static int s_inited;

typedef struct { u32 aram, len, disc; } DiscMap;
static DiscMap s_map[16];
static int s_nmap;

typedef struct { u32 key; u32 stamp; u8* buf; } Slot;   /* key = disc block index + 1 */
static Slot s_slot[XBOX_ARAM_CACHE_SLOTS];
static u32 s_clock, s_hits, s_misses;

u32 ARInit(u32* stack_idx_addr, u32 length) {
    (void)stack_idx_addr; (void)length;
    s_inited = 1;
    aram_alloc_ptr = 0;
    return 0;
}

u8* pc_aram_get_base(void) { return NULL; }
u32 ARGetBaseAddress(void) { return 0; }
u32 ARGetSize(void) { return PC_ARAM_SIZE; }
u32 ARGetInternalSize(void) { return PC_ARAM_SIZE; }
BOOL ARCheckInit(void) { return s_inited; }

u32 ARAlloc(u32 size) {
    u32 aligned = (size + 31) & ~31u;
    u32 addr;
    if (aram_alloc_ptr + aligned > PC_ARAM_SIZE) {
        xbox_logf("[ARAM] out of ARAM: want %u, used %u\n", (unsigned)size, (unsigned)aram_alloc_ptr);
        return 0;
    }
    addr = aram_alloc_ptr;
    aram_alloc_ptr += aligned;
    return addr;
}

void ARFree(u32* addr) { (void)addr; }

static const DiscMap* find_map(u32 a) {
    int i;
    for (i = 0; i < s_nmap; i++)
        if (a >= s_map[i].aram && a < s_map[i].aram + s_map[i].len) return &s_map[i];
    return NULL;
}

static u8* cache_block(u32 disc_block) {
    int i, victim = 0;
    u32 oldest = 0xFFFFFFFFu;
    s_clock++;
    for (i = 0; i < XBOX_ARAM_CACHE_SLOTS; i++) {
        if (s_slot[i].key == disc_block + 1) {
            s_slot[i].stamp = s_clock;
            s_hits++;
            return s_slot[i].buf;
        }
        if (s_slot[i].stamp < oldest) { oldest = s_slot[i].stamp; victim = i; }
    }
    s_misses++;
    if (!s_slot[victim].buf) {
        s_slot[victim].buf = (u8*)malloc(ARAM_PAGE);
        if (!s_slot[victim].buf) return NULL;
    }
    if (!pc_disc_read(disc_block * ARAM_PAGE, s_slot[victim].buf, ARAM_PAGE)) {
        s_slot[victim].key = 0;
        return NULL;
    }
    s_slot[victim].key = disc_block + 1;
    s_slot[victim].stamp = s_clock;
    return s_slot[victim].buf;
}

static void read_disc(u8* dst, u32 disc_off, u32 len) {
    while (len) {
        u32 blk = disc_off / ARAM_PAGE, in = disc_off % ARAM_PAGE;
        u32 n = ARAM_PAGE - in < len ? ARAM_PAGE - in : len;
        u8* b = cache_block(blk);
        if (b) memcpy(dst, b + in, n);
        else memset(dst, 0, n);
        dst += n;
        disc_off += n;
        len -= n;
    }
}

/* type 0 = MRAM->ARAM, type 1 = ARAM->MRAM. params are always (type, mram, aram). */
void ARStartDMA(u32 type, u32 mram_addr, u32 aram_addr, u32 length) {
    u8* m = (u8*)(uintptr_t)mram_addr;
    if (!length) return;
    if (length > PC_ARAM_SIZE || aram_addr > PC_ARAM_SIZE - length) {
        if (type == 1 && m && length <= 0x100000) memset(m, 0, length);
        return;
    }
    while (length) {
        u32 pg = aram_addr / ARAM_PAGE, in = aram_addr % ARAM_PAGE;
        u32 n = ARAM_PAGE - in < length ? ARAM_PAGE - in : length;
        if (type == 0) {
            if (!s_page[pg]) {
                s_page[pg] = (u8*)calloc(1, ARAM_PAGE);
                if (!s_page[pg]) { xbox_logf("[ARAM] page alloc failed at %08x\n", (unsigned)aram_addr); return; }
                s_resident++;
            }
            memcpy(s_page[pg] + in, m, n);
        } else if (s_page[pg]) {
            memcpy(m, s_page[pg] + in, n);
        } else {
            const DiscMap* d = find_map(aram_addr);
            if (d) {
                u32 run = d->aram + d->len - aram_addr;
                if (run < n) n = run;
                read_disc(m, d->disc + (aram_addr - d->aram), n);
            } else {
                memset(m, 0, n);
            }
        }
        m += n;
        aram_addr += n;
        length -= n;
    }
}

static int add_map(u32 aram, u32 len, u32 disc) {
    int i, j;
    /* a remount over the same ARAM replaces whatever mapped it before */
    for (i = j = 0; i < s_nmap; i++)
        if (s_map[i].aram + s_map[i].len <= aram || aram + len <= s_map[i].aram) s_map[j++] = s_map[i];
    s_nmap = j;
    if (s_nmap >= (int)(sizeof s_map / sizeof s_map[0])) return 0;
    s_map[s_nmap].aram = aram;
    s_map[s_nmap].len = (len + 31) & ~31u;
    s_map[s_nmap].disc = disc;
    s_nmap++;
    return 1;
}

/* JKRAramArchive::open (TARGET_XBOX branch): map an uncompressed archive's
 * data block. entryNum -> disc offset via DVDFastOpen (pc_dvd.c layout:
 * startAddr at +0x30). */
extern BOOL DVDFastOpen(s32 entrynum, void* fileInfo);
int xbox_aram_map_entry(long entryNum, u32 file_off, u32 aram, u32 len) {
    u8 fi[0x40];
    u32 start;
    if (XBOX_ARAM_FLAT) return 0;
    memset(fi, 0, sizeof fi);
    if (!DVDFastOpen((s32)entryNum, fi)) return 0;
    start = *(u32*)(fi + 0x30);
    if (!add_map(aram, len, start + file_off)) return 0;
    xbox_logf("[ARAM] archive entry %ld mapped from disc: aram %08x +%u (no copy)\n", entryNum, (unsigned)aram,
              (unsigned)len);
    return 1;
}

/* Called from jaudio's DVDT_LoadtoARAM_Main (TARGET_XBOX branch). Returns 1 if
 * the range is now disc-backed and the copy should be skipped. */
int xbox_aram_map_file(const char* name, u32 src, u32 dst, u32 length) {
    u32 off, size;
    if (XBOX_ARAM_FLAT) return 0;
    if (!pc_disc_find_file(name, &off, &size) || src >= size) return 0;
    if (!length || length > size - src) length = size - src;
    if (!add_map(dst, length, off + src)) return 0;
    xbox_logf("[ARAM] %s mapped from disc: aram %08x +%u (no copy)\n", name, (unsigned)dst, (unsigned)length);
    return 1;
}

void xbox_aram_log(void) {
    xbox_logf("[ARAM] resident %u KB, disc maps %d, cache %u hits / %u misses\n",
              (unsigned)(s_resident * (ARAM_PAGE / 1024)), s_nmap, (unsigned)s_hits, (unsigned)s_misses);
}

/* ARQ - synchronous wrapper around ARStartDMA (same as pc_aram.c). */
void ARQInit(void) {}
void ARQPostRequest(void* req, u32 owner, u32 type, u32 prio, u32 source, u32 dest, u32 length, void* callback) {
    (void)owner; (void)prio;
    if (type == 0) ARStartDMA(type, source, dest, length);
    else ARStartDMA(type, dest, source, length);
    if (callback) ((void (*)(u32))callback)((u32)(uintptr_t)req);
}
void ARQFlushQueue(void) {}
