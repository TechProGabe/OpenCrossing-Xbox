/* xbox_watchdog.c — hang dumper.
 *
 * A 1 Hz thread started first thing in main() watches the presented-frame
 * counter. It fires once if no frame has been presented XBOX_WATCHDOG_BOOT_SECS
 * after boot, or if frames stop for XBOX_WATCHDOG_SECS later. Every thread in
 * the process is dumped: state, wait reason, and each stack word that points
 * into the XBE image (a heuristic backtrace; frame pointers are not reliable
 * under -O2). The report goes to COM1, to E:\UDATA\4f430001\hang.log, and —
 * because real hardware has no serial port — onto the screen (pbkit's debug
 * screen: the XVideo framebuffer the splash used). Symbolize the addresses
 * with tools/xbox/sym.py. Costs nothing until it fires.
 * Kill switch: -DXBOX_WATCHDOG=0. */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/debug.h>
#include <pbkit/pbkit.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "xbox_io.h"

#ifndef XBOX_WATCHDOG
#define XBOX_WATCHDOG 1
#endif
#ifndef XBOX_WATCHDOG_SECS
#define XBOX_WATCHDOG_SECS 6
#endif
#ifndef XBOX_WATCHDOG_BOOT_SECS
#define XBOX_WATCHDOG_BOOT_SECS 90   /* CD-R boots read ~27 MB at drive speed */
#endif
/* last.log: every XBOX_LASTLOG_SECS in which something was logged (and every
 * 30 s regardless) the watchdog rewrites E:\UDATA\4f430001\last.log with the
 * log tail and a [STATE] line, flushed, so a hard freeze or power-off still
 * leaves the seconds before it on disk (boot.log is up to a second behind).
 * Quiet stretches don't touch the disk: heartbeat lines ([BEAT], [FRAME],
 * [PROF]) don't count as something logged. 0 disables. */
#ifndef XBOX_LASTLOG_SECS
#define XBOX_LASTLOG_SECS 3
#endif

extern unsigned int pc_image_base, pc_image_end;
unsigned int xbox_frame_count(void);
unsigned xbox_audio_starved_ms(unsigned* gaps);
void xbox_flush_file(HANDLE h);

#define WD_MAX_THREADS 16
#define WD_MAX_WORDS   40

typedef struct {
    PKTHREAD t;
    void *sp, *top;
    UCHAR state, wait;
    SCHAR prio;
    int self, n;
    ULONG words[WD_MAX_WORDS];
} Snap;

static Snap s_snap[WD_MAX_THREADS];

/* Xbox kernel stacks are nonpaged and committed from KernelStack (the saved
 * ESP of a thread that is not running) up to StackBase, so the scan can't
 * fault. Only copying happens at DPC level; formatting/COM1 come after. */
static void snap_thread(Snap* o, PKTHREAD t, int self) {
    ULONG* sp = (ULONG*)t->KernelStack;
    ULONG* top = (ULONG*)t->StackBase;
    o->t = t;
    o->sp = sp;
    o->top = top;
    o->state = t->State;
    o->wait = t->WaitReason;
    o->prio = t->Priority;
    o->self = self;
    o->n = 0;
    if (self || !sp || !top || sp >= top || top - sp > 0x40000) return;
    for (; sp < top && o->n < WD_MAX_WORDS; sp++) {
        ULONG v = *sp;
        if (v >= pc_image_base + 0x1000 && v < pc_image_end) o->words[o->n++] = v;
    }
}

static char s_report[8192];
static int s_rlen;

static void rep(const char* fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(s_report + s_rlen, sizeof s_report - (size_t)s_rlen, fmt, ap);
    va_end(ap);
    if (n > 0) s_rlen += n;
    if (s_rlen > (int)sizeof s_report - 1) s_rlen = (int)sizeof s_report - 1;
}

/* last `lines` lines of the log, each cut to `cols` */
static void screen_tail(int lines, int cols) {
    static char tail[4096];
    char* p;
    char* start[64];
    int n = 0, i;
    xbox_log_tail(tail, sizeof tail);
    for (p = tail; *p;) {
        if (n < 64) start[n++] = p;
        else { memmove(start, start + 1, sizeof start - sizeof start[0]); start[63] = p; }
        p = strchr(p, '\n');
        if (!p) break;
        *p++ = '\0';
    }
    for (i = n > lines ? n - lines : 0; i < n; i++) {
        char line[128];
        snprintf(line, sizeof line, "%.*s", cols, start[i]);
        debugPrint("%s\n", line);
    }
}

static void dump_all(const char* why) {
    PKTHREAD me = KeGetCurrentThread();
    PKPROCESS p = me->ApcState.Process;
    PLIST_ENTRY e;
    int i, j, n = 0;
    HANDLE h;
    KIRQL old = KeRaiseIrqlToDpcLevel();   /* freeze the thread list while walking it */
    for (e = p->ThreadListHead.Flink; e != &p->ThreadListHead && n < WD_MAX_THREADS; e = e->Flink) {
        PKTHREAD t = CONTAINING_RECORD(e, KTHREAD, ThreadListEntry);
        snap_thread(&s_snap[n++], t, t == me);
    }
    KfLowerIrql(old);

    s_rlen = 0;
    rep("[WDOG] %s (frame %u), %d threads\n", why, xbox_frame_count(), n);
    {
        char st[320];
        xbox_nv2a_state(st, sizeof st);
        rep("%s[WDOG] free %u KB\n", st, xbox_mem_free_kb());
    }
    for (i = 0; i < n; i++) {
        const Snap* o = &s_snap[i];
        rep("[WDOG] thread %p%s state %u wait %u prio %d\n[WDOG]  ", (void*)o->t, o->self ? " (watchdog)" : "",
            (unsigned)o->state, (unsigned)o->wait, (int)o->prio);
        for (j = 0; j < o->n; j++) rep(" %08lx", o->words[j]);
        rep("\n");
    }
    rep("[WDOG] end\n");

    /* screen FIRST: file I/O below can block if the hang involves the disk */
    pb_show_debug_screen();
    debugClearScreen();
    debugPrint("OpenCrossing-Xbox: %s at frame %u\n", why, xbox_frame_count());
    debugPrint("Log: E:\\UDATA\\4f430001\\hang.log + boot.log\n");
    debugPrint("(hang_prev.log + boot_prev.log once the game is restarted)\n\n");
    screen_tail(14, 76);
    debugPrint("\n");
    for (i = 0; i < n; i++) {
        const Snap* o = &s_snap[i];
        if (o->self) continue;
        debugPrint("t%d s%u w%u:", i, (unsigned)o->state, (unsigned)o->wait);
        for (j = 0; j < o->n && j < 8; j++) debugPrint(" %08lx", o->words[j]);
        debugPrint("\n");
    }

    xbox_log_write(s_report, (size_t)s_rlen);

    h = CreateFileA(XBOX_UDATA_DIR "hang.log", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        static char tail[4096];
        DWORD w;
        size_t tl = xbox_log_tail(tail, sizeof tail);
        WriteFile(h, tail, (DWORD)tl, &w, NULL);
        WriteFile(h, s_report, (DWORD)s_rlen, &w, NULL);
        xbox_flush_file(h);
        CloseHandle(h);
    }
    xbox_bootlog_pump();

}

static volatile int s_disabled;

static void write_last_log(void) {
    static HANDLE h = INVALID_HANDLE_VALUE;
    static char buf[4096 + 512];
    size_t n;
    DWORD w;
    if (!XBOX_LASTLOG_SECS) return;
    if (h == INVALID_HANDLE_VALUE) {
        h = CreateFileA(XBOX_UDATA_DIR "last.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
    }
    n = xbox_log_tail(buf, 4096);
    n += (size_t)xbox_nv2a_state(buf + n, (int)(sizeof buf - n));
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    SetEndOfFile(h);
    xbox_flush_file(h);
}

/* the fatal error card owns the screen for good */
void xbox_watchdog_disable(void) { s_disabled = 1; }

/* [BEAT] every XBOX_HEARTBEAT_SECS (from Melee-X): if the log ends with
 * [BEAT] lines whose vblank count climbs while `presented` stands still, the
 * game loops without drawing; if [BEAT] stops too, the whole machine
 * stopped. Free memory in each one shows a leak over a long session. 0 = off. */
#ifndef XBOX_HEARTBEAT_SECS
#define XBOX_HEARTBEAT_SECS 5
#endif

static int watchdog_body(void* arg) {
    unsigned last = 0, still = 0, secs = 0, fired = 0;
    (void)arg;
    DWORD tick = GetTickCount();
    for (;;) {
        unsigned f;
        HANDLE ev = (HANDLE)xbox_bootlog_event();
        /* an urgent line wakes us early: write it, the second goes on */
        if (ev) {
            /* one read: two could straddle a tick and wrap to INFINITE */
            DWORD el = GetTickCount() - tick;
            WaitForSingleObject(ev, el < 1000 ? 1000 - el : 0);
        } else
            Sleep(1000);
        xbox_bootlog_pump();   /* the queued log lines (xbox_io.c) */
        if (GetTickCount() - tick < 1000) continue;
        tick = GetTickCount();
        secs++;
        if (s_disabled) continue;
        f = xbox_frame_count();
        if (XBOX_HEARTBEAT_SECS && f && secs % XBOX_HEARTBEAT_SECS == 0) {
            /* audio starved: silence the AC97 played because the producer
             * thread was behind (an audible chug), xbox_audio.c */
            unsigned gaps, starved = xbox_audio_starved_ms(&gaps);
            char au[48] = "";
            if (starved) snprintf(au, sizeof au, ", audio starved %u ms in %u gaps", starved, gaps);
            xbox_logf_quiet("[BEAT] %us: vblank %u, presented %u, free %u KB%s\n", secs, (unsigned)pb_get_vbl_counter(), f,
                      xbox_mem_free_kb(), au);
        }
        if (XBOX_LASTLOG_SECS && f && secs % XBOX_LASTLOG_SECS == 0) {
            static unsigned logged_at;
            unsigned pos = xbox_log_pos_loud();
            if (pos != logged_at || secs % 30 == 0) {
                logged_at = pos;
                write_last_log();
            }
        }
        if (f == 0) {
            if (secs >= XBOX_WATCHDOG_BOOT_SECS && !fired) {
                dump_all("no first frame after boot");
                fired = 1;
            }
            continue;
        }
        if (f != last) {
            last = f;
            still = 0;
            fired = 0;
            continue;
        }
        if (++still >= XBOX_WATCHDOG_SECS && !fired) {
            dump_all("frames stopped");
            fired = 1;
        }
    }
    return 0;
}

static DWORD WINAPI watchdog(LPVOID arg) {
    return (DWORD)xbox_crash_guard(watchdog_body, arg);
}

void xbox_watchdog_start(void) {
    HANDLE h;
    if (!XBOX_WATCHDOG) return;
    h = CreateThread(NULL, 32 * 1024, watchdog, NULL, 0, NULL);
    if (h) {
        SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL);
        CloseHandle(h);
    }
}
