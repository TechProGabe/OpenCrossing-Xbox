/* xbox_disc_check.c — is the disc image a good Animal Crossing USA (GAFE01
 * rev 0)? Run once pc_disc_init() has opened it, before anything reads game
 * data, so a wrong, trimmed or damaged image is named on a clear screen
 * instead of crashing an hour in. Prime-X's scheme (its docs/boot-safety.md),
 * fitted to AC's disc: ten files, ~27 MB.
 *
 * Every boot (well under a second): the ID and revision; every file the
 * game needs is in the FST at its size; the DOL's CRC.
 * The first boot of an image (or Y + both triggers held at boot): the CRC32
 * of every file against xbox/src/disc_table.inc, behind the splash, B
 * skipping it for that boot. A pass is
 * remembered in E:\UDATA\4f430001\disc.ini by the image's name, size and
 * write time; a skip or a mismatch checks again next boot.
 *
 * Contents, not layout: a scrubbed .iso, a trimmed .ciso and a rebuilt
 * image all hold the same files at other offsets (tools/disc_file_table.py).
 * Kill switch: -DXBOX_DISC_CHECK=0 (the ID check stays). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "pc_disc.h"
#include "xbox_io.h"
#include "xbox_splash.h"

#ifndef XBOX_DISC_CHECK
#define XBOX_DISC_CHECK 1
#endif

/* the full letter, every stamp on it, stays this long after the last file
 * (any button cuts it short): the check itself takes about a second */
#ifndef XBOX_DISC_CHECK_HOLD_MS
#define XBOX_DISC_CHECK_HOLD_MS 700
#endif

void xbox_quit_to_dashboard(void);

typedef struct { const char* path; unsigned size, crc; } DiscFile;
typedef struct { const char* id; int rev; const char* name; } DiscRelease;
#include "disc_table.inc"

#define CHUNK (256 * 1024)
#define DISC_INI XBOX_UDATA_DIR "disc.ini"

static unsigned s_crc_table[256];

static void crc_init(void) {
    unsigned i, k, c;
    for (i = 0; i < 256; i++) {
        for (c = i, k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        s_crc_table[i] = c;
    }
}

static unsigned crc_update(unsigned c, const unsigned char* p, unsigned n) {
    c = ~c;
    while (n--) c = s_crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static unsigned be32(const unsigned char* p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static void fatal(const char* title, const char* l0, const char* l1, const char* l2) {
    static const char* lines[8];
    int n = 0;
    lines[n++] = l0;
    if (l1) lines[n++] = l1;
    if (l2) lines[n++] = l2;
    lines[n++] = "";
    lines[n++] = "OpenCrossing needs Animal Crossing USA (GAFE01) Rev 0,";
    lines[n++] = "a clean 1:1 dump of your own disc: .iso, .gcm or .ciso.";
    lines[n] = NULL;
    xbox_splash_error(title, lines);
}

/* ---- every boot ---- */

static void check_id(void) {
    unsigned char h[8];
    static char l0[96];
    size_t i;
    if (!pc_disc_read(0, h, 8)) fatal("Could not read the disc image", "The first sector didn't read.", NULL, NULL);
    if (memcmp(h, "GAFE01", 6) == 0 && h[7] == 0) return;
    xbox_logf("[DISC] id %.6s rev %u: not GAFE01 rev 0\n", (const char*)h, h[7]);
    for (i = 0; i < sizeof k_disc_others / sizeof k_disc_others[0]; i++)
        if (memcmp(h, k_disc_others[i].id, 6) == 0 && h[7] == k_disc_others[i].rev) {
            snprintf(l0, sizeof l0, "This is Animal Crossing %s (%.6s).", k_disc_others[i].name, (const char*)h);
            fatal("Wrong version of the game", l0, NULL, NULL);
        }
    snprintf(l0, sizeof l0, "This image says it is %.6s rev %u, not Animal Crossing.", (const char*)h, h[7]);
    fatal("Not Animal Crossing", l0, NULL, NULL);
}

static unsigned dol_crc(unsigned char* buf) {
    unsigned char h[0x100];
    unsigned off, size = 0, done, c = 0, i;
    if (!pc_disc_read(0x420, h, 4)) return 0;
    off = be32(h);
    if (!pc_disc_read(off, h, 0x100)) return 0;
    for (i = 0; i < 18; i++) {
        unsigned o = be32(h + i * 4), s = be32(h + 0x90 + i * 4);
        if (s && o + s > size) size = o + s;
    }
    if (size != DISC_DOL_SIZE) {
        xbox_logf("[DISC] DOL is %u bytes, a good one %u\n", size, DISC_DOL_SIZE);
        return 0;
    }
    for (done = 0; done < size; done += CHUNK) {
        unsigned n = size - done < CHUNK ? size - done : CHUNK;
        if (!pc_disc_read(off + done, buf, n)) return 0;
        c = crc_update(c, buf, n);
    }
    return c;
}

static void check_layout(unsigned char* buf) {
    unsigned i, off, size, c;
    static char l0[96];
    for (i = 0; i < DISC_FILE_COUNT; i++) {
        if (!pc_disc_find_file(k_disc_files[i].path, &off, &size)) {
            xbox_logf("[DISC] %s: missing\n", k_disc_files[i].path);
            snprintf(l0, sizeof l0, "%s is missing from it.", k_disc_files[i].path);
            fatal("This image has been changed", l0, "(a mod, NKit, or a copy trimmed too far)", NULL);
        }
        if (size != k_disc_files[i].size) {
            xbox_logf("[DISC] %s: %u bytes, a good copy %u\n", k_disc_files[i].path, size, k_disc_files[i].size);
            snprintf(l0, sizeof l0, "%s is %u bytes, not %u.", k_disc_files[i].path, size, k_disc_files[i].size);
            fatal("This image has been changed", l0, "(a mod, NKit, or a copy trimmed too far)", NULL);
        }
    }
    c = dol_crc(buf);
    if (c != DISC_DOL_CRC) {
        xbox_logf("[DISC] DOL crc %08x, a good copy %08x\n", c, DISC_DOL_CRC);
        fatal("This image has been changed", "Its game code (main.dol) differs from a good copy.",
              "(a mod, a patch, or a damaged copy)", NULL);
    }
}

/* ---- disc.ini: the images that passed ---- */

/* name, size and write time; 0 when the kernel can't say, which never
 * matches a stored key: the full check runs (the image sits next to
 * default.xbe, on D:, and the kernel calls need the full path) */
static int image_key(const char* name, char* key, size_t cap) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    char path[300];
    snprintf(path, sizeof path, "D:\\%s", name);
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) {
        xbox_logf("[DISC] no size or time for %s (error %lu): checking every boot\n", path,
                  (unsigned long)GetLastError());
        snprintf(key, cap, "%s 0 0", name);
        return 0;
    }
    snprintf(key, cap, "%s %lu %08lx%08lx", name, (unsigned long)a.nFileSizeLow,
             (unsigned long)a.ftLastWriteTime.dwHighDateTime, (unsigned long)a.ftLastWriteTime.dwLowDateTime);
    return 1;
}

static int passed_before(const char* key) {
    char line[400];
    FILE* f = fopen(DISC_INI, "r");
    int hit = 0;
    if (!f) return 0;
    while (!hit && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        hit = strncmp(line, "passed = ", 9) == 0 && strcmp(line + 9, key) == 0;
    }
    fclose(f);
    return hit;
}

static void remember_pass(const char* key) {
    FILE* f = fopen(DISC_INI, "w");   /* one image at a time: the last one that passed */
    if (!f) return;
    fprintf(f, "; OpenCrossing disc check: the image that passed the full check\n");
    fprintf(f, "; (name, size, write time). Delete this file to check again.\n");
    fprintf(f, "passed = %s\n", key);
    fclose(f);
}

/* ---- the first boot's full check ---- */

/* -1 skipped (B), else the number of files that differ. On the letter:
 * one stamp box per file (the disc has ten), the file and its CRC under
 * them, then the bar comes back for the rest of the load. */
static int full_check(unsigned char* buf, unsigned* first_bad) {
    unsigned total = 0, done = 0, i, bad = 0;
    DWORD t0 = GetTickCount();
    char task[80], detail[80];
    for (i = 0; i < DISC_FILE_COUNT; i++) total += k_disc_files[i].size;
    xbox_splash_letter(1);
    xbox_splash_text("Checking your copy of the game...", "");
    for (i = 0; i < DISC_FILE_COUNT; i++) {
        const DiscFile* df = &k_disc_files[i];
        unsigned off, size, pos, c = 0;
        pc_disc_find_file(df->path, &off, &size);
        for (pos = 0; pos < size; pos += CHUNK) {
            unsigned n = size - pos < CHUNK ? size - pos : CHUNK;
            if (!pc_disc_read(off + pos, buf, n)) {
                static char l0[96];
                xbox_logf("[DISC] read error in %s at %u\n", df->path, pos);
                snprintf(l0, sizeof l0, "Reading %s failed at byte %u.", df->path, pos);
                fatal("Could not read the disc image", l0, "A hard drive or disc fault, or a copy cut short.", NULL);
            }
            c = crc_update(c, buf, n);
            done += n;
            snprintf(task, sizeof task, "Checking your copy of the game... %u%%", (unsigned)((double)done * 100 / total));
            xbox_splash_text(task, NULL);
            if (xbox_splash_button(SDL_CONTROLLER_BUTTON_B)) {
                xbox_logf("[DISC] full check skipped (B) at %s\n", df->path);
                xbox_splash_text("Check skipped: it runs again next time.", "Loading your town...");
                xbox_splash_letter(0);
                return -1;
            }
        }
        snprintf(detail, sizeof detail, "%-16s %08X  %s", df->path, c, c == df->crc ? "OK" : "DIFFERS");
        xbox_splash_text(NULL, detail);
        xbox_splash_stamp((int)i, c == df->crc);   /* a file that differs is crossed out */
        if (c != df->crc) {
            xbox_logf("[DISC] %s: crc %08x, a good copy %08x\n", df->path, c, df->crc);
            if (!bad++) *first_bad = i;
        }
    }
    xbox_splash_text(bad ? "Some files don't match a good copy." : "All stamped! Your game is in one piece.", NULL);
    /* let the last stamp land; any button moves on */
    {
        DWORD t1 = GetTickCount();
        while (GetTickCount() - t1 < XBOX_DISC_CHECK_HOLD_MS && !xbox_splash_button(-1)) Sleep(16);
    }
    xbox_logf("[DISC] full check: %u files, %d differ, %lu ms\n", DISC_FILE_COUNT, bad,
              (unsigned long)(GetTickCount() - t0));
    xbox_splash_text(NULL, "Loading your town...");
    xbox_splash_letter(0);
    return bad;
}

void xbox_disc_check(const char* image_name) {
    unsigned char* buf;
    char key[320];
    unsigned first_bad = 0;
    int bad, keyed;
    DWORD t0 = GetTickCount();
    check_id();
    if (!XBOX_DISC_CHECK) return;
#ifdef XBOX_DBG_FORCE_DISC_CHECK   /* test runs: the full check every boot */
    g_xbox_force_disc_check = 1;
#endif
    crc_init();
    buf = (unsigned char*)malloc(CHUNK);
    if (!buf) return;
    check_layout(buf);
    xbox_logf("[DISC] quick check ok (%lu ms)\n", (unsigned long)(GetTickCount() - t0));
    keyed = image_key(image_name, key, sizeof key);
    if (keyed && passed_before(key) && !g_xbox_force_disc_check) {
        xbox_logf("[DISC] full check passed before for %s\n", key);
        free(buf);
        return;
    }
    if (g_xbox_force_disc_check) xbox_logf("[DISC] Y + triggers held at boot: full check\n");
    else xbox_logf("[DISC] first boot of %s: full check\n", key);
    bad = full_check(buf, &first_bad);
    free(buf);
    if (bad == 0) {
        if (keyed) remember_pass(key);
        return;
    }
    if (bad > 0) {
        static char l0[96];
        static const char* lines[] = { NULL, "", "The game may crash in the parts that use them.",
                                       "Re-dump or re-copy the image to fix it.", NULL };
        snprintf(l0, sizeof l0, "%d file%s from a good copy (first: %s).", bad, bad > 1 ? "s differ" : " differs",
                 k_disc_files[first_bad].path);
        lines[0] = l0;
        if (!xbox_splash_ask("This copy doesn't match", lines)) xbox_quit_to_dashboard();
    }
}
