/* xbox_io.c — file paths and logging for the Xbox build.
 *
 * PATHS. pc/ uses relative paths ("save/card_a", "settings.ini", "." for the
 * disc scan). The Xbox has no working directory, so every relative path is
 * resolved here (docs/architecture.md "Files on the console"):
 *   - reads of the disc image, and directory scans of "."  -> D:\ (the XBE's
 *     own folder: HDD install, burned DVD or xemu disc alike)
 *   - everything the game writes (saves, settings)         -> E:\UDATA\<id>\
 *   - a read of any other file tries UDATA first, then D:\
 * xbox_prelude.h routes fopen/remove/rename here for C TUs.
 *
 * LOGGING. pdclib's stdout/stderr are dead handles on nxdk. printf-family
 * calls from C TUs are routed to COM1 (0x3F8), which xemu exposes with
 * `-device lpc47m157 -serial ...` and real hardware ignores (no SuperIO on a
 * retail board: the LSR reads 0xFF, so the busy-wait never spins). */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_io.h"

#undef fopen
#undef remove
#undef rename
#undef printf
#undef vprintf
#undef fprintf
#undef vfprintf
#undef puts
#undef fread
#undef fclose

int g_xbox_log = XBOX_LOG_DEFAULT;
/* nonzero = only this thread may log (fbdump holds it so other threads can't
 * splice text into the middle of a base64 line) */
static volatile DWORD s_log_owner;

void xbox_log_exclusive(int on) { s_log_owner = on ? GetCurrentThreadId() : 0; }

/* ---- COM1 ---- */
static inline unsigned char port_in(unsigned short p) {
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void port_out(unsigned short p, unsigned char v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p));
}

/* Is there a 16550 at 0x3F8? xemu's lpc47m157 and debug kits have one; a
 * retail board does not, and what an absent port reads back is up to the
 * board and modchip. If the LSR never shows "empty", every byte would spin
 * out its full timeout (~0.1 s) and a boot's logging takes many minutes, so
 * probe the scratch register once and stay silent when nothing answers. */
static int s_com1 = -1;
static int com1_present(void) {
    if (s_com1 < 0) {
        port_out(0x3F8 + 7, 0x5A);
        s_com1 = port_in(0x3F8 + 7) == 0x5A;
        port_out(0x3F8 + 7, 0xA5);
        s_com1 = s_com1 && port_in(0x3F8 + 7) == 0xA5;
    }
    return s_com1;
}

static void com1_write(const char* s, size_t n) {
    size_t i;
    if (!com1_present()) return;
    for (i = 0; i < n; i++) {
        int spin = 100000;
        if (s[i] == '\n') {
            while (!(port_in(0x3F8 + 5) & 0x20) && --spin) {}
            port_out(0x3F8, '\r');
            spin = 100000;
        }
        while (!(port_in(0x3F8 + 5) & 0x20) && --spin) {}
        port_out(0x3F8, (unsigned char)s[i]);
    }
}

/* ---- log tail ring (the watchdog shows it on screen) ---- */
#define TAIL_SIZE 4096
static char s_tail[TAIL_SIZE];
static volatile unsigned s_tail_pos;

static void tail_write(const char* s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) s_tail[(s_tail_pos + i) % TAIL_SIZE] = s[i];
    s_tail_pos += (unsigned)n;
}

unsigned xbox_log_pos(void) { return s_tail_pos; }

size_t xbox_log_tail(char* out, size_t cap) {
    unsigned end = s_tail_pos, len = end < TAIL_SIZE ? end : TAIL_SIZE, i;
    if (len > cap - 1) len = (unsigned)cap - 1;
    for (i = 0; i < len; i++) out[i] = s_tail[(end - len + i) % TAIL_SIZE];
    out[len] = '\0';
    return len;
}

/* ---- boot log file: E:\UDATA\4f430001\boot.log ----
 * Real hardware has no serial port, so everything logged until the game has
 * shown XBOX_BOOTLOG_FRAMES frames also goes to a file on the HDD, flushed
 * per write so a hard freeze still leaves the last line on disk. */
static HANDLE s_bootlog = INVALID_HANDLE_VALUE;

void xbox_bootlog_open(void) {
    s_bootlog = CreateFileA(XBOX_UDATA_DIR "boot.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
}

void xbox_bootlog_close(void) {
    HANDLE h = s_bootlog;
    s_bootlog = INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

/* nxdk's winapi has no FlushFileBuffers; its HANDLEs are NT handles */
void xbox_flush_file(HANDLE h) {
    IO_STATUS_BLOCK iosb;
    NtFlushBuffersFile(h, &iosb);
}

static void bootlog_write(const char* s, size_t n) {
    DWORD w;
    HANDLE h = s_bootlog;
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, s, (DWORD)n, &w, NULL);
    xbox_flush_file(h);
}

void xbox_log_write(const char* s, size_t n) {
    DWORD owner = s_log_owner;
    if (owner && owner != GetCurrentThreadId()) return;
    tail_write(s, n);
    bootlog_write(s, n);
    if (g_xbox_log) com1_write(s, n);
}

int xbox_vlogf(const char* fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0) return n;
    xbox_log_write(buf, (size_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
    return n;
}

int xbox_logf(const char* fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = xbox_vlogf(fmt, ap);
    va_end(ap);
    return n;
}

int xbox_printf(const char* fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = xbox_vlogf(fmt, ap);
    va_end(ap);
    return n;
}

int xbox_vprintf(const char* fmt, va_list ap) { return xbox_vlogf(fmt, ap); }

int xbox_vfprintf(FILE* f, const char* fmt, va_list ap) {
    if (f == stdout || f == stderr) return xbox_vlogf(fmt, ap);
    return vfprintf(f, fmt, ap);
}

int xbox_fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = xbox_vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int xbox_puts(const char* s) {
    xbox_log_write(s, strlen(s));
    xbox_log_write("\n", 1);
    return 0;
}

/* MSVC secure variant referenced by glad's _MSC_VER path. */
int sscanf_s(const char* s, const char* fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsscanf(s, fmt, ap);
    va_end(ap);
    return n;
}

char* getcwd(char* buf, size_t size) {
    if (!buf || size < 4) return NULL;
    strcpy(buf, "D:\\");
    return buf;
}

/* ---- hitch stats ---- */
XboxFrameStats g_xfs;

unsigned long long xbox_ticks(void) { return KeQueryPerformanceCounter(); }
unsigned long long xbox_ticks_per_sec(void) { return KeQueryPerformanceFrequency(); }

size_t xbox_fread(void* buf, size_t size, size_t n, FILE* f) {
    unsigned long long t0 = xbox_ticks();
    size_t r = fread(buf, size, n, f);
    g_xfs.fread_ticks += xbox_ticks() - t0;
    g_xfs.fread_bytes += (unsigned long long)r * size;
    g_xfs.fread_n++;
    return r;
}

/* ---- memory ---- */
void xbox_mem_log(const char* where) {
    MM_STATISTICS st;
    memset(&st, 0, sizeof st);
    st.Length = sizeof st;
    if (MmQueryStatistics(&st) >= 0)
        xbox_logf("[MEM] %-18s free %5u KB of %5u KB (image %u KB, virt %u KB, pool %u KB)\n", where,
                  (unsigned)(st.AvailablePages * 4), (unsigned)(st.TotalPhysicalPages * 4),
                  (unsigned)(st.ImagePagesCommitted * 4), (unsigned)(st.VirtualMemoryBytesCommitted / 1024),
                  (unsigned)(st.PoolPagesCommitted * 4));
}

unsigned xbox_mem_free_kb(void) {
    MM_STATISTICS st;
    memset(&st, 0, sizeof st);
    st.Length = sizeof st;
    return MmQueryStatistics(&st) >= 0 ? (unsigned)(st.AvailablePages * 4) : 0;
}

/* Is every page of [p, p + size) mapped? Heap memory the game has freed is
 * decommitted on the Xbox, where a PC keeps it readable. */
int xbox_ptr_readable(const void* p, unsigned size) {
    uintptr_t a = (uintptr_t)p, end;
    if (!p) return 0;
    if (!size) size = 1;
    end = a + size - 1;
    if (end < a) return 0;
    for (a &= ~(uintptr_t)4095; a <= end; a += 4096)
        if (!MmIsAddressValid((PVOID)a)) return 0;
    return 1;
}

int xbox_tex_ptr_ok(const void* p, int w, int h, int bpp, unsigned fmt) {
    static unsigned logged;
    unsigned size = (unsigned)(w > 0 ? w : 1) * (unsigned)(h > 0 ? h : 1) * (unsigned)bpp / 8;
    if (xbox_ptr_readable(p, size)) return 1;
    if (logged++ < 16)
        xbox_logf("[TEX] texture image at %p (%dx%d fmt %u, %u bytes) is not mapped: drawn without it\n", p, w, h,
                  fmt, size);
    return 0;
}

/* ---- paths ---- */
static int is_absolute(const char* p) {
    return p[0] && p[1] == ':';
}

static void join(char* out, size_t cap, const char* base, const char* rel) {
    size_t i = 0, j;
    while (rel[0] == '.' && (rel[1] == '/' || rel[1] == '\\')) rel += 2;
    if (strcmp(rel, ".") == 0) rel = "";
    for (j = 0; base[j] && i + 1 < cap; j++) out[i++] = base[j];
    for (j = 0; rel[j] && i + 1 < cap; j++) out[i++] = rel[j] == '/' ? '\\' : rel[j];
    out[i] = '\0';
    /* strip a trailing backslash unless it's the drive root ("D:\") */
    if (i > 3 && out[i - 1] == '\\') out[i - 1] = '\0';
}

static int file_exists(const char* p) {
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

const char* xbox_resolve(const char* in, int mode, char* out, size_t cap) {
    size_t i;
    if (!in) return in;
    if (is_absolute(in)) {
        for (i = 0; in[i] && i + 1 < cap; i++) out[i] = in[i] == '/' ? '\\' : in[i];
        out[i] = '\0';
        return out;
    }
    switch (mode) {
        case XBOX_PATH_DISC:
            join(out, cap, XBOX_DISC_DIR, in);
            break;
        case XBOX_PATH_WRITE:
            join(out, cap, XBOX_UDATA_DIR, in);
            break;
        default: /* XBOX_PATH_READ */
#ifdef XBOX_DBG_SAVE_FROM_D
            /* test runs: a save packed on the disc wins over the HDD copy a
             * previous run left (harness OCX_STAGE_EXTRA) */
            join(out, cap, XBOX_DISC_DIR, in);
            if (strncmp(in, "save/", 5) == 0 && file_exists(out)) break;
#endif
            join(out, cap, XBOX_UDATA_DIR, in);
            if (!file_exists(out)) join(out, cap, XBOX_DISC_DIR, in);
            break;
    }
    return out;
}

static int mode_writes(const char* m) {
    return strchr(m, 'w') || strchr(m, 'a') || strchr(m, '+');
}

FILE* xbox_fopen(const char* path, const char* mode) {
    char p[MAX_PATH];
    return fopen(xbox_resolve(path, mode_writes(mode) ? XBOX_PATH_WRITE : XBOX_PATH_READ, p, sizeof p), mode);
}

int xbox_remove(const char* path) {
    char p[MAX_PATH];
    return remove(xbox_resolve(path, XBOX_PATH_WRITE, p, sizeof p));
}

/* Saves must survive a power-off or IGR right after they are written: FATX
 * caches both data and directory entries, and Resetti's "quit without saving"
 * detection is exactly a save written at load time (pc_m_card.c arms the
 * reset code and persists it). fclose flushes the file itself; rename (the
 * save's temp -> real swap) flushes the whole volume, which covers the
 * directory entries too. */
/* FILE is nxdk pdclib's struct _PDCLIB_file_t (pdclib/_PDCLIB_int.h), whose
 * first member is the kernel file handle (_PDCLIB_fd_t = void* on xbox). */
_Static_assert(sizeof(((struct _PDCLIB_file_t*)0)->handle) == sizeof(HANDLE), "pdclib FILE handle is not a HANDLE");

int xbox_fclose(FILE* f) {
    HANDLE h;
    if (!f) return EOF;
    fflush(f);
    h = (HANDLE)((struct _PDCLIB_file_t*)f)->handle;
    if (h && h != INVALID_HANDLE_VALUE) xbox_flush_file(h);
    return fclose(f);
}

static void flush_volume(char drive) {
    char path[] = "\\??\\X:";
    ANSI_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    path[4] = drive;
    RtlInitAnsiString(&name, path);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(NtOpenFile(&h, GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_SYNCHRONOUS_IO_NONALERT))) {
        NtFlushBuffersFile(h, &iosb);
        NtClose(h);
    }
}

int xbox_rename(const char* from, const char* to) {
    char a[MAX_PATH], b[MAX_PATH];
    int ok;
    xbox_resolve(from, XBOX_PATH_WRITE, a, sizeof a);
    xbox_resolve(to, XBOX_PATH_WRITE, b, sizeof b);
    /* Win32 MoveFile won't replace; POSIX rename does. */
    DeleteFileA(b);
    ok = MoveFileA(a, b);
    /* xbox_resolve always NUL-terminates; resolved HDD paths are "E:\\..." */
    if (b[0] && b[1] == ':') flush_volume(b[0]);
    return ok ? 0 : -1;
}
