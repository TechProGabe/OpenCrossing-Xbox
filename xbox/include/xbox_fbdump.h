/* xbox_fbdump.h — framebuffer -> COM1 screenshot (xbox/src/xbox_fbdump.c). */
#ifndef XBOX_FBDUMP_H
#define XBOX_FBDUMP_H
#ifdef __cplusplus
extern "C" {
#endif
void xbox_fbdump(const void* fb, int w, int h, int bpp, int pitch);
#ifdef __cplusplus
}
#endif
#endif
