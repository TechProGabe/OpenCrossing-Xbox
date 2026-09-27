/* xbox_io.c — file paths and logging for the Xbox build.
 *
 * PATHS. pc/ uses relative paths ("save/card_a", "settings.ini", "." for the
 * disc scan). The Xbox has no working directory, so every relative path is
 * resolved here (docs/PLAN.md "Distribution"):
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

static void com1_write(const char* s, size_t n) {
    size_t i;
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

void xbox_log_write(const char* s, size_t n) {
    DWORD owner = s_log_owner;
    if (owner && owner != GetCurrentThreadId()) return;
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

int xbox_rename(const char* from, const char* to) {
    char a[MAX_PATH], b[MAX_PATH];
    xbox_resolve(from, XBOX_PATH_WRITE, a, sizeof a);
    xbox_resolve(to, XBOX_PATH_WRITE, b, sizeof b);
    /* Win32 MoveFile won't replace; POSIX rename does. */
    DeleteFileA(b);
    return MoveFileA(a, b) ? 0 : -1;
}
