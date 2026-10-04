/* xbox_crash.c — CPU exception reporter.
 *
 * Every Xbox thread runs in kernel mode, so an unhandled page fault, general
 * protection fault or divide error in game code ends in a kernel bugcheck:
 * the console freezes or reboots and nothing reaches the disk (the watchdog
 * never gets to run). xbox_crash_guard() puts an SEH registration record on
 * the thread's stack (fs:[0], the same mechanism nxdk's own __try uses), so
 * the kernel's exception dispatcher calls on_exception() first. It writes
 * the faulting address, registers, the renderer state and the stack words
 * that point into the XBE to E:\UDATA\4f430001\crash.log and to the screen,
 * then parks the thread. Symbolize with tools/xbox/sym.py.
 * Guarded: the main thread (xbox_main.c), the audio producer and AC97 pump
 * (xbox_audio.c) and the watchdog. Kill switch: -DXBOX_CRASH_GUARD=0.
 * Test: -DXBOX_DBG_CRASH_FRAME=N faults on purpose at frame N (xbox_main.c). */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/debug.h>
#include <pbkit/pbkit.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "xbox_io.h"

#ifndef XBOX_CRASH_GUARD
#define XBOX_CRASH_GUARD 1
#endif

extern unsigned int pc_image_base, pc_image_end;
unsigned int xbox_frame_count(void);
void xbox_flush_file(HANDLE h);

typedef struct Reg {
    struct Reg* prev;
    void* handler;
} Reg;

enum { DISP_CONTINUE_SEARCH = 1 };

static volatile LONG s_in_crash;
static char s_rep[6144];
static int s_len;

static void rep(const char* fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(s_rep + s_len, sizeof s_rep - (size_t)s_len, fmt, ap);
    va_end(ap);
    if (n > 0) s_len += n;
    if (s_len > (int)sizeof s_rep - 1) s_len = (int)sizeof s_rep - 1;
}

static const char* code_name(ULONG c) {
    switch (c) {
        case 0xC0000005: return "access violation";
        case 0xC000001D: return "illegal instruction";
        case 0xC0000094: return "integer divide by zero";
        case 0xC0000095: return "integer overflow";
        case 0xC0000096: return "privileged instruction";
        case 0xC00000FD: return "stack overflow";
        case 0x80000003: return "breakpoint";
        case 0x80000004: return "single step";
        default: return "exception";
    }
}

static void build_report(const EXCEPTION_RECORD* er, const CONTEXT* cx) {
    PKTHREAD t = KeGetCurrentThread();
    ULONG* sp = (ULONG*)cx->Esp;
    ULONG* top = (ULONG*)t->StackBase;
    int n = 0, i;
    char st[320];

    s_len = 0;
    rep("[CRASH] %s (%08lx) at %08lx, frame %u, thread %p\n", code_name((ULONG)er->ExceptionCode),
        (unsigned long)er->ExceptionCode, (unsigned long)(ULONG)er->ExceptionAddress, xbox_frame_count(), (void*)t);
    if ((ULONG)er->ExceptionCode == 0xC0000005 && er->NumberParameters >= 2)
        rep("[CRASH] %s of address %08lx\n", er->ExceptionInformation[0] ? "write" : "read",
            (unsigned long)er->ExceptionInformation[1]);
    rep("[CRASH] eip %08lx esp %08lx ebp %08lx eflags %08lx\n", cx->Eip, cx->Esp, cx->Ebp, cx->EFlags);
    rep("[CRASH] eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx\n", cx->Eax, cx->Ebx, cx->Ecx, cx->Edx,
        cx->Esi, cx->Edi);
    xbox_nv2a_state(st, sizeof st);
    rep("%s", st);
    rep("[CRASH] free %u KB\n", xbox_mem_free_kb());
    /* the stack below the fault: every word that points into the XBE */
    rep("[CRASH] stack:");
    if (sp && top && sp < top && top - sp < 0x40000) {
        for (; sp < top && n < 48; sp++) {
            ULONG v = *sp;
            if (v >= pc_image_base + 0x1000 && v < pc_image_end) {
                rep(" %08lx", v);
                n++;
            }
        }
    }
    rep("\n[CRASH] raw:");
    sp = (ULONG*)cx->Esp;
    for (i = 0; i < 16 && sp && top && sp + i < top; i++) rep(" %08lx", sp[i]);
    rep("\n[CRASH] end\n");
}

static void write_crash_log(void) {
    static char tail[4096];
    HANDLE h = CreateFileA(XBOX_UDATA_DIR "crash.log", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           NULL);
    DWORD w;
    size_t tl;
    if (h == INVALID_HANDLE_VALUE) return;
    tl = xbox_log_tail(tail, sizeof tail);
    WriteFile(h, tail, (DWORD)tl, &w, NULL);
    WriteFile(h, s_rep, (DWORD)s_len, &w, NULL);
    xbox_flush_file(h);
    CloseHandle(h);
}

static void show_screen(void) {
    char* line = s_rep;
    pb_show_debug_screen();
    debugClearScreen();
    debugPrint("OpenCrossing-Xbox crashed. Please report it with\n");
    debugPrint("E:\\UDATA\\4f430001\\crash.log (and last.log;\n");
    debugPrint("crash_prev.log / last_prev.log once the game is restarted).\n\n");
    while (*line) {
        char* nl = strchr(line, '\n');
        char buf[112];
        int len = nl ? (int)(nl - line) : (int)strlen(line);
        if (len > (int)sizeof buf - 1) len = (int)sizeof buf - 1;
        memcpy(buf, line, (size_t)len);
        buf[len] = '\0';
        debugPrint("%s\n", buf);
        if (!nl) break;
        line = nl + 1;
    }
    debugPrint("\nHold the power button to switch off.\n");
}

__attribute__((cdecl)) static int on_exception(EXCEPTION_RECORD* er, void* frame, CONTEXT* cx, void* dc) {
    (void)frame;
    (void)dc;
    if (er->ExceptionFlags & EXCEPTION_UNWIND) return DISP_CONTINUE_SEARCH;
    /* a second fault (another thread, or inside this report) just parks */
    if (InterlockedExchange((LONG*)&s_in_crash, 1)) {
        if (KeGetCurrentIrql() < DISPATCH_LEVEL)
            for (;;) Sleep(1000);
        for (;;) {}
    }
    xbox_watchdog_disable();
    build_report(er, cx);
    show_screen();
    if (KeGetCurrentIrql() >= DISPATCH_LEVEL) {
        /* no waits or file I/O at raised IRQL: the screen is all we get */
        for (;;) {}
    }
    xbox_log_write(s_rep, (size_t)s_len);
    write_crash_log();
    for (;;) Sleep(1000);
    return DISP_CONTINUE_SEARCH;
}

__attribute__((noinline)) int xbox_crash_guard(int (*fn)(void*), void* arg) {
    Reg r;
    int ret;
    if (!XBOX_CRASH_GUARD) return fn(arg);
    r.handler = (void*)on_exception;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(r.prev));
    __asm__ volatile("movl %0, %%fs:0" : : "r"(&r) : "memory");
    ret = fn(arg);
    __asm__ volatile("movl %0, %%fs:0" : : "r"(r.prev) : "memory");
    return ret;
}
