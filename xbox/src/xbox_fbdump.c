/* xbox_fbdump.c — screenshots over COM1 (xemu has no QMP screendump).
 *
 * Emits one frame as:  [FBDUMP] BEGIN w h bpp pitch
 *                      [FBDUMP] <base64 of zlib(deflate) pixels>   (many lines)
 *                      [FBDUMP] END
 * tools/xbox/fbdump_to_png.py turns a serial log into PNGs. Reads the linear
 * framebuffer the caller passes (unified RAM: the NV2A's colour buffer is plain
 * system memory, so this works for the GPU backend too). */
#include <stdlib.h>
#include <string.h>
#define Z_SOLO   /* how nxdk builds libzlib: no compress.c, caller-supplied allocator */
#include <zlib.h>
#include "xbox_io.h"
#include "xbox_fbdump.h"

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void emit_b64(const unsigned char* p, size_t n) {
    char line[16 + 76 + 2];
    size_t i = 0;
    while (i < n) {
        int k = 0;
        memcpy(line, "[FBDUMP] ", 9);
        k = 9;
        while (i < n && k < 9 + 76) {
            unsigned v = (unsigned)p[i] << 16;
            int m = 1;
            if (i + 1 < n) { v |= (unsigned)p[i + 1] << 8; m++; }
            if (i + 2 < n) { v |= p[i + 2]; m++; }
            line[k++] = B64[(v >> 18) & 63];
            line[k++] = B64[(v >> 12) & 63];
            line[k++] = m > 1 ? B64[(v >> 6) & 63] : '=';
            line[k++] = m > 2 ? B64[v & 63] : '=';
            i += 3;
        }
        line[k++] = '\n';
        xbox_log_write(line, (size_t)k);
    }
}

static voidpf z_alloc(voidpf o, uInt n, uInt sz) { (void)o; return calloc(n, sz); }
static void z_free(voidpf o, voidpf p) { (void)o; free(p); }

void xbox_fbdump(const void* fb, int w, int h, int bpp, int pitch) {
    z_stream zs;
    uLong cap = (uLong)(pitch * h) + (uLong)(pitch * h) / 1000 + 64;
    unsigned char* z = (unsigned char*)malloc(cap);
    int prev = g_xbox_log;
    if (!z) { xbox_logf("[FBDUMP] ERROR no memory\n"); return; }
    memset(&zs, 0, sizeof zs);
    zs.zalloc = z_alloc;
    zs.zfree = z_free;
    if (deflateInit(&zs, 6) != Z_OK) { free(z); xbox_logf("[FBDUMP] ERROR deflateInit\n"); return; }
    zs.next_in = (Bytef*)fb;
    zs.avail_in = (uInt)(pitch * h);
    zs.next_out = z;
    zs.avail_out = (uInt)cap;
    if (deflate(&zs, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&zs);
        free(z);
        xbox_logf("[FBDUMP] ERROR deflate\n");
        return;
    }
    g_xbox_log = 1;
    xbox_logf("[FBDUMP] BEGIN %d %d %d %d\n", w, h, bpp, pitch);
    emit_b64(z, zs.total_out);
    xbox_logf("[FBDUMP] END\n");
    g_xbox_log = prev;
    deflateEnd(&zs);
    free(z);
}
