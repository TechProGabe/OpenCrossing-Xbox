/* xbox_payload.c — what tools/xbox/xbe_payload.py appended to default.xbe:
 * the boot music (INTR) and the boot screen's painted layers (SP..). The
 * kernel never loads it; this reads the footer's table once and hands out
 * offsets. Each user opens its own handle (the music thread reads while
 * the splash does), so nothing here is shared but the table. */
#include <windows.h>
#include <string.h>
#include "xbox_io.h"
#include "xbox_payload.h"

#define MAX_ENTRIES 16

typedef struct { char tag[4]; unsigned off, size; } Entry;
static Entry s_table[MAX_ENTRIES];
static int s_count = -1;   /* -1: not read yet */

HANDLE xbox_payload_open(void) {
    return CreateFileA("D:\\default.xbe", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

int xbox_payload_read(HANDLE h, unsigned off, void* dst, unsigned n) {
    DWORD got = 0;
    if (SetFilePointer(h, (LONG)off, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return 0;
    return ReadFile(h, dst, n, &got, NULL) && got == n;
}

static void load_table(void) {
    unsigned foot[4], n;
    DWORD size;
    HANDLE h = xbox_payload_open();
    s_count = 0;
    if (h == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(h, NULL);
    /* xbe_len, n, version, "OCPK" */
    if (size != INVALID_FILE_SIZE && size >= 16 && xbox_payload_read(h, size - 16, foot, 16) &&
        memcmp(&foot[3], "OCPK", 4) == 0 && foot[2] == 1 && (n = foot[1]) <= MAX_ENTRIES &&
        16 + 12 * n <= size && xbox_payload_read(h, size - 16 - 12 * n, s_table, 12 * n)) {
        unsigned i;
        for (i = 0; i < n; i++)
            if ((unsigned long long)s_table[i].off + s_table[i].size > size) break;
        if (i == n) s_count = (int)n;
    }
    CloseHandle(h);
    xbox_logf("[XBOX] payload: %d entries at the end of default.xbe\n", s_count);
}

int xbox_payload_find(const char* tag, unsigned* off, unsigned* size) {
    int i;
    if (s_count < 0) load_table();
    for (i = 0; i < s_count; i++)
        if (memcmp(s_table[i].tag, tag, 4) == 0) {
            *off = s_table[i].off;
            *size = s_table[i].size;
            return 1;
        }
    return 0;
}
