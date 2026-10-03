/* xbox_io.c — file paths and logging for the Xbox build.
 *
 * PATHS. pc/ uses relative paths ("save/card_a", "settings.ini", "." for the
 * disc scan). The Xbox has no working directory, so every relative path is
 * resolved here (docs/architecture.md "Files on the console"):
 *   - reads of the disc image, and directory scans of "."  -> D:\ (the XBE's
 *     own folder: HDD install, burned DVD or xemu disc alike)
 *   - everything the game writes (saves, settings)         -> E:\UDATA\<id>\
 *   - a read of any other file tries UDATA first, then D:\
 * xbox_prelude.h routes fopen/remove/rename/fread/fclose here for C TUs, and
 * pc_disc.c's fread/fseek to xbox_disc_fread/xbox_disc_fseek (below).
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

/* Heartbeat lines ([BEAT], [FRAME], [PROF]) are logged through
 * xbox_logf_quiet: they go everywhere a line goes, but xbox_log_pos_loud
 * doesn't move, so the watchdog doesn't rewrite last.log for them (a line a
 * few seconds would otherwise keep it flushing the HDD all session). */
static volatile unsigned s_quiet_bytes;
unsigned xbox_log_pos_loud(void) { return s_tail_pos - s_quiet_bytes; }

size_t xbox_log_tail(char* out, size_t cap) {
    unsigned end = s_tail_pos, len = end < TAIL_SIZE ? end : TAIL_SIZE, i;
    if (len > cap - 1) len = (unsigned)cap - 1;
    for (i = 0; i < len; i++) out[i] = s_tail[(end - len + i) % TAIL_SIZE];
    out[len] = '\0';
    return len;
}

/* ---- boot log file: E:\UDATA\4f430001\boot.log ----
 * Real hardware has no serial port, so the log also goes to a file on the
 * HDD. Until the game has shown 120 frames every write is flushed at once,
 * so a hang during boot still leaves its last line on disk. After that
 * (xbox_bootlog_async, from Melee-X: its logs cover whole sessions) the game
 * thread only queues lines in memory, and the watchdog thread writes and
 * flushes them once a second (xbox_bootlog_pump): a per-line flush costs
 * ~45 ms on hardware. boot.log keeps the first 4 MB; after that the log
 * goes on in boot2.log and boot3.log in turn, each restarted at 2 MB, so the
 * newest 2-4 MB before a late hang survive. Kill switch:
 * -DXBOX_LOG_SESSION=0 (boot.log closes at frame 120, as before). */
#ifndef XBOX_LOG_SESSION
#define XBOX_LOG_SESSION 1
#endif
#define BOOTLOG_FIRST_MAX (4u << 20)
#define BOOTLOG_NEXT_MAX (2u << 20)
#define PEND_BYTES (128 * 1024)
static HANDLE s_bootlog = INVALID_HANDLE_VALUE;
static volatile int s_bootlog_async;
static char s_pend[2][PEND_BYTES];
static unsigned s_pend_len[2], s_pend_cur, s_pend_drops;
static RTL_CRITICAL_SECTION s_pend_lock;
static unsigned s_file_bytes, s_file_no = 1;   /* boot.log = 1, then 2, 3, 2, ... */
static volatile int s_pump_seen;   /* the watchdog called xbox_bootlog_pump */
static RTL_CRITICAL_SECTION s_pump_lock;   /* one writer at a time (watchdog, quit) */
/* Lines of these kinds wake the watchdog to write the queue at once rather
 * than within the second: they come right before a user turns the console
 * off (an error on screen, a quit), and the NES card error of v2 left no
 * line at all. */
static HANDLE s_pump_event;
static const char* const k_urgent[] = { "[NES]", "[AUDIO]", "[CARD]", "[VIDEO]", "[XBOX]", "[CRASH]", "[WDOG]", "[NV2A] GPU" };
void* xbox_bootlog_event(void) { return s_pump_event; }
static int urgent(const char* s, size_t n) {
    size_t i, k;
    for (i = 0; i < sizeof k_urgent / sizeof k_urgent[0]; i++) {
        k = strlen(k_urgent[i]);
        if (n >= k && memcmp(s, k_urgent[i], k) == 0) return 1;
    }
    return 0;
}

/* The previous boot's logs are kept as *_prev.log (from Melee-X): a restart
 * from the Options menu, or a relaunch to try again, used to delete the
 * logs of the boot that had the problem. One generation only. */
static void keep_prev(const char* name) {
    char from[64], to[64];
    snprintf(from, sizeof from, XBOX_UDATA_DIR "%s.log", name);
    snprintf(to, sizeof to, XBOX_UDATA_DIR "%s_prev.log", name);
    /* only when there is a new one: a crash.log stays (as crash_prev.log)
     * until the next crash, however many clean boots come between */
    if (GetFileAttributesA(from) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(to);
    MoveFileA(from, to);
}

void xbox_bootlog_open(void) {
    static const char* const logs[] = { "boot", "boot2", "boot3", "last", "perf", "hang", "crash" };
    int i;
    RtlInitializeCriticalSection(&s_pend_lock);
    RtlInitializeCriticalSection(&s_pump_lock);
    s_pump_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (i = 0; i < (int)(sizeof logs / sizeof logs[0]); i++) keep_prev(logs[i]);
    s_bootlog = CreateFileA(XBOX_UDATA_DIR "boot.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
}

void xbox_bootlog_close(void) {
    HANDLE h = s_bootlog;
    s_bootlog = INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

/* from here on, writes are queued for xbox_bootlog_pump. With the kill
 * switch, or no watchdog to pump them (-DXBOX_WATCHDOG=0), boot.log is
 * closed instead, as before. */
void xbox_bootlog_async(void) {
    if (!XBOX_LOG_SESSION || !s_pump_seen) {
        xbox_bootlog_close();
        return;
    }
    s_bootlog_async = 1;
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
    if (s_bootlog_async) {
        unsigned c;
        RtlEnterCriticalSection(&s_pend_lock);
        c = s_pend_cur;
        if (s_pend_len[c] + n <= PEND_BYTES) {
            memcpy(s_pend[c] + s_pend_len[c], s, n);
            s_pend_len[c] += (unsigned)n;
        } else {
            s_pend_drops++;
        }
        RtlLeaveCriticalSection(&s_pend_lock);
        if (s_pump_event && urgent(s, n)) {
            /* at most 10 early pumps a second: a GPU fault every frame
             * would otherwise keep the watchdog writing to the disk */
            static DWORD s_last_wake;
            DWORD now = GetTickCount();
            if (now - s_last_wake >= 100) {
                s_last_wake = now;
                SetEvent(s_pump_event);
            }
        }
        return;
    }
    WriteFile(h, s, (DWORD)n, &w, NULL);
    xbox_flush_file(h);
    s_file_bytes += (unsigned)n;
}

/* the watchdog thread, once a second or woken by an urgent line, and a quit
 * or restart before it leaves: write what was queued, flush once */
static void bootlog_pump_locked(void);
void xbox_bootlog_pump(void) {
    s_pump_seen = 1;
    if (!s_bootlog_async) return;
    RtlEnterCriticalSection(&s_pump_lock);
    bootlog_pump_locked();
    RtlLeaveCriticalSection(&s_pump_lock);
}

static void bootlog_pump_locked(void) {
    unsigned c, len, drops;
    DWORD w;
    if (s_bootlog == INVALID_HANDLE_VALUE) return;
    RtlEnterCriticalSection(&s_pend_lock);
    c = s_pend_cur;
    len = s_pend_len[c];
    drops = s_pend_drops;
    s_pend_drops = 0;
    s_pend_cur = c ^ 1;
    s_pend_len[c ^ 1] = 0;
    RtlLeaveCriticalSection(&s_pend_lock);
    if (!len && !drops) return;
    if (s_file_bytes + len > (s_file_no == 1 ? BOOTLOG_FIRST_MAX : BOOTLOG_NEXT_MAX)) {
        char name[64];
        s_file_no = s_file_no == 2 ? 3 : 2;
        snprintf(name, sizeof name, XBOX_UDATA_DIR "boot%u.log", s_file_no);
        CloseHandle(s_bootlog);
        s_bootlog = CreateFileA(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        s_file_bytes = 0;
        if (s_bootlog == INVALID_HANDLE_VALUE) return;
    }
    if (len) WriteFile(s_bootlog, s_pend[c], len, &w, NULL);
    if (drops) {
        char line[64];
        int n = snprintf(line, sizeof line, "[LOG] %u lines not kept (queue full)\n", drops);
        WriteFile(s_bootlog, line, (DWORD)n, &w, NULL);
        len += (unsigned)n;
    }
    xbox_flush_file(s_bootlog);
    s_file_bytes += len;
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

void xbox_log_write_quiet(const char* s, size_t n) {
    unsigned pos = s_tail_pos;
    xbox_log_write(s, n);
    s_quiet_bytes += s_tail_pos - pos;
}

int xbox_logf_quiet(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return n;
    xbox_log_write_quiet(buf, (size_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
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

/* CPU upgrades (GitHub #2: a 1 GHz swap). nxdk's QueryPerformanceCounter
 * (SDL's, so pc_os.c's game clock and pc_vi.c's limiter) counts CPU cycles;
 * nxdk derives its frequency from the CPU multiplier and FSB but falls back
 * to 733 MHz for CPUs missing from its table. The kernel's counter
 * (xbox_ticks) is the ACPI timer, 3.375 MHz from a crystal, whatever the CPU.
 * Once at boot the CPU counter is timed against it for 100 ms, and if nxdk's
 * frequency is more than 3% off the measured one replaces it; otherwise
 * nothing changes. (Not against KeQueryInterruptTime: in xemu that runs 7%
 * apart from both counters.) Kill switch: -DXBOX_CLOCK_CHECK=0. */
#ifndef XBOX_CLOCK_CHECK
#define XBOX_CLOCK_CHECK 1
#endif
static unsigned long long s_qpc_hz;

void xbox_clock_check(void) {
    LARGE_INTEGER qf, q0, q1;
    unsigned long long ke = KeQueryPerformanceFrequency(), k0, k1, meas, diff;
    QueryPerformanceFrequency(&qf);
    s_qpc_hz = (unsigned long long)qf.QuadPart;
    if (!XBOX_CLOCK_CHECK || !ke) return;
    k0 = KeQueryPerformanceCounter();
    QueryPerformanceCounter(&q0);
    do {
        k1 = KeQueryPerformanceCounter();
    } while (k1 - k0 < ke / 10);
    QueryPerformanceCounter(&q1);
    meas = (unsigned long long)(q1.QuadPart - q0.QuadPart) * ke / (k1 - k0);
    diff = meas > s_qpc_hz ? meas - s_qpc_hz : s_qpc_hz - meas;
    if (diff * 100 > s_qpc_hz * 3) s_qpc_hz = meas;
    xbox_logf("[CLOCK] CPU %llu.%llu MHz (nxdk says %llu.%llu)%s\n", meas / 1000000, meas / 100000 % 10,
              (unsigned long long)qf.QuadPart / 1000000, (unsigned long long)qf.QuadPart / 100000 % 10,
              s_qpc_hz != (unsigned long long)qf.QuadPart ? ": timers use the measured clock" : "");
}

unsigned long long xbox_ticks(void) { return KeQueryPerformanceCounter(); }
unsigned long long xbox_ticks_per_sec(void) { return KeQueryPerformanceFrequency(); }

/* SDL_GetPerformanceFrequency for pc_os.c, pc_vi.c, pc_profiler.c
 * (xbox/CMakeLists.txt): the counter is SDL's own */
unsigned long long xbox_perf_frequency(void) {
    if (!s_qpc_hz) {
        LARGE_INTEGER qf;
        QueryPerformanceFrequency(&qf);
        return (unsigned long long)qf.QuadPart;
    }
    return s_qpc_hz;
}

size_t xbox_fread(void* buf, size_t size, size_t n, FILE* f) {
    unsigned long long t0 = xbox_ticks();
    size_t r = fread(buf, size, n, f);
    g_xfs.fread_ticks += xbox_ticks() - t0;
    g_xfs.fread_bytes += (unsigned long long)r * size;
    g_xfs.fread_n++;
    return r;
}

/* The disc image (pc_disc.c only: it is built with XBOX_DISC_TU, and the
 * prelude maps its fread/fseek here) reads without pdclib. pdclib's fread
 * refills a 1 KB buffer with one NtReadFile per KB and copies byte by byte,
 * and every disc read starts with an fseek that throws the buffer away:
 * ~2.5 MB/s on hardware (648 KB took 264 ms). On a cold boot the first title
 * demo's music misses xbox_aram.c's block cache block after block; the audio
 * producer fell behind and took the CPU back from the game thread (66-83 ms
 * frames for ~15 s). Here fseek moves the handle's own file pointer and fread
 * is one ReadFile straight into the caller's buffer (pdclib's handle is
 * synchronous: CreateFileA without FILE_FLAG_OVERLAPPED). pc_disc.c uses
 * nothing else on the stream, never writes it, and pc_disc_read's mutex
 * serialises each fseek/fread pair. Kill switch: -DXBOX_DISC_DIRECT=0. */
#ifndef XBOX_DISC_DIRECT
#define XBOX_DISC_DIRECT 1
#endif
static HANDLE file_handle(FILE* f) { return (HANDLE)((struct _PDCLIB_file_t*)f)->handle; }

int xbox_disc_fseek(FILE* f, long off, int whence) {
    DWORD how = whence == SEEK_SET ? FILE_BEGIN : whence == SEEK_CUR ? FILE_CURRENT : FILE_END;
    if (!XBOX_DISC_DIRECT) return fseek(f, off, whence);
    return SetFilePointer(file_handle(f), off, NULL, how) == INVALID_SET_FILE_POINTER ? -1 : 0;
}

size_t xbox_disc_fread(void* buf, size_t size, size_t n, FILE* f) {
    static unsigned s_fails;
    unsigned long long t0;
    DWORD got = 0;
    if (!XBOX_DISC_DIRECT) return xbox_fread(buf, size, n, f);
    if (!size || !n) return 0;
    t0 = xbox_ticks();
    if (!ReadFile(file_handle(f), buf, (DWORD)(size * n), &got, NULL) && s_fails++ < 8)
        xbox_logf("[IO] disc image read of %u bytes failed (error %lu)\n", (unsigned)(size * n),
                  (unsigned long)GetLastError());
    g_xfs.fread_ticks += xbox_ticks() - t0;
    g_xfs.fread_bytes += got;
    g_xfs.fread_n++;
    return got / size;
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
            /* test runs: a save or settings.ini packed on the disc wins over
             * the HDD copy a previous run left (harness OCX_STAGE_EXTRA) */
            join(out, cap, XBOX_DISC_DIR, in);
            if ((strncmp(in, "save/", 5) == 0 || strcmp(in, "settings.ini") == 0) && file_exists(out)) break;
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
    int w = mode_writes(mode);
    return fopen(xbox_resolve(path, w ? XBOX_PATH_WRITE : XBOX_PATH_READ, p, sizeof p), mode);
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

/* local time minus UTC, in seconds, from the dashboard's time zone and DST
 * rule (what nxdk's GetLocalTime applies). pc_os.c's OSInit adds it to UTC,
 * so the game clock reads the dashboard's local time, as a GameCube's RTC
 * holds local time. */
long xbox_local_offset_secs(void) {
    TIME_ZONE_INFORMATION tz;
    long bias;
    switch (GetTimeZoneInformation(&tz)) {
        case TIME_ZONE_ID_UNKNOWN: bias = tz.Bias; break;
        case TIME_ZONE_ID_STANDARD: bias = tz.Bias + tz.StandardBias; break;
        case TIME_ZONE_ID_DAYLIGHT: bias = tz.Bias + tz.DaylightBias; break;
        default: bias = 0; break;
    }
    {
        static int logged;   /* OSInit runs twice */
        if (!logged++) xbox_logf("[CLOCK] dashboard time zone: UTC%+ld:%02ld\n", -bias / 60, (bias < 0 ? -bias : bias) % 60);
    }
    return -bias * 60L;
}
