/* xbox_mem.c — word-at-a-time memcpy/memmove/memset/memcmp.
 *
 * nxdk's pdclib implements all four as byte loops. EIP sampling at the title
 * demo put them at ~45% of CPU time (texture decode, ARAM DMA copies, the
 * vertex batcher, JSystem heaps). These use rep movsd/stosd, which the
 * Pentium III runs at a dword per clock once aligned. Our objects link
 * before libpdclib.lib and pdclib keeps each function in its own member, so
 * these replace pdclib's with no duplicate symbols.
 * Kill switch: -DXBOX_FAST_MEM=0 (pdclib's versions come back). */
#include <stddef.h>
#include <stdint.h>

#ifndef XBOX_FAST_MEM
#define XBOX_FAST_MEM 1
#endif

#if XBOX_FAST_MEM
/* the bodies must not be turned back into calls to themselves */
#define NOBUILTIN __attribute__((no_builtin))
typedef uint32_t __attribute__((may_alias, aligned(1))) u32_any;

static inline void copy_fwd(unsigned char* d, const unsigned char* s, size_t n) {
    size_t head, words;
    if (n >= 16) {
        head = (size_t)(-(uintptr_t)d & 3);   /* align the destination */
        n -= head;
        __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(head) : : "memory");
        words = n >> 2;
        n &= 3;
        __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(words) : : "memory");
    }
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
}

NOBUILTIN void* memcpy(void* restrict dst, const void* restrict src, size_t n) {
    copy_fwd((unsigned char*)dst, (const unsigned char*)src, n);
    return dst;
}

NOBUILTIN void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d <= s || d >= s + n) {
        copy_fwd(d, s, n);
    } else {
        /* overlapping, dst above src: copy backwards, tail bytes first */
        size_t words = n >> 2, tail = n & 3;
        d += n;
        s += n;
        while (tail--) *--d = *--s;
        if (words) {
            unsigned char* dd = d - 4;
            const unsigned char* ss = s - 4;
            __asm__ volatile("std\n\trep movsl\n\tcld" : "+D"(dd), "+S"(ss), "+c"(words) : : "memory");
        }
    }
    return dst;
}

NOBUILTIN void* memset(void* dst, int c, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    uint32_t v = (uint8_t)c * 0x01010101u;
    if (n >= 16) {
        size_t head = (size_t)(-(uintptr_t)d & 3), words;
        n -= head;
        __asm__ volatile("rep stosb" : "+D"(d), "+c"(head) : "a"(v) : "memory");
        words = n >> 2;
        n &= 3;
        __asm__ volatile("rep stosl" : "+D"(d), "+c"(words) : "a"(v) : "memory");
    }
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(v) : "memory");
    return dst;
}

NOBUILTIN int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* p = (const unsigned char*)a;
    const unsigned char* q = (const unsigned char*)b;
    /* skip equal dwords (unaligned loads are fine on x86), then find the byte */
    while (n >= 4 && *(const u32_any*)p == *(const u32_any*)q) {
        p += 4;
        q += 4;
        n -= 4;
    }
    while (n--) {
        if (*p != *q) return *p - *q;
        p++;
        q++;
    }
    return 0;
}
#endif
