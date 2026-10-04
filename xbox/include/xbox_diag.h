/* xbox_diag.h — CPU and code-integrity diagnostics (xbox/src/xbox_diag.c).
 *
 * Written for GitHub #2/#3 (docs/known-issues.md, "Crash in lbRTC_Sub_DD on
 * an upgraded console"): a 1 GHz Coppermine cC0 swap faults on a read of
 * d68301f8 at `xor esi, esi` in lbRTC_Sub_DD, which is what the bytes after
 * that instruction decode to if the CPU starts one byte late. The pieces
 * here tell "the CPU decodes it wrong" from "the bytes in RAM aren't the
 * bytes on disk", without reaching the title screen.
 *
 * Everything is off unless built with -DXBOX_DIAG_ISSUE3=ON (CMake; the
 * one-off troubleshooting build for #3, docs/toolchain.md). Each piece has
 * its own define, so one can be turned on alone with XBOX_CMAKE_ARGS. */
#ifndef XBOX_DIAG_H
#define XBOX_DIAG_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef XBOX_DIAG_ISSUE3
#define XBOX_DIAG_ISSUE3 0
#endif
/* boot.log: cpuid leaves, microcode revision (MSR 8Bh), a few P6 MSRs,
 * CR0/CR4, kernel version, the XBE path */
#ifndef XBOX_DIAG_CPU
#define XBOX_DIAG_CPU XBOX_DIAG_ISSUE3
#endif
/* boot.log: .text and .rdata in RAM compared byte for byte with
 * D:\default.xbe (the kernel's thunk table in .rdata excepted) */
#ifndef XBOX_DIAG_TEXT_CHECK
#define XBOX_DIAG_TEXT_CHECK XBOX_DIAG_ISSUE3
#endif
/* boot.log: lbRTC_Sub_DD on an all-zero date (the #3 path) under an
 * exception guard, from the real address and from copies at every
 * alignment mod 64 */
#ifndef XBOX_DIAG_RTC_TEST
#define XBOX_DIAG_RTC_TEST XBOX_DIAG_ISSUE3
#endif
/* crash.log: the code bytes around the eip from RAM and from the XBE on
 * disk, and whether they differ */
#ifndef XBOX_CRASH_CODE_DUMP
#define XBOX_CRASH_CODE_DUMP XBOX_DIAG_ISSUE3
#endif
/* CMake -DXBOX_RTC_SHIM=ON (implied by XBOX_DIAG_ISSUE3): src/lb_rtc.c is
 * compiled with lbRTC_Sub_DD renamed lbRTC_Sub_DD_game and xbox_diag.c
 * defines lbRTC_Sub_DD, which runs a plain -O0 copy of the same C when
 * settings.ini [Xbox] rtc_shim = 1 (default 0: the game's own code). */
#ifndef XBOX_RTC_SHIM
#define XBOX_RTC_SHIM 0
#endif

/* the boot-time pieces, after boot.log is open and the image range known */
void xbox_diag_boot(void);
/* crash report lines (each "[CRASH] ...\n"), written into out; returns the
 * length. _code reads RAM only (safe at any IRQL when eip is in the image);
 * _disk opens the XBE on disk, PASSIVE_LEVEL only. */
int xbox_diag_crash_code(unsigned eip, char* out, int cap);
int xbox_diag_crash_disk(unsigned eip, char* out, int cap);

#ifdef __cplusplus
}
#endif
#endif
