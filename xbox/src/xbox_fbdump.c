/* xbox_fbdump.c — screenshots over COM1 (xemu has no QMP screendump).
 *
 * Emits one frame as:  [FBDUMP] BEGIN w h bpp pitch
 *                      [FBDUMP] <base64 of zlib(deflate) pixels>   (many lines)
 *                      [FBDUMP] END
 * tools/xbox/fbdump_to_png.py turns a serial log into PNGs. Reads the linear
 * framebuffer the caller passes (unified RAM: the NV2A's colour buffer is plain
 * system memory, so this works for the GPU backend too).
 *
 * On the console there is no COM1: xbox_fbdump_bmp writes a frame to a BMP
 * file instead (the screenshots setting, and -DXBOX_NES_SHOT). */
#include <stdlib.h>
#include <string.h>
#define Z_SOLO   /* how nxdk builds libzlib: no compress.c, caller-supplied allocator */
#include <zlib.h>
#include <windows.h>
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
    /* streamed: a 1.2 MB frame never fits the 64 MB heap in one piece */
    static unsigned char out[32 * 1024 + 64];
    z_stream zs;
    size_t have = 0;   /* bytes in out[] not yet base64'd */
    int prev = g_xbox_log, rc, y;
    memset(&zs, 0, sizeof zs);
    zs.zalloc = z_alloc;
    zs.zfree = z_free;
    if (deflateInit2(&zs, 1, Z_DEFLATED, 9, 1, Z_DEFAULT_STRATEGY) != Z_OK) { xbox_logf("[FBDUMP] ERROR deflateInit\n"); return; }
    g_xbox_log = 1;
    xbox_log_exclusive(1);
    xbox_logf("[FBDUMP] BEGIN %d %d %d %d\n", w, h, bpp, pitch);
    for (y = 0; y <= h; y++) {
        int flush = y == h ? Z_FINISH : Z_NO_FLUSH;
        zs.next_in = y < h ? (Bytef*)fb + (size_t)y * (size_t)pitch : (Bytef*)fb;
        zs.avail_in = y < h ? (uInt)pitch : 0;
        do {
            size_t emit;
            zs.next_out = out + have;
            zs.avail_out = (uInt)(32 * 1024 - have);
            rc = deflate(&zs, flush);
            have = 32 * 1024 - zs.avail_out;
            /* base64 whole 57-byte lines only; keep the tail for the next pass */
            emit = rc == Z_STREAM_END ? have : have - have % 57;
            if (emit) {
                emit_b64(out, emit);
                memmove(out, out + emit, have - emit);
                have -= emit;
            }
        } while (zs.avail_out == 0 || (flush == Z_FINISH && rc != Z_STREAM_END));
    }
    xbox_logf("[FBDUMP] END\n");
    xbox_log_exclusive(0);
    g_xbox_log = prev;
    deflateEnd(&zs);
}

/* 24-bit bottom-up BMP (from Melee-X's xhw_fbdump_file). Rows are converted
 * one at a time (the framebuffer is write-combined: each row is read once,
 * in order) and written 8 at a time. Returns 0 if the file could not be
 * written. */
int xbox_fbdump_bmp(const char* path, const void* fb, int w, int h, int bpp, int pitch) {
    enum { ROWS = 8 };
    unsigned char* buf;   /* 30 KB, only while a shot is written */
    unsigned char hdr[54];
    unsigned stride = ((unsigned)w * 3 + 3) & ~3u, size = 54 + stride * (unsigned)h;
    DWORD done;
    HANDLE f;
    int x, y, n = 0;
    if (w > 1280 || w <= 0 || h <= 0) return 0;
    buf = (unsigned char*)calloc(ROWS, stride);
    if (!buf) return 0;
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        free(buf);
        return 0;
    }
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B';
    hdr[1] = 'M';
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    WriteFile(f, hdr, sizeof hdr, &done, NULL);
    for (y = h - 1; y >= 0; y--) {
        const unsigned char* src = (const unsigned char*)fb + (size_t)y * (size_t)pitch;
        unsigned char* row = buf + n * stride;
        for (x = 0; x < w; x++) {
            unsigned char* d = row + x * 3;
            if (bpp == 16) {
                unsigned v = ((const unsigned short*)src)[x], r = v >> 11, g = (v >> 5) & 63, b = v & 31;
                d[0] = (unsigned char)(b << 3 | b >> 2);
                d[1] = (unsigned char)(g << 2 | g >> 4);
                d[2] = (unsigned char)(r << 3 | r >> 2);
            } else {
                unsigned v = ((const unsigned*)src)[x];
                d[0] = (unsigned char)v;
                d[1] = (unsigned char)(v >> 8);
                d[2] = (unsigned char)(v >> 16);
            }
        }
        if (++n == ROWS || y == 0) {
            WriteFile(f, buf, (DWORD)(n * stride), &done, NULL);
            n = 0;
        }
    }
    CloseHandle(f);
    free(buf);
    return 1;
}
