/* xbox_watchdog.c — hang dumper.
 *
 * A 1 Hz thread watches the presented-frame counter. If it stops for
 * XBOX_WATCHDOG_SECS, every thread in the process is dumped once to COM1:
 * state, wait reason, and each stack word that points into the XBE image
 * (a heuristic backtrace; frame pointers are not reliable under -O2).
 * Symbolize with: tools/xbox/sym.py < log. Costs nothing until it fires.
 * Kill switch: -DXBOX_WATCHDOG=0. */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include "xbox_io.h"

#ifndef XBOX_WATCHDOG
#define XBOX_WATCHDOG 1
#endif
#ifndef XBOX_WATCHDOG_SECS
#define XBOX_WATCHDOG_SECS 6
#endif

extern unsigned int pc_image_base, pc_image_end;
unsigned int xbox_frame_count(void);

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

static void dump_all(void) {
    PKTHREAD me = KeGetCurrentThread();
    PKPROCESS p = me->ApcState.Process;
    PLIST_ENTRY e;
    int i, j, n = 0;
    KIRQL old = KeRaiseIrqlToDpcLevel();   /* freeze the thread list while walking it */
    for (e = p->ThreadListHead.Flink; e != &p->ThreadListHead && n < WD_MAX_THREADS; e = e->Flink) {
        PKTHREAD t = CONTAINING_RECORD(e, KTHREAD, ThreadListEntry);
        snap_thread(&s_snap[n++], t, t == me);
    }
    KfLowerIrql(old);
    xbox_logf("[WDOG] no frame for %d s at frame %u, dumping %d threads\n", XBOX_WATCHDOG_SECS, xbox_frame_count(), n);
    for (i = 0; i < n; i++) {
        const Snap* o = &s_snap[i];
        xbox_logf("[WDOG] thread %p%s state %u wait %u prio %d stack %p..%p\n", (void*)o->t,
                  o->self ? " (watchdog)" : "", (unsigned)o->state, (unsigned)o->wait, (int)o->prio, o->sp, o->top);
        if (!o->n) continue;
        xbox_logf("[WDOG]   ");
        for (j = 0; j < o->n; j++) xbox_logf("%08lx ", o->words[j]);
        xbox_logf("\n");
    }
    xbox_logf("[WDOG] end\n");
}

static DWORD WINAPI watchdog(LPVOID arg) {
    unsigned last = 0, still = 0, fired = 0;
    (void)arg;
    for (;;) {
        unsigned f;
        Sleep(1000);
        f = xbox_frame_count();
        if (f != last || f == 0) {
            last = f;
            still = 0;
            fired = 0;
            continue;
        }
        if (++still >= XBOX_WATCHDOG_SECS && !fired) {
            dump_all();
            fired = 1;
        }
    }
    return 0;
}

void xbox_watchdog_start(void) {
    HANDLE h;
    if (!XBOX_WATCHDOG) return;
    h = CreateThread(NULL, 16 * 1024, watchdog, NULL, 0, NULL);
    if (h) {
        SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL);
        CloseHandle(h);
    }
}
