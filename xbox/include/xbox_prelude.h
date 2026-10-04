/* xbox_prelude.h — force-included into every TU of the Xbox build.
 * Compat fixes for nxdk (clang i386-pc-win32, pdclib) live here or in xbox/src,
 * never in src/ (CLAUDE.md §1). Keep each entry with its reason. */
#ifndef XBOX_PRELUDE_H
#define XBOX_PRELUDE_H

/* MSVCRT's _tolower (JKRFileLoader.cpp); pdclib has only tolower. Only ever
 * called on upper-case input there, where the two agree. */
#define _tolower(c) tolower(c)

/* POSIX strcasecmp (famicom.cpp); impl in xbox/src/xbox_posix.c. */
#ifdef __cplusplus
extern "C" {
#endif
int strcasecmp(const char* a, const char* b);
int strncasecmp(const char* a, const char* b, unsigned int n);
#ifdef __cplusplus
}
#endif

/* C TUs: route relative-path file I/O and printf-family logging through
 * xbox/src/xbox_io.c (see its header comment). C++ TUs are left alone —
 * libc++'s <cstdio> re-exports these names. */
#ifndef __cplusplus
#include <stdio.h>
#include "xbox_io.h"
#define fopen    xbox_fopen
#define remove   xbox_remove
#define rename   xbox_rename
#define printf   xbox_printf
#define vprintf  xbox_vprintf
#define fprintf  xbox_fprintf
#define vfprintf xbox_vfprintf
#define puts     xbox_puts
#ifdef XBOX_DISC_TU   /* pc_disc.c: the disc image reads without pdclib (xbox_io.c) */
#define fread    xbox_disc_fread
#define fseek    xbox_disc_fseek
#else
#define fread    xbox_fread   /* timed for the hitch log (disc reads) */
#endif
#define fclose   xbox_fclose  /* flushes FATX so saves survive a power-off */

/* memcpy, memmove, memset and memcmp as compiler builtins (from Melee-X,
 * where they were ~18% of a console frame). nxdk-cc builds freestanding,
 * which implies -fno-builtin: every fixed-size copy and compare was a real
 * call into xbox_mem.c (81 call sites in pc_gx.o alone). As builtins,
 * constant sizes become inline moves and compares; the rest still call
 * xbox_mem.c. C only: libc++ headers spell std::memcpy. string.h and the
 * decomp's _mem.h first, so their prototypes are declared before the names
 * become macros. xbox_mem.c,
 * which defines the four, undefines them. Kill switch: -DXBOX_BUILTIN_MEM=0. */
#ifndef XBOX_BUILTIN_MEM
#define XBOX_BUILTIN_MEM 1
#endif
#if XBOX_BUILTIN_MEM
#include <string.h>
#include "_mem.h"   /* the decomp declares them again: here, before the macros */
#define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define memmove(d, s, n) __builtin_memmove((d), (s), (n))
#define memset(d, c, n) __builtin_memset((d), (c), (n))
#define memcmp(a, b, n) __builtin_memcmp((a), (b), (n))
#endif
#endif

#endif /* XBOX_PRELUDE_H */
