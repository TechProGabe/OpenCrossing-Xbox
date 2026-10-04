/* xbox_diag.c — CPU and code-integrity diagnostics for GitHub #2/#3.
 *
 * The report (docs/known-issues.md): on a 128 MB console with a 1 GHz
 * Coppermine cC0 (cpuid 0686; stock is a cD0, 068a), two builds that put
 * lbRTC_Sub_DD at different addresses faulted at the same function offset
 * (+dc), with the registers of a correct run up to there, reading d68301f8.
 * The bytes at the eip are `31 f6 83 f8 01 83 d6 0b` (xor esi,esi; cmp
 * eax,1; adc esi,11); from the second byte on they decode as
 * `test byte [ebx+d68301f8], 0b` with ebx = 0, the read that faulted. So
 * either that CPU decodes the sequence one byte late, or the first byte in
 * that console's RAM isn't 31. Each piece below tells those apart:
 *
 *  - xbox_diag_boot: cpuid / microcode / MSRs / kernel ([DIAG] cpu lines);
 *    .text and .rdata in RAM compared with D:\default.xbe ([DIAG] .text);
 *    lbRTC_Sub_DD on an all-zero date, the #3 path, under an exception
 *    guard, from its real address and from copies at every alignment mod
 *    64 ([DIAG] rtc lines). A fault is caught, logged and survived.
 *  - xbox_diag_crash_code/_disk: the code bytes around a crash's eip, from
 *    RAM and from the XBE on disk, in crash.log (xbox_crash.c).
 *  - lbRTC_Sub_DD shim (XBOX_RTC_SHIM): settings.ini rtc_shim = 1 runs a
 *    plain -O0 copy of the C instead of the game's -O2 code.
 *
 * The exception guard (diag_try) registers an SEH frame on fs:[0] like
 * xbox_crash.c, and its handler longjmps back out of the kernel's
 * dispatcher, which is what an __except block does after RtlUnwind (there
 * is nothing between the two frames to unwind). fs:[0] is put back by hand
 * because the dispatcher pushed a frame of its own before calling us.
 * All off unless -DXBOX_DIAG_ISSUE3=ON (xbox_diag.h for the per-piece
 * defines). */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_io.h"
#include "xbox_diag.h"
#include "xbox_settings.h"

extern unsigned int pc_image_base, pc_image_end;

/* ---- exception guard ---- */
typedef struct Reg {
    struct Reg* prev;
    void* handler;
} Reg;

static jmp_buf s_jb;
static volatile unsigned s_fault_code, s_fault_eip, s_fault_addr, s_fault_write;

__attribute__((cdecl)) static int try_handler(EXCEPTION_RECORD* er, void* frame, CONTEXT* cx, void* dc) {
    (void)frame;
    (void)dc;
    if (er->ExceptionFlags & EXCEPTION_UNWIND) return 1;   /* ExceptionContinueSearch */
    s_fault_code = (unsigned)er->ExceptionCode;
    s_fault_eip = (unsigned)cx->Eip;
    s_fault_write = er->NumberParameters >= 1 ? (unsigned)er->ExceptionInformation[0] : 0;
    s_fault_addr = er->NumberParameters >= 2 ? (unsigned)er->ExceptionInformation[1] : 0;
    longjmp(s_jb, 1);
    return 1;
}

/* runs fn(arg); 0 when it returned, 1 when it faulted (s_fault_* say where) */
__attribute__((noinline)) static int diag_try(void (*fn)(void*), void* arg) {
    Reg r;
    volatile int faulted = 0;
    r.handler = (void*)try_handler;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(r.prev));
    __asm__ volatile("movl %0, %%fs:0" : : "r"(&r) : "memory");
    if (setjmp(s_jb) == 0)
        fn(arg);
    else
        faulted = 1;
    __asm__ volatile("movl %0, %%fs:0" : : "r"(r.prev) : "memory");
    return faulted;
}

static const char* fault_str(void) {
    static char s[96];
    snprintf(s, sizeof s, "FAULT %08x at eip %08x, %s of %08x", s_fault_code, s_fault_eip,
             s_fault_write ? "write" : "read", s_fault_addr);
    return s;
}

/* ---- hex helpers ---- */
static int hex_line(char* out, int cap, const char* tag, const unsigned char* p, int n) {
    int len = snprintf(out, (size_t)cap, "%s", tag), i;
    for (i = 0; i < n && len < cap - 4; i++) len += snprintf(out + len, (size_t)(cap - len), " %02x", p[i]);
    if (len < cap - 1) len += snprintf(out + len, (size_t)(cap - len), "\n");
    return len;
}

static unsigned fnv1a(unsigned h, const unsigned char* p, unsigned n) {
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

/* ---- the XBE on disk ---- */
#define XBE_PATH "D:\\default.xbe"
#define HDR_CAP 8192
static unsigned char s_hdr[HDR_CAP];   /* the file's own header (not RAM's) */
static unsigned s_hdr_len;

static int file_read_at(HANDLE h, unsigned off, void* buf, unsigned n) {
    DWORD got = 0;
    if (SetFilePointer(h, (LONG)off, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return 0;
    if (!ReadFile(h, buf, n, &got, NULL)) return 0;
    return got == n;
}

static HANDLE xbe_open(void) {
    HANDLE h = CreateFileA(XBE_PATH, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned size;
    if (h == INVALID_HANDLE_VALUE) return h;
    if (!file_read_at(h, 0, s_hdr, 0x110) || memcmp(s_hdr, "XBEH", 4) != 0) goto bad;
    size = ((const XBE_FILE_HEADER*)s_hdr)->SizeOfHeaders;
    if (size < 0x180 || size > HDR_CAP || !file_read_at(h, 0, s_hdr, size)) goto bad;
    s_hdr_len = size;
    return h;
bad:
    CloseHandle(h);
    return INVALID_HANDLE_VALUE;
}

/* the file's section table, by index; NULL past the end */
static const XBE_SECTION_HEADER* xbe_section(unsigned i, const char** name) {
    const XBE_FILE_HEADER* h = (const XBE_FILE_HEADER*)s_hdr;
    unsigned base = h->ImageBase, tab = (unsigned)h->PointerToSectionTable - base, off = tab + i * sizeof(XBE_SECTION_HEADER);
    const XBE_SECTION_HEADER* s;
    if (i >= h->NumberOfSections || off + sizeof *s > s_hdr_len) return NULL;
    s = (const XBE_SECTION_HEADER*)(s_hdr + off);
    *name = "?";
    if ((unsigned)s->SectionName >= base && (unsigned)s->SectionName < base + s_hdr_len - 8)
        *name = (const char*)(s_hdr + ((unsigned)s->SectionName - base));
    return s;
}

/* the kernel patches the import thunk table in place: skip it when comparing.
 * Its pointer in the header is XOR-coded (retail key, else debug). */
static void thunk_range(unsigned* lo, unsigned* hi) {
    const XBE_FILE_HEADER* h = (const XBE_FILE_HEADER*)s_hdr;
    unsigned t = h->PointerToKernelThunkTable ^ 0x5B6D40B6u, n = 0;
    const unsigned* p;
    *lo = *hi = 0;
    if (t < pc_image_base || t >= pc_image_end) t = h->PointerToKernelThunkTable ^ 0xEFB1F152u;
    if (t < pc_image_base || t >= pc_image_end) return;
    /* count the entries from RAM: the kernel wrote addresses in, 0 ends it */
    for (p = (const unsigned*)t; (unsigned)(p + 1) <= pc_image_end && *p && n < 512; p++) n++;
    *lo = t;
    *hi = t + n * 4;
}

/* file offset of a virtual address, with the bytes left in its section; 0 if none */
static unsigned xbe_file_off(unsigned va, unsigned* left) {
    const char* name;
    const XBE_SECTION_HEADER* s;
    unsigned i;
    for (i = 0; (s = xbe_section(i, &name)) != NULL; i++) {
        if (va >= s->VirtualAddress && va < s->VirtualAddress + s->FileSize) {
            *left = s->VirtualAddress + s->FileSize - va;
            return s->FileAddress + (va - s->VirtualAddress);
        }
    }
    return 0;
}

/* ---- boot: CPU ---- */
#if XBOX_DIAG_CPU
static void cpuid(unsigned leaf, unsigned sub, unsigned r[4]) {
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}

typedef struct {
    unsigned msr, lo, hi;
} MsrArg;
static void do_rdmsr(void* a) {
    MsrArg* m = a;
    __asm__ volatile("rdmsr" : "=a"(m->lo), "=d"(m->hi) : "c"(m->msr));
}
static void do_wrmsr(void* a) {
    MsrArg* m = a;
    __asm__ volatile("wrmsr" : : "c"(m->msr), "a"(m->lo), "d"(m->hi));
}
static void do_ucode(void* a) {
    MsrArg* m = a;
    unsigned r[4];
    /* Intel's sequence: clear 8Bh, cpuid(1), then 8Bh's edx is the revision */
    __asm__ volatile("wrmsr" : : "c"(0x8B), "a"(0), "d"(0));
    cpuid(1, 0, r);
    m->msr = 0x8B;
    __asm__ volatile("rdmsr" : "=a"(m->lo), "=d"(m->hi) : "c"(m->msr));
}
static void do_cr(void* a) {
    unsigned* c = a;
    __asm__ volatile("movl %%cr0, %0" : "=r"(c[0]));
    __asm__ volatile("movl %%cr4, %0" : "=r"(c[1]));
}

static const char* stepping_name(unsigned sig) {
    switch (sig & 0xfff) {
        case 0x681: return "Coppermine cA2";
        case 0x683: return "Coppermine cB0";
        case 0x686: return "Coppermine cC0";
        case 0x68a: return "Coppermine cD0 (the stock Xbox CPU)";
        case 0x6b1: return "Tualatin tA1";
        case 0x6b4: return "Tualatin tB1";
        default: return "";
    }
}

static void log_msr(const char* what, unsigned msr) {
    MsrArg m = { msr, 0, 0 };
    if (diag_try(do_rdmsr, &m))
        xbox_logf("[DIAG] msr %03x (%s): %s\n", msr, what, fault_str());
    else
        xbox_logf("[DIAG] msr %03x (%s): %08x %08x\n", msr, what, m.hi, m.lo);
}

static void diag_cpu(void) {
    unsigned r[4], max, i, cr[2] = { 0, 0 };
    char vendor[13];
    MsrArg m = { 0, 0, 0 };
    cpuid(0, 0, r);
    max = r[0];
    memcpy(vendor, &r[1], 4);
    memcpy(vendor + 4, &r[3], 4);
    memcpy(vendor + 8, &r[2], 4);
    vendor[12] = '\0';
    xbox_logf("[DIAG] cpuid 0: max leaf %u, \"%s\"\n", max, vendor);
    cpuid(1, 0, r);
    xbox_logf("[DIAG] cpuid 1: eax %08x ebx %08x ecx %08x edx %08x: family %u model %u stepping %u %s\n", r[0],
              r[1], r[2], r[3], (r[0] >> 8) & 15, (r[0] >> 4) & 15, r[0] & 15, stepping_name(r[0]));
    for (i = 2; i <= max && i <= 4; i++) {
        cpuid(i, 0, r);
        xbox_logf("[DIAG] cpuid %u: eax %08x ebx %08x ecx %08x edx %08x\n", i, r[0], r[1], r[2], r[3]);
    }
    cpuid(0x80000000u, 0, r);
    if (r[0] >= 0x80000004u) {
        char brand[49];
        for (i = 0; i < 3; i++) {
            cpuid(0x80000002u + i, 0, r);
            memcpy(brand + i * 16, r, 16);
        }
        brand[48] = '\0';
        xbox_logf("[DIAG] cpuid brand: \"%s\"\n", brand);
    } else {
        xbox_logf("[DIAG] cpuid 80000000: %08x (no brand string)\n", r[0]);
    }
    if (diag_try(do_ucode, &m))
        xbox_logf("[DIAG] microcode: %s\n", fault_str());
    else
        xbox_logf("[DIAG] microcode revision %08x (msr 08b: %08x %08x; 0 = no update loaded)\n", m.hi, m.hi, m.lo);
    log_msr("platform id", 0x17);
    log_msr("EBL_CR_POWERON: bits 25-22 bus ratio, 19-18 FSB", 0x2A);
    log_msr("BBL_CR_CTL3 (L2 config)", 0x11E);
    log_msr("MISC_ENABLE", 0x1A0);
    if (diag_try(do_cr, cr))
        xbox_logf("[DIAG] cr0/cr4: %s\n", fault_str());
    else
        xbox_logf("[DIAG] cr0 %08x cr4 %08x\n", cr[0], cr[1]);
    xbox_logf("[DIAG] kernel %u.%u.%u.%u, hardware flags %08x, gpu rev %02x, mcp rev %02x\n", XboxKrnlVersion.Major,
              XboxKrnlVersion.Minor, XboxKrnlVersion.Build, XboxKrnlVersion.Qfe, (unsigned)XboxHardwareInfo.Flags,
              XboxHardwareInfo.GpuRevision, XboxHardwareInfo.McpRevision);
    if (XeImageFileName->Buffer)
        xbox_logf("[DIAG] xbe: %.*s\n", (int)XeImageFileName->Length, XeImageFileName->Buffer);
}
#endif

/* ---- boot: RAM vs disk ---- */
#if XBOX_DIAG_TEXT_CHECK
#define CHUNK 65536
static void diag_text_check(void) {
    HANDLE h = xbe_open();
    unsigned char* buf;
    const char* name;
    const XBE_SECTION_HEADER* s;
    unsigned i, tlo, thi;
    if (h == INVALID_HANDLE_VALUE) {
        xbox_logf("[DIAG] %s: could not open or not an XBE (error %lu)\n", XBE_PATH, (unsigned long)GetLastError());
        return;
    }
    buf = malloc(CHUNK);
    if (!buf) {
        CloseHandle(h);
        return;
    }
    thunk_range(&tlo, &thi);
    for (i = 0; (s = xbe_section(i, &name)) != NULL; i++) {
        unsigned off, n = s->FileSize, diff = 0, first = 0, last = 0, ram_h = 2166136261u, disk_h = 2166136261u;
        unsigned char fr = 0, fd = 0;
        int ok = 1;
        if (strcmp(name, ".text") != 0 && strcmp(name, ".rdata") != 0) continue;
        if (s->VirtualAddress < pc_image_base || s->VirtualAddress + n > pc_image_end) {
            xbox_logf("[DIAG] %s: %08x+%x is outside the image %08x-%08x\n", name, s->VirtualAddress, n,
                      pc_image_base, pc_image_end);
            continue;
        }
        for (off = 0; off < n && ok; off += CHUNK) {
            unsigned len = n - off < CHUNK ? n - off : CHUNK, k;
            const unsigned char* ram = (const unsigned char*)(s->VirtualAddress + off);
            if (!file_read_at(h, s->FileAddress + off, buf, len)) {
                ok = 0;
                break;
            }
            ram_h = fnv1a(ram_h, ram, len);
            disk_h = fnv1a(disk_h, buf, len);
            if (memcmp(ram, buf, len) == 0) continue;
            for (k = 0; k < len; k++) {
                unsigned va = s->VirtualAddress + off + k;
                if (ram[k] == buf[k] || (va >= tlo && va < thi)) continue;
                if (!diff) {
                    first = va;
                    fr = ram[k];
                    fd = buf[k];
                }
                last = va;
                diff++;
            }
        }
        if (!ok)
            xbox_logf("[DIAG] %s: read of the file failed at +%x (error %lu)\n", name, off, (unsigned long)GetLastError());
        else if (!diff)
            xbox_logf("[DIAG] %s %08x+%x: RAM == disk (fnv %08x)\n", name, s->VirtualAddress, n, ram_h);
        else
            xbox_logf("[DIAG] %s %08x+%x: RAM != disk: %u bytes differ, first %08x (ram %02x disk %02x), last %08x "
                      "(fnv ram %08x disk %08x)\n",
                      name, s->VirtualAddress, n, diff, first, fr, fd, last, ram_h, disk_h);
    }
    if (thi) xbox_logf("[DIAG] (kernel thunk table %08x-%08x not compared)\n", tlo, thi);
    free(buf);
    CloseHandle(h);
}
#endif

/* ---- lbRTC_Sub_DD ---- */
typedef struct {
    unsigned char sec, min, hour, day, weekday, month;
    unsigned short year;
} DiagRtc;   /* lbRTC_time_c (include/lb_rtc.h) */

#if XBOX_RTC_SHIM
void lbRTC_Sub_DD_game(DiagRtc* t, int days);
#define RTC_SUB_DD lbRTC_Sub_DD_game
#else
void lbRTC_Sub_DD(DiagRtc* t, int days);
#define RTC_SUB_DD lbRTC_Sub_DD
#endif

#if XBOX_DIAG_RTC_TEST
/* the sequence at the #3 eip, and the function's epilogue (pop esi/edi/ebx/ebp; ret) */
static const unsigned char k_seq[] = { 0x31, 0xf6, 0x83, 0xf8, 0x01, 0x83, 0xd6, 0x0b };
static const unsigned char k_epi[] = { 0x5e, 0x5f, 0x5b, 0x5d, 0xc3 };
#define FN_SCAN 512
static unsigned char s_copy[FN_SCAN + 64] __attribute__((aligned(64)));

typedef struct {
    void (*fn)(DiagRtc*, int);
    DiagRtc t;
    int days;
} RtcCall;
static void do_rtc(void* a) {
    RtcCall* c = a;
    c->fn(&c->t, c->days);
}
static void serialize(void) {
    unsigned r[4];
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(0), "c"(0));
}
static void cache_flush(void) { __asm__ volatile("wbinvd" : : : "memory"); }

/* Kabu_manager's call on a new town: an all-zero date, days = weekday = 0.
 * month 0 takes the fallthrough into the sequence; January (month 1) reaches
 * it through the `jmp` from the other branch. */
static int rtc_run(void (*fn)(DiagRtc*, int), unsigned month, int reps, int flush, const char* what) {
    RtcCall c;
    int i;
    for (i = 0; i < reps; i++) {
        memset(&c.t, 0, sizeof c.t);
        c.t.month = (unsigned char)month;
        c.fn = fn;
        c.days = c.t.weekday;
        if (flush) cache_flush();
        serialize();
        if (diag_try(do_rtc, &c)) {
            xbox_logf("[DIAG] rtc %s: %s after %d ok\n", what, fault_str(), i);
            return 0;
        }
    }
    xbox_logf("[DIAG] rtc %s x%d%s: ok (-> y %u m %u d %u)\n", what, reps, flush ? " (wbinvd)" : "", c.t.year, c.t.month,
              c.t.day);
    return 1;
}

/* the guard itself, first: the #3 read on purpose. Caught = the rtc lines
 * below mean what they say; not caught = this boot ends in crash.log here */
static void do_fault(void* a) {
    (void)a;
    *(volatile unsigned*)0xd68301f8u;
}

static void diag_rtc_test(void) {
    const unsigned char* fn = (const unsigned char*)RTC_SUB_DD;
    unsigned seq = 0, len = 0, i;
    char line[160];
    int faults = 0;
    unsigned first_fault = 0;
    if (diag_try(do_fault, NULL))
        xbox_logf("[DIAG] guard self-test: caught %s (expected: read of d68301f8)\n", fault_str());
    else
        xbox_logf("[DIAG] guard self-test: the read of d68301f8 did not fault?!\n");
    for (i = 0; i + sizeof k_seq <= FN_SCAN; i++)
        if (!seq && memcmp(fn + i, k_seq, sizeof k_seq) == 0) seq = i;
    for (i = seq ? seq : 0; i + sizeof k_epi <= FN_SCAN; i++)
        if (memcmp(fn + i, k_epi, sizeof k_epi) == 0) {
            len = i + sizeof k_epi;
            break;
        }
    xbox_logf("[DIAG] lbRTC_Sub_DD at %08x (mod 64 = %02x): sequence at +%x, length %x\n", (unsigned)fn,
              (unsigned)fn & 63, seq, len);
    if (seq) {
        snprintf(line, sizeof line, "[DIAG] lbRTC_Sub_DD+%x (%08x):", seq - 8, (unsigned)fn + seq - 8);
        hex_line(line + strlen(line), (int)(sizeof line - strlen(line)), "", fn + seq - 8, 24);
        xbox_log_write(line, strlen(line));
    }
    /* 1. the real function, where the game runs it */
    rtc_run(RTC_SUB_DD, 0, 16, 1, "real, zero date");
    rtc_run(RTC_SUB_DD, 0, 1000, 0, "real, zero date");
    rtc_run(RTC_SUB_DD, 1, 16, 1, "real, January");
    if (!seq || !len || len > FN_SCAN) {
        xbox_logf("[DIAG] rtc copies: skipped (sequence or epilogue not found; not the v1 code)\n");
        return;
    }
    /* 2. the same bytes at every alignment mod 64 (the function has no calls
     * and only absolute data addresses, so a copy runs as the original) */
    for (i = 0; i < 64; i++) {
        RtcCall c;
        memcpy(s_copy + i, fn, len);
        memset(&c.t, 0, sizeof c.t);
        c.fn = (void (*)(DiagRtc*, int))(s_copy + i);
        c.days = 0;
        cache_flush();
        serialize();
        if (diag_try(do_rtc, &c)) {
            if (!faults) first_fault = i;
            faults++;
            xbox_logf("[DIAG] rtc copy mod 64 = %02x: %s (sequence at %08x)\n", i, fault_str(),
                      (unsigned)(s_copy + i + seq));
        }
    }
    if (!faults)
        xbox_logf("[DIAG] rtc copies: all 64 alignments ok (the original is mod 64 = %02x)\n", (unsigned)fn & 63);
    else
        xbox_logf("[DIAG] rtc copies: %d of 64 alignments fault, first mod 64 = %02x\n", faults, first_fault);
}
#endif

void xbox_diag_boot(void) {
#if XBOX_DIAG_CPU
    diag_cpu();
#endif
#if XBOX_DIAG_TEXT_CHECK
    diag_text_check();
#endif
#if XBOX_DIAG_RTC_TEST
    diag_rtc_test();
#endif
}

/* ---- crash: code bytes ---- */
#define CODE_BEFORE 32
#define CODE_AFTER 32
static unsigned char s_crash_ram[CODE_BEFORE + CODE_AFTER];
static unsigned s_crash_lo, s_crash_n;   /* what _code dumped, for _disk */

int xbox_diag_crash_code(unsigned eip, char* out, int cap) {
    int len = 0;
    unsigned lo, hi;
    if (!XBOX_CRASH_CODE_DUMP || cap < 16) return 0;
    s_crash_n = 0;
    lo = eip - CODE_BEFORE;
    hi = eip + CODE_AFTER;
    if (eip >= pc_image_base + 0x1000 && eip < pc_image_end) {
        if (lo < pc_image_base + 0x1000) lo = pc_image_base + 0x1000;
        if (hi > pc_image_end) hi = pc_image_end;
#if XBOX_DIAG_RTC_TEST
    } else if (eip >= (unsigned)s_copy && eip < (unsigned)s_copy + sizeof s_copy) {
        if (lo < (unsigned)s_copy) lo = (unsigned)s_copy;
        if (hi > (unsigned)s_copy + sizeof s_copy) hi = (unsigned)s_copy + sizeof s_copy;
#endif
    } else {
        return snprintf(out, (size_t)cap, "[CRASH] code: eip %08x is outside the image %08x-%08x\n", eip, pc_image_base,
                        pc_image_end);
    }
    memcpy(s_crash_ram, (const void*)lo, hi - lo);
    s_crash_lo = lo;
    s_crash_n = hi - lo;
    len += snprintf(out + len, (size_t)(cap - len), "[CRASH] eip mod 16 = %x, mod 64 = %02x, page +%03x\n", eip & 15,
                    eip & 63, eip & 4095);
    len += hex_line(out + len, cap - len, "[CRASH] ram eip-32:", s_crash_ram, (int)(eip - lo));
    len += hex_line(out + len, cap - len, "[CRASH] ram eip+0: ", s_crash_ram + (eip - lo), (int)(hi - eip));
    return len;
}

int xbox_diag_crash_disk(unsigned eip, char* out, int cap) {
    static unsigned char disk[CODE_BEFORE + CODE_AFTER];
    HANDLE h;
    unsigned off, left, i, diff = 0, first = 0;
    int len = 0;
    if (!XBOX_CRASH_CODE_DUMP || !s_crash_n || cap < 16) return 0;
    h = xbe_open();
    if (h == INVALID_HANDLE_VALUE)
        return snprintf(out, (size_t)cap, "[CRASH] disk: %s could not be opened (error %lu)\n", XBE_PATH,
                        (unsigned long)GetLastError());
    off = xbe_file_off(s_crash_lo, &left);
    if (!off || left < s_crash_n || !file_read_at(h, off, disk, s_crash_n)) {
        CloseHandle(h);
        return snprintf(out, (size_t)cap, "[CRASH] disk: %08x+%x is not in a section of %s\n", s_crash_lo, s_crash_n,
                        XBE_PATH);
    }
    CloseHandle(h);
    len += hex_line(out + len, cap - len, "[CRASH] disk eip-32:", disk, (int)(eip - s_crash_lo));
    len += hex_line(out + len, cap - len, "[CRASH] disk eip+0: ", disk + (eip - s_crash_lo), (int)(s_crash_n - (eip - s_crash_lo)));
    for (i = 0; i < s_crash_n; i++)
        if (disk[i] != s_crash_ram[i]) {
            if (!diff) first = i;
            diff++;
        }
    if (!diff)
        len += snprintf(out + len, (size_t)(cap - len), "[CRASH] ram vs disk: identical\n");
    else
        len += snprintf(out + len, (size_t)(cap - len), "[CRASH] ram vs disk: %u bytes differ, first at eip%+d (ram %02x, disk %02x)\n",
                        diff, (int)(s_crash_lo + first) - (int)eip, s_crash_ram[first], disk[first]);
    return len;
}

/* ---- lbRTC_Sub_DD shim ---- */
#if XBOX_RTC_SHIM
unsigned char lbRTC_GetDaysByMonth(unsigned short year, unsigned char month);
void lbRTC_Sub_MM(DiagRtc* t, int months);

/* src/lb_rtc.c's lbRTC_Sub_DD line for line: January takes December, and
 * month 0 (a zero date) asks lbRTC_GetDaysByMonth for month 255, as the
 * game's code does. Built -O0 (optnone) so clang emits plain compares and
 * branches, not the cmp/adc sequence at the #3 eip. */
__attribute__((optnone, noinline)) static void sub_dd_plain(DiagRtc* time, int days) {
    int day = time->day;
    int month_days;
    if (time->month == 1)
        month_days = lbRTC_GetDaysByMonth(time->year, 12);
    else
        month_days = lbRTC_GetDaysByMonth(time->year, (unsigned char)(time->month - 1));
    day -= days;
    if (day <= 0) {
        if (day == 0)
            day = month_days;
        else
            day += month_days;
        lbRTC_Sub_MM(time, 1);
    }
    time->day = (unsigned char)day;
}

void lbRTC_Sub_DD(DiagRtc* time, int days) {
    static int logged;
    if (!logged) {
        logged = 1;
        xbox_logf("[DIAG] rtc shim %s: first call, date %u/%u/%u, %d days\n",
                  g_xbox_settings.rtc_shim ? "on (plain C)" : "off (game code)", time->year, time->month, time->day, days);
    }
    if (g_xbox_settings.rtc_shim)
        sub_dd_plain(time, days);
    else
        lbRTC_Sub_DD_game(time, days);
}
#endif
